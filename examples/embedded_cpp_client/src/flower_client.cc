// Copyright 2026 Flower Labs GmbH. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ==============================================================================
// Reconnect/retry and TLS are not implemented yet. Fleet requests are signed
// with the node's ECDSA key. The Reconnect field on pull/push responses is
// ignored, as in the Python grpc-rere client. Message content travels as Flower
// objects (see object_codec.h).
#include "flower_client.h"
#include "fleet_transport.h"
#include "heartbeat.h"
#include "message_codec.h"
#include "node_credentials.h"
#include "object_codec.h"

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace flwr_local {

namespace {

// Heartbeat interval in seconds. Must be within [10, 1800] (the SuperLink
// validates this). 30s is the SuperLink default.
constexpr double HEARTBEAT_INTERVAL_SECONDS = 30.0;

// Polling interval in seconds when no message is available.
constexpr int POLL_INTERVAL_SECONDS = 3;

// PullObject is retried while the SuperLink reports an object as not yet
// available.
constexpr int OBJECT_PULL_MAX_ATTEMPTS = 20;
constexpr int OBJECT_PULL_RETRY_MILLISECONDS = 500;

// Global reference for the signal handler.
FlowerClient* g_client = nullptr;

void signal_handler(int) { g_client->stop(); }

// Pulls every object of a message's tree into `objects`.
bool pull_tree_objects(FleetTransport* transport, uint64_t node_id,
                       uint64_t run_id, const flwr::proto::ObjectTree& tree,
                       ObjectMap* objects, std::string* error) {
  for (const std::string& object_id : tree_object_ids(tree)) {
    for (int attempt = 1;; attempt++) {
      flwr::proto::PullObjectResponse response;
      if (!transport->pull_object(node_id, run_id, object_id, &response)) {
        *error = "PullObject failed for " + object_id;
        return false;
      }
      if (!response.object_found()) {
        *error = "object not found: " + object_id;
        return false;
      }
      if (response.object_available()) {
        (*objects)[object_id] = response.object_content();
        break;
      }
      if (attempt >= OBJECT_PULL_MAX_ATTEMPTS) {
        *error = "object not available after retries: " + object_id;
        return false;
      }
      std::this_thread::sleep_for(
          std::chrono::milliseconds(OBJECT_PULL_RETRY_MILLISECONDS));
    }
  }
  return true;
}

// Pulls an incoming message's objects, rebuilds the inline Message, and
// confirms receipt to the SuperLink.
bool receive_message(FleetTransport* transport, uint64_t node_id,
                     uint64_t run_id, const flwr::proto::ObjectTree& tree,
                     flwr::proto::Message* message, std::string* error) {
  ObjectMap objects;
  if (!pull_tree_objects(transport, node_id, run_id, tree, &objects, error)) {
    return false;
  }
  if (!inflate_message(tree.object_id(), objects, message, error)) {
    return false;
  }
  if (!transport->confirm_message_received(node_id, run_id, tree.object_id())) {
    *error = "ConfirmMessageReceived failed";
    return false;
  }
  return true;
}

// Sends a reply. The Message metadata goes in PushMessages with its object ID
// as message_id, and the objects the SuperLink asks for are uploaded with
// PushObject.
bool send_reply(FleetTransport* transport, uint64_t node_id, uint64_t run_id,
                const flwr::proto::Message& reply) {
  DeflatedMessage deflated;
  deflate_message(reply, &deflated);

  flwr::proto::Message metadata_only = reply;
  metadata_only.clear_content();
  metadata_only.mutable_metadata()->set_message_id(deflated.root_id);

  flwr::proto::PushMessagesRequest push_request;
  push_request.mutable_node()->set_node_id(node_id);
  *push_request.add_messages_list() = metadata_only;
  *push_request.add_message_object_trees() = deflated.tree;

  flwr::proto::PushMessagesResponse push_response;
  if (!transport->push_messages(node_id, push_request, &push_response)) {
    return false;
  }

  for (const std::string& object_id : push_response.objects_to_push()) {
    auto found = deflated.objects.find(object_id);
    if (found == deflated.objects.end()) {
      std::cerr << "SuperLink requested unknown object " << object_id
                << std::endl;
      return false;
    }
    flwr::proto::PushObjectResponse object_response;
    if (!transport->push_object(node_id, run_id, object_id, found->second,
                                push_response.session_id(),
                                &object_response) ||
        !object_response.stored()) {
      std::cerr << "PushObject failed for " << object_id << std::endl;
      return false;
    }
  }
  return true;
}

}  // namespace

FlowerClient::FlowerClient(const std::string& server_address,
                           const std::string& client_id, Trainer* trainer)
    : server_address_(server_address), client_id_(client_id),
      trainer_(trainer) {}

void FlowerClient::stop() { running_ = false; }

void FlowerClient::run() {
  // Install SIGINT handler for graceful shutdown.
  g_client = this;
  std::signal(SIGINT, signal_handler);

  NodeCredentials credentials = NodeCredentials::generate();
  const std::string public_key = credentials.public_key_pem();
  FleetTransport transport(server_address_, credentials);

  // The public key identifies this node to the SuperLink. A fresh key pair is
  // generated per process, so each run registers a new node.

  std::cout << "Registering node with public key: " << public_key << std::endl;
  uint64_t node_id = transport.register_node(public_key);
  if (node_id == 0) {
    std::cerr << "Failed to register node" << std::endl;
    return;
  }
  std::cout << "Registered node, node_id=" << node_id << std::endl;

  std::cout << "Activating node" << std::endl;
  uint64_t activated_node_id =
      transport.activate_node(public_key, HEARTBEAT_INTERVAL_SECONDS);
  if (activated_node_id == 0) {
    std::cerr << "Failed to activate node" << std::endl;
    return;
  }
  std::cout << "Activated node, node_id=" << activated_node_id << std::endl;

  // Start the background heartbeat.
  Heartbeat heartbeat(&transport, activated_node_id,
                      HEARTBEAT_INTERVAL_SECONDS);
  std::cout << "Heartbeat started (interval=" << HEARTBEAT_INTERVAL_SECONDS
            << "s)" << std::endl;

  // Poll loop. The SuperLink sets Reconnect on every push response, so that
  // field is not acted on here.
  while (running_) {
    flwr::proto::PullMessagesResponse pull_response;
    if (!transport.pull_messages(activated_node_id, &pull_response)) {
      std::cerr << "PullMessages failed, retrying in " << POLL_INTERVAL_SECONDS
                << "s" << std::endl;
      std::this_thread::sleep_for(
          std::chrono::seconds(POLL_INTERVAL_SECONDS));
      continue;
    }

    // Tolerate an empty response.
    if (pull_response.messages_list_size() == 0) {
      std::this_thread::sleep_for(std::chrono::seconds(POLL_INTERVAL_SECONDS));
      continue;
    }

    for (int i = 0; i < pull_response.messages_list_size(); i++) {
      if (i >= pull_response.message_object_trees_size()) {
        std::cerr << "Message " << i << " has no object tree, skipping"
                  << std::endl;
        continue;
      }

      const flwr::proto::ObjectTree& tree =
          pull_response.message_object_trees(i);
      const uint64_t run_id =
          pull_response.messages_list(i).metadata().run_id();
      std::cout << "Received message type="
                << pull_response.messages_list(i).metadata().message_type()
                << " message_id=" << tree.object_id() << " run_id=" << run_id
                << std::endl;

      flwr::proto::Message incoming;
      std::string error;
      if (!receive_message(&transport, activated_node_id, run_id, tree,
                           &incoming, &error)) {
        std::cerr << "Failed to receive message: " << error << std::endl;
        continue;
      }

      flwr::proto::Message* reply =
          handle_message(incoming, activated_node_id, trainer_);
      if (reply == nullptr) {
        std::cerr << "Failed to handle message, skipping" << std::endl;
        continue;
      }

      if (send_reply(&transport, activated_node_id, run_id, *reply)) {
        std::cout << "Pushed reply for message_id=" << tree.object_id()
                  << std::endl;
      } else {
        std::cerr << "Failed to push reply" << std::endl;
      }

      delete reply;
    }
  }

  // Graceful shutdown.
  std::cout << "Stopping heartbeat" << std::endl;
  heartbeat.stop();

  std::cout << "Deactivating node" << std::endl;
  if (!transport.deactivate_node(activated_node_id)) {
    std::cerr << "Failed to deactivate node" << std::endl;
  }

  // Phase 1 decision: do not unregister. Each run generates a new key pair and
  // registers a new node, so unregistering would leave stale nodes behind.
  std::cout << "Client shut down" << std::endl;
}

}  // namespace flwr_local
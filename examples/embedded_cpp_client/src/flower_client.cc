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
// flower_client.cc
//
// Implements the current Fleet lifecycle:
//   RegisterNode -> ActivateNode -> heartbeat -> PullMessages/PushMessages
//   loop -> DeactivateNode.
//
// Phase 1 scope: register, activate, heartbeat, poll, cleanly deactivate.
// Phase 2 scope: decode Message/RecordDict, dispatch to the trainer, build
// reply messages with reply metadata, push replies.
//
// Reconnect/retry, object transfer, TLS, and SuperNode authentication are
// follow-up phases (3-5).
#include "flower_client.h"
#include "fleet_transport.h"
#include "heartbeat.h"
#include "message_codec.h"

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

// Global reference for the signal handler.
FlowerClient* g_client = nullptr;

void signal_handler(int) { g_client->stop(); }

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

  FleetTransport transport(server_address_);

  // Derive a stable public key from the client ID (Phase 1 decision: no
  // persistence or key management yet).
  const std::string public_key = "client-" + client_id_;

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

  // Poll loop.
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

    // Phase 2: handle one message at a time.
    for (int i = 0; i < pull_response.messages_list_size(); i++) {
      const flwr::proto::Message& incoming = pull_response.messages_list(i);

      std::cout << "Received message type="
                << incoming.metadata().message_type()
                << " message_id=" << incoming.metadata().message_id()
                << " run_id=" << incoming.metadata().run_id() << std::endl;

      flwr::proto::Message* reply =
          handle_message(incoming, activated_node_id, trainer_);
      if (reply == nullptr) {
        std::cerr << "Failed to handle message, skipping" << std::endl;
        continue;
      }

      // Build the PushMessages request with the reply and an empty object
      // tree (payloads are inline in Phase 2).
      flwr::proto::PushMessagesRequest push_request;
      push_request.mutable_node()->set_node_id(activated_node_id);
      *push_request.add_messages_list() = *reply;
      push_request.add_message_object_trees();  // empty ObjectTree

      flwr::proto::PushMessagesResponse push_response;
      if (!transport.push_messages(activated_node_id, push_request,
                                   &push_response)) {
        std::cerr << "PushMessages failed" << std::endl;
      } else {
        std::cout << "Pushed reply for message_id="
                  << incoming.metadata().message_id() << std::endl;
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

  // Phase 1 decision: do not unregister; the identity is intended to be
  // reused across runs.
  std::cout << "Client shut down" << std::endl;
}

}  // namespace flwr_local
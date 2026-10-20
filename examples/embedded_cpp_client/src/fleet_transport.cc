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
// fleet_transport.cc
#include "fleet_transport.h"

#include <ctime>
#include <iostream>

namespace flwr_local {

namespace {

// Current UTC time as ISO-8601 with second precision, e.g. 2026-01-31T12:00:00Z.
std::string utc_timestamp() {
  std::time_t now = std::time(nullptr);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buf;
}

}  // namespace

FleetTransport::FleetTransport(const std::string& server_address,
                               NodeCredentials credentials,
                               int grpc_max_message_length)
    : credentials_(std::move(credentials)) {
  grpc::ChannelArguments args;
  args.SetMaxReceiveMessageSize(grpc_max_message_length);
  args.SetMaxSendMessageSize(grpc_max_message_length);

  // Insecure gRPC connection (trusted development network only).
  std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(
      server_address, grpc::InsecureChannelCredentials(), args);

  stub_ = flwr::proto::Fleet::NewStub(channel);
}

void FleetTransport::add_auth_metadata(grpc::ClientContext* context) const {
  const std::string timestamp = utc_timestamp();
  context->AddMetadata("flwr-public-key-bin", credentials_.public_key_pem());
  context->AddMetadata("flwr-timestamp", timestamp);
  context->AddMetadata("flwr-signature-bin", credentials_.sign(timestamp));
}

uint64_t FleetTransport::register_node(const std::string& public_key) {
  flwr::proto::RegisterNodeFleetRequest request;
  request.set_public_key(public_key);

  flwr::proto::RegisterNodeFleetResponse response;
  grpc::ClientContext context;
  add_auth_metadata(&context);
  grpc::Status status = stub_->RegisterNode(&context, request, &response);
  if (!status.ok()) {
    std::cerr << "RegisterNode RPC failed: " << status.error_message()
              << std::endl;
    return 0;
  }
  return response.node_id();
}

uint64_t FleetTransport::activate_node(const std::string& public_key,
                                       double heartbeat_interval) {
  flwr::proto::ActivateNodeRequest request;
  request.set_public_key(public_key);
  request.set_heartbeat_interval(heartbeat_interval);

  flwr::proto::ActivateNodeResponse response;
  grpc::ClientContext context;
  add_auth_metadata(&context);
  grpc::Status status = stub_->ActivateNode(&context, request, &response);
  if (!status.ok()) {
    std::cerr << "ActivateNode RPC failed: " << status.error_message()
              << std::endl;
    return 0;
  }
  return response.node_id();
}

bool FleetTransport::deactivate_node(uint64_t node_id) {
  flwr::proto::DeactivateNodeRequest request;
  request.set_node_id(node_id);

  flwr::proto::DeactivateNodeResponse response;
  grpc::ClientContext context;
  add_auth_metadata(&context);
  grpc::Status status = stub_->DeactivateNode(&context, request, &response);
  if (!status.ok()) {
    std::cerr << "DeactivateNode RPC failed: " << status.error_message()
              << std::endl;
    return false;
  }
  return true;
}

bool FleetTransport::unregister_node(uint64_t node_id) {
  flwr::proto::UnregisterNodeFleetRequest request;
  request.set_node_id(node_id);

  flwr::proto::UnregisterNodeFleetResponse response;
  grpc::ClientContext context;
  add_auth_metadata(&context);
  grpc::Status status = stub_->UnregisterNode(&context, request, &response);
  if (!status.ok()) {
    std::cerr << "UnregisterNode RPC failed: " << status.error_message()
              << std::endl;
    return false;
  }
  return true;
}

bool FleetTransport::send_node_heartbeat(uint64_t node_id,
                                         double heartbeat_interval) {
  flwr::proto::SendNodeHeartbeatRequest request;
  request.mutable_node()->set_node_id(node_id);
  request.set_heartbeat_interval(heartbeat_interval);

  flwr::proto::SendNodeHeartbeatResponse response;
  grpc::ClientContext context;
  add_auth_metadata(&context);
  grpc::Status status = stub_->SendNodeHeartbeat(&context, request, &response);
  if (!status.ok()) {
    std::cerr << "SendNodeHeartbeat RPC failed: " << status.error_message()
              << std::endl;
    return false;
  }
  return response.success();
}

bool FleetTransport::pull_messages(
    uint64_t node_id, flwr::proto::PullMessagesResponse* response) {
  flwr::proto::PullMessagesRequest request;
  request.mutable_node()->set_node_id(node_id);

  grpc::ClientContext context;
  add_auth_metadata(&context);
  grpc::Status status = stub_->PullMessages(&context, request, response);
  if (!status.ok()) {
    std::cerr << "PullMessages RPC failed: " << status.error_message()
              << std::endl;
    return false;
  }
  return true;
}

bool FleetTransport::push_messages(
    uint64_t node_id, const flwr::proto::PushMessagesRequest& request,
    flwr::proto::PushMessagesResponse* response) {
  grpc::ClientContext context;
  add_auth_metadata(&context);
  grpc::Status status = stub_->PushMessages(&context, request, response);
  if (!status.ok()) {
    std::cerr << "PushMessages RPC failed: " << status.error_message()
              << std::endl;
    return false;
  }
  return true;
}

bool FleetTransport::pull_object(uint64_t node_id, uint64_t run_id,
                                 const std::string& object_id,
                                 flwr::proto::PullObjectResponse* response) {
  flwr::proto::PullObjectRequest request;
  request.mutable_node()->set_node_id(node_id);
  request.set_run_id(run_id);
  request.set_object_id(object_id);

  grpc::ClientContext context;
  add_auth_metadata(&context);
  grpc::Status status = stub_->PullObject(&context, request, response);
  if (!status.ok()) {
    std::cerr << "PullObject RPC failed: " << status.error_message()
              << std::endl;
    return false;
  }
  return true;
}

bool FleetTransport::push_object(uint64_t node_id, uint64_t run_id,
                                 const std::string& object_id,
                                 const std::string& object_content,
                                 const std::string& session_id,
                                 flwr::proto::PushObjectResponse* response) {
  flwr::proto::PushObjectRequest request;
  request.mutable_node()->set_node_id(node_id);
  request.set_run_id(run_id);
  request.set_object_id(object_id);
  request.set_object_content(object_content);
  request.set_session_id(session_id);

  grpc::ClientContext context;
  add_auth_metadata(&context);
  grpc::Status status = stub_->PushObject(&context, request, response);
  if (!status.ok()) {
    std::cerr << "PushObject RPC failed: " << status.error_message()
              << std::endl;
    return false;
  }
  return true;
}

bool FleetTransport::confirm_message_received(
    uint64_t node_id, uint64_t run_id, const std::string& message_object_id) {
  flwr::proto::ConfirmMessageReceivedRequest request;
  request.mutable_node()->set_node_id(node_id);
  request.set_run_id(run_id);
  request.set_message_object_id(message_object_id);

  flwr::proto::ConfirmMessageReceivedResponse response;
  grpc::ClientContext context;
  add_auth_metadata(&context);
  grpc::Status status =
      stub_->ConfirmMessageReceived(&context, request, &response);
  if (!status.ok()) {
    std::cerr << "ConfirmMessageReceived RPC failed: "
              << status.error_message() << std::endl;
    return false;
  }
  return true;
}

}  // namespace flwr_local

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

#include <iostream>

namespace flwr_local {

FleetTransport::FleetTransport(const std::string& server_address,
                               int grpc_max_message_length) {
  grpc::ChannelArguments args;
  args.SetMaxReceiveMessageSize(grpc_max_message_length);
  args.SetMaxSendMessageSize(grpc_max_message_length);

  // Insecure gRPC connection (trusted development network only).
  std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(
      server_address, grpc::InsecureChannelCredentials(), args);

  stub_ = flwr::proto::Fleet::NewStub(channel);
}

uint64_t FleetTransport::register_node(const std::string& public_key) {
  flwr::proto::RegisterNodeFleetRequest request;
  request.set_public_key(public_key);

  flwr::proto::RegisterNodeFleetResponse response;
  grpc::ClientContext context;
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
  grpc::Status status = stub_->PushMessages(&context, request, response);
  if (!status.ok()) {
    std::cerr << "PushMessages RPC failed: " << status.error_message()
              << std::endl;
    return false;
  }
  return true;
}

}  // namespace flwr_local
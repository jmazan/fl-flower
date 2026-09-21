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
// fleet_transport.h
//
// gRPC channel and Fleet stub wrapper. Implements the current Fleet RPCs used
// by the client lifecycle: RegisterNode, ActivateNode, DeactivateNode,
// UnregisterNode, SendNodeHeartbeat, PullMessages, PushMessages.
#ifndef FLEET_TRANSPORT_H
#define FLEET_TRANSPORT_H

#include "flwr/proto/fleet.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <string>

namespace flwr_local {

class FleetTransport {
 public:
  // Establish an insecure gRPC channel to the SuperLink Fleet API.
  // server_address is "host:port", e.g. "127.0.0.1:9092".
  FleetTransport(const std::string& server_address,
                 int grpc_max_message_length = 536870912);  // 512 MiB

  // Register a node with the given public key. Returns the node ID.
  // Returns 0 on failure.
  uint64_t register_node(const std::string& public_key);

  // Activate the node. Returns the node ID, or 0 on failure.
  uint64_t activate_node(const std::string& public_key,
                         double heartbeat_interval);

  // Deactivate the node.
  bool deactivate_node(uint64_t node_id);

  // Unregister the node.
  bool unregister_node(uint64_t node_id);

  // Send a heartbeat. Returns true on success.
  bool send_node_heartbeat(uint64_t node_id, double heartbeat_interval);

  // Pull messages for the node. Returns true on success; on success the
  // response is stored in *response.
  bool pull_messages(uint64_t node_id,
                     flwr::proto::PullMessagesResponse* response);

  // Push reply messages. Returns true on success; on success the response is
  // stored in *response.
  bool push_messages(uint64_t node_id,
                     const flwr::proto::PushMessagesRequest& request,
                     flwr::proto::PushMessagesResponse* response);

 private:
  std::unique_ptr<flwr::proto::Fleet::Stub> stub_;
};

}  // namespace flwr_local

#endif  // FLEET_TRANSPORT_H
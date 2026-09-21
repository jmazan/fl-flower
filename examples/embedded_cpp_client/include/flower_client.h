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
// flower_client.h
//
// Public lifecycle API of the native C++ Flower Fleet client.
#ifndef FLOWER_CLIENT_H
#define FLOWER_CLIENT_H

#include "flower_trainer.h"

#include <string>

namespace flwr_local {

class FlowerClient {
 public:
  // server_address: "host:port" of the SuperLink Fleet API (e.g.
  // "127.0.0.1:9092").
  // client_id: stable identifier used to derive the node public key
  // (e.g. "client-0").
  // trainer: the model/training callback implementation.
  FlowerClient(const std::string& server_address, const std::string& client_id,
               Trainer* trainer);

  // Run the client lifecycle:
  //   register -> activate -> heartbeat -> poll/reply loop -> deactivate
  // Blocks until the process is interrupted (SIGINT) or the server instructs
  // a shutdown.
  void run();

  // Gracefully stop the client (called from signal handlers).
  void stop();

 private:
  std::string server_address_;
  std::string client_id_;
  Trainer* trainer_;
  volatile bool running_ = true;
};

}  // namespace flwr_local

#endif  // FLOWER_CLIENT_H
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
// heartbeat.h
//
// Background heartbeat thread. Sends SendNodeHeartbeat periodically while the
// node is active so the SuperLink does not mark the node as offline.
#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include "fleet_transport.h"

#include <atomic>
#include <thread>

namespace flwr_local {

class Heartbeat {
 public:
  // Start the heartbeat thread. The thread sends a heartbeat every
  // interval_seconds (which must be within the SuperLink's accepted range,
  // [10, 1800] seconds).
  Heartbeat(FleetTransport* transport, uint64_t node_id,
            double interval_seconds);

  // Stop the heartbeat thread and join it.
  void stop();

 private:
  void run();

  FleetTransport* transport_;
  uint64_t node_id_;
  double interval_seconds_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace flwr_local

#endif  // HEARTBEAT_H
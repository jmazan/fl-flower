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
// heartbeat.cc
#include "heartbeat.h"

#include <chrono>
#include <iostream>

namespace flwr_local {

Heartbeat::Heartbeat(FleetTransport* transport, uint64_t node_id,
                     double interval_seconds)
    : transport_(transport), node_id_(node_id),
      interval_seconds_(interval_seconds) {
  running_.store(true);
  thread_ = std::thread(&Heartbeat::run, this);
}

void Heartbeat::stop() {
  running_.store(false);
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Heartbeat::run() {
  while (running_.load()) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            static_cast<int64_t>(interval_seconds_ * 1000.0)));

    if (!running_.load()) {
      break;
    }

    if (!transport_->send_node_heartbeat(node_id_, interval_seconds_)) {
      std::cerr << "Heartbeat failed" << std::endl;
      // Phase 4 will add reconnect/backoff handling here.
    }
  }
}

}  // namespace flwr_local
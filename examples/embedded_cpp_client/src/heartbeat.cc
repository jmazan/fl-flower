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
  thread_ = std::thread(&Heartbeat::run, this);
}

void Heartbeat::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_requested_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Heartbeat::run() {
  const std::chrono::duration<double> interval(interval_seconds_);
  std::unique_lock<std::mutex> lock(mutex_);
  while (!stop_requested_) {
    lock.unlock();
    if (!transport_->send_node_heartbeat(node_id_, interval_seconds_)) {
      std::cerr << "Heartbeat failed" << std::endl;
    }
    lock.lock();
    cv_.wait_for(lock, interval, [this] { return stop_requested_; });
  }
}

}  // namespace flwr_local
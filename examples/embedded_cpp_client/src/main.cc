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
// main.cc
//
// Native C++ Flower Fleet client entry point.
//
// Usage:
//   ./flwr_client CLIENT_ID SERVER_URL
// Example:
//   ./flwr_client 0 127.0.0.1:9092
#include "flower_client.h"
#include "trainer.h"

#include <iostream>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cout << "Client takes 2 mandatory arguments as follows: " << std::endl;
    std::cout << "./flwr_client  CLIENT_ID  SERVER_URL" << std::endl;
    std::cout << "Example: ./flwr_client 0 '127.0.0.1:9092'" << std::endl;
    return 0;
  }

  const std::string client_id = argv[1];
  const std::string server_url = argv[2];

  // Populate local datasets.
  std::vector<double> ms{3.5, 9.3};  // b + m_0*x0 + m_1*x1
  double b = 1.7;
  std::cout << "Training set:" << std::endl;
  flwr_local::SyntheticDataset local_training_data(ms, b, 1000);
  std::cout << std::endl;

  std::cout << "Validation set:" << std::endl;
  flwr_local::SyntheticDataset local_validation_data(ms, b, 100);
  std::cout << std::endl;

  std::cout << "Test set:" << std::endl;
  flwr_local::SyntheticDataset local_test_data(ms, b, 500);
  std::cout << std::endl;

  // Define the trainer (linear model with SGD).
  flwr_local::LinearModelTrainer trainer(500, 0.01, static_cast<int>(ms.size()),
                                         local_training_data,
                                         local_validation_data,
                                         local_test_data);

  // Start the Flower client.
  flwr_local::FlowerClient client(server_url, client_id, &trainer);
  client.run();

  return 0;
}
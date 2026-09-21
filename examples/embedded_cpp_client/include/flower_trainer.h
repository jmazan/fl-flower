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
// flower_trainer.h
//
// Trainer interface. The trainer is deliberately independent from the Flower
// protobuf classes: it only deals with ordinary C++ parameters, configuration,
// metrics, and results. This keeps the ML library decoupled from the Flower
// transport and makes it easy to replace the stub trainer later.
#ifndef FLOWER_TRAINER_H
#define FLOWER_TRAINER_H

#include <map>
#include <string>
#include <vector>

namespace flwr_local {

// A single tensor of raw bytes. For the linear model these are little-endian
// IEEE-754 doubles (see parameter_codec.h for the wire format).
struct Tensor {
  std::string data;  // raw bytes
};

// Model parameters: an ordered list of tensors plus a serialization type tag.
struct Parameters {
  std::vector<Tensor> tensors;
  std::string tensor_type;
};

// Configuration values passed from the server (fit/evaluate config).
// Values are stored as strings and parsed on demand; the legacy strategy only
// sends scalar config values.
struct Config {
  std::map<std::string, std::string> values;
};

// Result of a fit call.
struct FitResult {
  Parameters parameters;
  int num_examples;
  std::map<std::string, std::string> metrics;
};

// Result of an evaluate call.
struct EvaluateResult {
  double loss;
  int num_examples;
  std::map<std::string, std::string> metrics;
};

class Trainer {
 public:
  // Return the current local model parameters.
  virtual Parameters get_parameters() = 0;

  // Train on the provided parameters and return the updated parameters.
  virtual FitResult fit(const Parameters& parameters, const Config& config) = 0;

  // Evaluate the provided parameters and return loss and metrics.
  virtual EvaluateResult evaluate(const Parameters& parameters,
                                  const Config& config) = 0;

  virtual ~Trainer() = default;
};

}  // namespace flwr_local

#endif  // FLOWER_TRAINER_H
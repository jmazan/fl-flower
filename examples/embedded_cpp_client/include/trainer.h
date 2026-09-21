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
// trainer.h
//
// Trainer implementation for the linear model. This is the stub trainer that
// Phase 3 will replace with the third-party NanoPI ML library. It implements
// the Trainer interface from flower_trainer.h.
#ifndef TRAINER_H
#define TRAINER_H

#include "flower_trainer.h"

#include <cstddef>
#include <random>
#include <vector>

namespace flwr_local {

// A simple synthetic dataset: each data point is a vector of features with the
// label stored in the last position.
class SyntheticDataset {
 public:
  SyntheticDataset(std::vector<double> ms, double b, size_t size);

  size_t size() const;
  std::vector<std::vector<double>> get_data_points() const;
  int get_features_count() const;

 private:
  std::vector<double> ms;
  double b;
  std::vector<std::vector<double>> data_points;
};

// Linear model trained with SGD on a synthetic dataset.
class LinearModelTrainer : public Trainer {
 public:
  LinearModelTrainer(int num_iterations, double learning_rate, int num_params,
                     SyntheticDataset& training_dataset,
                     SyntheticDataset& validation_dataset,
                     SyntheticDataset& test_dataset);

  Parameters get_parameters() override;
  FitResult fit(const Parameters& parameters, const Config& config) override;
  EvaluateResult evaluate(const Parameters& parameters,
                          const Config& config) override;

  // Model accessors (used by the parameter codec).
  std::vector<double> get_pred_weights() const;
  void set_pred_weights(const std::vector<double>& weights);
  double get_bias() const;
  void set_bias(double bias);

 private:
  int num_iterations;
  int batch_size;
  double learning_rate;

  std::vector<double> pred_weights;
  double pred_b;

  SyntheticDataset& training_dataset;
  SyntheticDataset& validation_dataset;
  SyntheticDataset& test_dataset;

  std::vector<double> predict(const std::vector<std::vector<double>>& X) const;
  double compute_mse(const std::vector<double>& true_y,
                     const std::vector<double>& pred) const;
};

}  // namespace flwr_local

#endif  // TRAINER_H
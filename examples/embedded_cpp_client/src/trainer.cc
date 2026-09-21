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
// trainer.cc
//
// Linear model trainer implementation. Adapted from the legacy quickstart-cpp
// example. The `data_indices` initialization bug from the original
// (initializing with dataset.size() zeroes and then appending) is fixed here.
#include "trainer.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <numeric>
#include <tuple>

namespace flwr_local {

// ---------------------------------------------------------------------------
// SyntheticDataset
// ---------------------------------------------------------------------------

SyntheticDataset::SyntheticDataset(std::vector<double> ms, double b,
                                   size_t size)
    : ms(ms), b(b) {
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<> distr(-10.0, 10.0);
  std::cout << "True parameters: " << std::endl;
  for (size_t i = 0; i < ms.size(); i++) {
    std::cout << std::fixed << "  m" << i << " = " << ms[i] << std::endl;
  }
  std::cout << "  b = " << std::fixed << b << std::endl;

  std::vector<std::vector<double>> xs(size, std::vector<double>(ms.size()));
  std::vector<double> ys(size, 0);
  for (size_t m_ind = 0; m_ind < ms.size(); m_ind++) {
    std::uniform_real_distribution<double> distx(-10.0, 10.0);
    for (size_t i = 0; i < size; i++) {
      xs[i][m_ind] = distx(mt);
    }
  }

  for (size_t i = 0; i < size; i++) {
    ys[i] = b;
    for (size_t m_ind = 0; m_ind < ms.size(); m_ind++) {
      ys[i] += ms[m_ind] * xs[i][m_ind];
    }
  }

  for (size_t i = 0; i < size; i++) {
    std::vector<double> data_point;
    data_point.insert(data_point.end(), xs[i].begin(), xs[i].end());
    data_point.push_back(ys[i]);
    data_points.push_back(data_point);
  }
}

size_t SyntheticDataset::size() const { return data_points.size(); }

int SyntheticDataset::get_features_count() const {
  return static_cast<int>(data_points[0].size()) - 1;
}

std::vector<std::vector<double>> SyntheticDataset::get_data_points() const {
  return data_points;
}

// ---------------------------------------------------------------------------
// LinearModelTrainer
// ---------------------------------------------------------------------------

LinearModelTrainer::LinearModelTrainer(int num_iterations,
                                       double learning_rate, int num_params,
                                       SyntheticDataset& training_dataset,
                                       SyntheticDataset& validation_dataset,
                                       SyntheticDataset& test_dataset)
    : num_iterations(num_iterations), learning_rate(learning_rate),
      training_dataset(training_dataset),
      validation_dataset(validation_dataset), test_dataset(test_dataset) {
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<> distr(-10.0, 10.0);
  for (int i = 0; i < num_params; i++) {
    pred_weights.push_back(distr(mt));
  }
  pred_b = 0.0;
  batch_size = 64;
}

std::vector<double> LinearModelTrainer::get_pred_weights() const {
  return pred_weights;
}

void LinearModelTrainer::set_pred_weights(const std::vector<double>& weights) {
  pred_weights.assign(weights.begin(), weights.end());
}

double LinearModelTrainer::get_bias() const { return pred_b; }

void LinearModelTrainer::set_bias(double bias) { pred_b = bias; }

std::vector<double> LinearModelTrainer::predict(
    const std::vector<std::vector<double>>& X) const {
  std::vector<double> prediction(X.size(), 0.0);
  for (size_t i = 0; i < X.size(); i++) {
    for (size_t j = 0; j < X[i].size(); j++) {
      prediction[i] += pred_weights[j] * X[i][j];
    }
    prediction[i] += pred_b;
  }
  return prediction;
}

double LinearModelTrainer::compute_mse(const std::vector<double>& true_y,
                                       const std::vector<double>& pred) const {
  double error = 0.0;
  for (size_t i = 0; i < true_y.size(); i++) {
    error += (pred[i] - true_y[i]) * (pred[i] - true_y[i]);
  }
  return error / static_cast<double>(true_y.size());
}

Parameters LinearModelTrainer::get_parameters() {
  // The parameter codec (parameter_codec.cc) converts the raw double buffers
  // into the Parameters struct. Here we only expose the model state.
  Parameters params;
  params.tensor_type = "cpp_double";

  Tensor weights_tensor;
  weights_tensor.data.assign(
      reinterpret_cast<const char*>(pred_weights.data()),
      reinterpret_cast<const char*>(pred_weights.data()) +
          pred_weights.size() * sizeof(double));
  params.tensors.push_back(weights_tensor);

  Tensor bias_tensor;
  bias_tensor.data.assign(reinterpret_cast<const char*>(&pred_b),
                          reinterpret_cast<const char*>(&pred_b) +
                              sizeof(double));
  params.tensors.push_back(bias_tensor);

  return params;
}

FitResult LinearModelTrainer::fit(const Parameters& parameters,
                                  const Config& config) {
  std::cout << "Fitting..." << std::endl;

  // Apply the parameters received from the server.
  if (parameters.tensors.size() >= 2) {
    const std::string& weights_bytes = parameters.tensors[0].data;
    const std::string& bias_bytes = parameters.tensors[1].data;

    if (weights_bytes.size() % sizeof(double) == 0 &&
        bias_bytes.size() == sizeof(double)) {
      std::vector<double> weights(weights_bytes.size() / sizeof(double));
      memcpy(weights.data(), weights_bytes.data(), weights_bytes.size());
      set_pred_weights(weights);

      double bias;
      memcpy(&bias, bias_bytes.data(), sizeof(double));
      set_bias(bias);
    } else {
      std::cerr << "Malformed parameters payload, keeping local model"
                << std::endl;
    }
  } else {
    std::cerr << "Expected at least 2 tensors, got "
              << parameters.tensors.size() << std::endl;
  }

  // Train.
  int features = training_dataset.get_features_count();
  std::vector<std::vector<double>> data_points =
      training_dataset.get_data_points();

  // Fixed: previously this initialized data_indices with dataset.size() zeroes
  // and then appended the real indices, producing an incorrect index array.
  std::vector<size_t> data_indices(training_dataset.size());
  for (size_t i = 0; i < training_dataset.size(); i++) {
    data_indices[i] = i;
  }

  std::vector<double> dW(features);
  std::vector<double> err(batch_size, 10000);
  std::vector<double> pW(features);
  double training_error = 0.0;

  for (int iteration = 0; iteration < num_iterations; iteration++) {
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(data_indices.begin(), data_indices.end(), g);

    std::vector<std::vector<double>> X(batch_size,
                                       std::vector<double>(features));
    std::vector<double> y(batch_size);

    for (int i = 0; i < batch_size; i++) {
      std::vector<double> point = data_points[data_indices[i]];
      y[i] = point.back();
      point.pop_back();
      X[i] = point;
    }

    pW = pred_weights;
    double pB = pred_b;
    double dB;

    std::vector<double> pred = predict(X);

    for (int i = 0; i < batch_size; i++) {
      err[i] = y[i] - pred[i];
    }

    // dW = X^T * err
    for (int j = 0; j < features; j++) {
      dW[j] = 0.0;
      for (int i = 0; i < batch_size; i++) {
        dW[j] += X[i][j] * err[i];
      }
      dW[j] *= (-2.0 / batch_size);
    }

    dB = (-2.0 / batch_size) *
         std::accumulate(err.begin(), err.end(), 0.0);

    for (int j = 0; j < features; j++) {
      pred_weights[j] = pW[j] - learning_rate * dW[j];
    }
    pred_b = pB - learning_rate * dB;

    if (iteration % 250 == 0) {
      training_error = compute_mse(y, predict(X));
      std::cout << "Iteration: " << iteration
                << "  Training error: " << training_error << '\n';
    }
  }

  std::cout << "Local model:" << std::endl;
  for (size_t i = 0; i < pred_weights.size(); i++) {
    std::cout << "  m" << i << "_local = " << std::fixed << pred_weights[i]
              << std::endl;
  }
  std::cout << "  b_local = " << std::fixed << pred_b << std::endl << std::endl;

  FitResult result;
  result.parameters = get_parameters();
  result.num_examples = static_cast<int>(training_dataset.size());
  result.metrics["loss"] = std::to_string(training_error);
  return result;
}

EvaluateResult LinearModelTrainer::evaluate(const Parameters& parameters,
                                            const Config& config) {
  std::cout << "Evaluating..." << std::endl;

  // Apply the parameters received from the server.
  if (parameters.tensors.size() >= 2) {
    const std::string& weights_bytes = parameters.tensors[0].data;
    const std::string& bias_bytes = parameters.tensors[1].data;

    if (weights_bytes.size() % sizeof(double) == 0 &&
        bias_bytes.size() == sizeof(double)) {
      std::vector<double> weights(weights_bytes.size() / sizeof(double));
      memcpy(weights.data(), weights_bytes.data(), weights_bytes.size());
      set_pred_weights(weights);

      double bias;
      memcpy(&bias, bias_bytes.data(), sizeof(double));
      set_bias(bias);
    } else {
      std::cerr << "Malformed parameters payload, keeping local model"
                << std::endl;
    }
  } else {
    std::cerr << "Expected at least 2 tensors, got "
              << parameters.tensors.size() << std::endl;
  }

  std::vector<std::vector<double>> data_points = test_dataset.get_data_points();
  int num_features = test_dataset.get_features_count();
  std::vector<std::vector<double>> X(test_dataset.size(),
                                     std::vector<double>(num_features));
  std::vector<double> y(test_dataset.size());

  for (size_t i = 0; i < test_dataset.size(); i++) {
    std::vector<double> point = data_points[i];
    y[i] = point.back();
    point.pop_back();
    X[i] = point;
  }

  double test_loss = compute_mse(y, predict(X));

  EvaluateResult result;
  result.loss = test_loss;
  result.num_examples = static_cast<int>(test_dataset.size());
  result.metrics["loss"] = std::to_string(test_loss);
  return result;
}

}  // namespace flwr_local
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
// parameter_codec.cc
#include "parameter_codec.h"

#include <cstdint>

namespace flwr_local {

namespace {

constexpr const char* STYPE_CPP_DOUBLE = "cpp_double";
constexpr const char* DTYPE_FLOAT64 = "float64";

}  // namespace

flwr::proto::ArrayRecord parameters_to_arrayrecord(const Parameters& params) {
  flwr::proto::ArrayRecord record;

  int idx = 0;
  for (const Tensor& tensor : params.tensors) {
    flwr::proto::ArrayRecord::Item* item = record.add_items();
    item->set_key(std::to_string(idx++));

    flwr::proto::Array* array = item->mutable_value();
    array->set_stype(STYPE_CPP_DOUBLE);
    array->set_dtype(DTYPE_FLOAT64);

    // Shape: number of doubles in the tensor.
    const size_t num_doubles = tensor.data.size() / sizeof(double);
    array->add_shape(static_cast<int32_t>(num_doubles));

    // Data: raw little-endian doubles.
    array->set_data(tensor.data);
  }

  return record;
}

bool arrayrecord_to_parameters(const flwr::proto::ArrayRecord& record,
                               Parameters* params) {
  params->tensors.clear();
  params->tensor_type = STYPE_CPP_DOUBLE;

  for (int i = 0; i < record.items_size(); i++) {
    const flwr::proto::ArrayRecord::Item& item = record.items(i);
    if (!item.has_value()) {
      return false;
    }
    const flwr::proto::Array& array = item.value();

    // Validate the payload before handing it to the trainer.
    if (array.stype() != STYPE_CPP_DOUBLE) {
      return false;
    }
    if (array.data().size() % sizeof(double) != 0) {
      return false;
    }

    Tensor tensor;
    tensor.data = array.data();
    params->tensors.push_back(tensor);
  }

  return true;
}

}  // namespace flwr_local
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
// parameter_codec.h
//
// Serialization of model parameters (Profile A: raw `cpp_double` arrays).
//
// Wire format (documented, matches the legacy Python strategy in
// examples/quickstart-cpp/fedavg_cpp.py):
//   - Each tensor is stored as an `Array` with:
//       stype = "cpp_double"
//       dtype = "float64"
//       shape = [n]  (number of doubles)
//       data  = n doubles in little-endian IEEE-754 byte order
//   - The legacy Python strategy decodes the data with
//       struct.unpack("d", tensor_bytes[i:i+8])
//     which uses native byte order. On the NanoPI (little-endian ARM) this
//     matches little-endian doubles. If the server runs on a big-endian
//     machine, the byte order must be converted explicitly.
#ifndef PARAMETER_CODEC_H
#define PARAMETER_CODEC_H

#include "flower_trainer.h"

#include "flwr/proto/recorddict.pb.h"

namespace flwr_local {

// Serialize Parameters into an ArrayRecord proto (keys "0", "1", ...).
flwr::proto::ArrayRecord parameters_to_arrayrecord(const Parameters& params);

// Deserialize an ArrayRecord proto into Parameters.
// Returns false if the record is malformed (wrong tensor count, bad dtype,
// or data length not a multiple of sizeof(double)).
bool arrayrecord_to_parameters(const flwr::proto::ArrayRecord& record,
                               Parameters* params);

}  // namespace flwr_local

#endif  // PARAMETER_CODEC_H
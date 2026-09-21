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
// message_codec.h
//
// Conversion between Flower `Message`/`RecordDict` protos and the trainer's
// logical operations (get_parameters, fit, evaluate).
//
// Profile A record layout (matches the legacy Python compat layer in
// flwr/compat/common/recorddict_compat.py):
//   fitins.parameters        -> ArrayRecord (keys "0", "1", ...)
//   fitins.config            -> ConfigRecord
//   fitres.parameters        -> ArrayRecord
//   fitres.num_examples      -> MetricRecord {"num_examples": int}
//   fitres.metrics           -> ConfigRecord
//   fitres.status            -> ConfigRecord {"code": int, "message": str}
//   evaluateins.parameters   -> ArrayRecord
//   evaluateins.config       -> ConfigRecord
//   evaluateres.loss         -> MetricRecord {"loss": double}
//   evaluateres.num_examples -> MetricRecord {"num_examples": int}
//   evaluateres.metrics      -> ConfigRecord
//   evaluateres.status       -> ConfigRecord {"code": int, "message": str}
//   getparametersres.parameters -> ArrayRecord
//   getparametersres.status  -> ConfigRecord {"code": int, "message": str}
#ifndef MESSAGE_CODEC_H
#define MESSAGE_CODEC_H

#include "flower_trainer.h"

#include "flwr/proto/message.pb.h"

namespace flwr_local {

// Handle a single incoming Message by dispatching to the trainer and building
// the reply Message. Returns nullptr if the message type is unknown.
//
// The reply reuses the incoming message's run_id, group_id, and ttl, sets
// src_node_id to the local node, dst_node_id to the sender, and
// reply_to_message_id to the incoming message_id.
flwr::proto::Message* handle_message(const flwr::proto::Message& incoming,
                                     uint64_t local_node_id,
                                     Trainer* trainer);

}  // namespace flwr_local

#endif  // MESSAGE_CODEC_H
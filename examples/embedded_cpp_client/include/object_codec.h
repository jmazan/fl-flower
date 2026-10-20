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
// object_codec.h
//
// Flower object transfer. The SuperLink does not carry message content inline.
// Each object is stored under its ID, the SHA-256 of its bytes, and its bytes
// start with a head that lists its child object IDs. This module converts
// between inline `flwr::proto::Message` content (used by message_codec) and
// that object form, so messages can move through PullObject/PushObject.
#ifndef OBJECT_CODEC_H
#define OBJECT_CODEC_H

#include "flwr/proto/message.pb.h"

#include <map>
#include <set>
#include <string>

namespace flwr_local {

// Object bytes keyed by object ID. Each value is the full object:
// "<Type> <child IDs> <body length>", a NUL byte, then the body.
using ObjectMap = std::map<std::string, std::string>;

// An outgoing Message in object form.
struct DeflatedMessage {
  std::string root_id;           // Object ID of the Message object.
  flwr::proto::ObjectTree tree;  // Object tree rooted at root_id.
  ObjectMap objects;             // Every object in the tree, keyed by ID.
};

// Convert an inline Message with content into objects. The message's
// metadata.message_id is not used, since it is derived from the object ID.
void deflate_message(const flwr::proto::Message& message, DeflatedMessage* out);

// Rebuild an inline Message from the objects of its tree. The root object ID
// becomes metadata.message_id. Returns false and sets *error on failure.
bool inflate_message(const std::string& root_id, const ObjectMap& objects,
                     flwr::proto::Message* message, std::string* error);

// IDs of all objects in a tree, each listed once.
std::set<std::string> tree_object_ids(const flwr::proto::ObjectTree& tree);

}  // namespace flwr_local

#endif  // OBJECT_CODEC_H

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
// object_codec.cc
#include "object_codec.h"

#include <openssl/sha.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

namespace flwr_local {

namespace {

constexpr const char* TYPE_MESSAGE = "Message";
constexpr const char* TYPE_RECORD_DICT = "RecordDict";
constexpr const char* TYPE_ARRAY_RECORD = "ArrayRecord";
constexpr const char* TYPE_ARRAY = "Array";
constexpr const char* TYPE_ARRAY_CHUNK = "ArrayChunk";
constexpr const char* TYPE_METRIC_RECORD = "MetricRecord";
constexpr const char* TYPE_CONFIG_RECORD = "ConfigRecord";

// Same chunk size as the Python client.
constexpr size_t ARRAY_CHUNK_SIZE = 5 * 1024 * 1024;

constexpr char HEAD_VALUE_DIVIDER = ' ';
constexpr char HEAD_BODY_DIVIDER = '\0';

using Refs = std::vector<std::pair<std::string, std::string>>;

// ---------------------------------------------------------------------------
// Hashing and string helpers
// ---------------------------------------------------------------------------

std::string sha256_hex(const std::string& data) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         digest);
  static const char* HEX_DIGITS = "0123456789abcdef";
  std::string hex;
  hex.reserve(2 * SHA256_DIGEST_LENGTH);
  for (unsigned char byte : digest) {
    hex.push_back(HEX_DIGITS[byte >> 4]);
    hex.push_back(HEX_DIGITS[byte & 0x0f]);
  }
  return hex;
}

std::vector<std::string> split(const std::string& text, char delimiter) {
  std::vector<std::string> parts;
  if (text.empty()) {
    return parts;
  }
  size_t start = 0;
  while (true) {
    size_t end = text.find(delimiter, start);
    if (end == std::string::npos) {
      parts.push_back(text.substr(start));
      return parts;
    }
    parts.push_back(text.substr(start, end - start));
    start = end + 1;
  }
}

void append_utf8(uint32_t code_point, std::string* out) {
  if (code_point < 0x80) {
    out->push_back(static_cast<char>(code_point));
  } else if (code_point < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (code_point >> 6)));
    out->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else if (code_point < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (code_point >> 12)));
    out->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (code_point >> 18)));
    out->push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  }
}

// ---------------------------------------------------------------------------
// JSON writing. Only the shapes used by Flower objects are supported:
// strings and lists of integers inside a flat object.
// ---------------------------------------------------------------------------

void append_json_string(const std::string& value, std::string* out) {
  out->push_back('"');
  for (char c : value) {
    if (c == '"' || c == '\\') {
      out->push_back('\\');
      out->push_back(c);
    } else if (static_cast<unsigned char>(c) < 0x20) {
      char escaped[7];
      std::snprintf(escaped, sizeof(escaped), "\\u%04x",
                    static_cast<unsigned int>(static_cast<unsigned char>(c)));
      *out += escaped;
    } else {
      out->push_back(c);
    }
  }
  out->push_back('"');
}

void append_json_int_list(const std::vector<int>& values, std::string* out) {
  *out += "[";
  for (size_t i = 0; i < values.size(); i++) {
    if (i > 0) {
      *out += ", ";
    }
    *out += std::to_string(values[i]);
  }
  *out += "]";
}

std::string refs_json(const Refs& refs) {
  std::string out = "{";
  for (size_t i = 0; i < refs.size(); i++) {
    if (i > 0) {
      out += ", ";
    }
    append_json_string(refs[i].first, &out);
    out += ": ";
    append_json_string(refs[i].second, &out);
  }
  out += "}";
  return out;
}

// ---------------------------------------------------------------------------
// JSON reading
// ---------------------------------------------------------------------------

class JsonReader {
 public:
  explicit JsonReader(const std::string& text) : text_(text) {}

  bool consume(char expected) {
    skip_whitespace();
    if (pos_ >= text_.size() || text_[pos_] != expected) {
      return false;
    }
    pos_++;
    return true;
  }

  bool at_end() {
    skip_whitespace();
    return pos_ == text_.size();
  }

  bool read_string(std::string* value) {
    if (!consume('"')) {
      return false;
    }
    value->clear();
    while (pos_ < text_.size()) {
      char c = text_[pos_++];
      if (c == '"') {
        return true;
      }
      if (c != '\\') {
        value->push_back(c);
        continue;
      }
      if (pos_ >= text_.size()) {
        return false;
      }
      char escape = text_[pos_++];
      switch (escape) {
        case '"':
        case '\\':
        case '/':
          value->push_back(escape);
          break;
        case 'b':
          value->push_back('\b');
          break;
        case 'f':
          value->push_back('\f');
          break;
        case 'n':
          value->push_back('\n');
          break;
        case 'r':
          value->push_back('\r');
          break;
        case 't':
          value->push_back('\t');
          break;
        case 'u':
          if (!read_unicode_escape(value)) {
            return false;
          }
          break;
        default:
          return false;
      }
    }
    return false;
  }

  bool read_int_list(std::vector<int>* values) {
    values->clear();
    if (!consume('[')) {
      return false;
    }
    if (consume(']')) {
      return true;
    }
    while (true) {
      int value = 0;
      if (!read_int(&value)) {
        return false;
      }
      values->push_back(value);
      if (consume(']')) {
        return true;
      }
      if (!consume(',')) {
        return false;
      }
    }
  }

 private:
  void skip_whitespace() {
    while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                   text_[pos_] == '\n' || text_[pos_] == '\r')) {
      pos_++;
    }
  }

  // Reads the part after "\u". Surrogate pairs are combined into one code point.
  bool read_unicode_escape(std::string* value) {
    uint32_t code_point = 0;
    if (!read_hex4(&code_point)) {
      return false;
    }
    if (code_point >= 0xD800 && code_point <= 0xDBFF) {
      uint32_t low = 0;
      if (text_.compare(pos_, 2, "\\u") != 0) {
        return false;
      }
      pos_ += 2;
      if (!read_hex4(&low) || low < 0xDC00 || low > 0xDFFF) {
        return false;
      }
      code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
    }
    append_utf8(code_point, value);
    return true;
  }

  bool read_hex4(uint32_t* code_point) {
    if (text_.size() - pos_ < 4) {
      return false;
    }
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
      char c = text_[pos_++];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
    }
    *code_point = value;
    return true;
  }

  bool read_int(int* value) {
    skip_whitespace();
    bool negative = pos_ < text_.size() && text_[pos_] == '-';
    if (negative) {
      pos_++;
    }
    size_t digits_start = pos_;
    long long result = 0;
    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
      result = result * 10 + (text_[pos_] - '0');
      if (result > INT32_MAX) {
        return false;
      }
      pos_++;
    }
    if (pos_ == digits_start) {
      return false;
    }
    *value = static_cast<int>(negative ? -result : result);
    return true;
  }

  const std::string& text_;
  size_t pos_ = 0;
};

// Parses `{"key": <value>, ...}`. `read_value` receives each key and must
// consume the matching value from `reader`.
template <typename ReadValue>
bool parse_json_object(JsonReader* reader, ReadValue read_value) {
  if (!reader->consume('{')) {
    return false;
  }
  if (reader->consume('}')) {
    return true;
  }
  while (true) {
    std::string key;
    if (!reader->read_string(&key) || !reader->consume(':')) {
      return false;
    }
    if (!read_value(key)) {
      return false;
    }
    if (reader->consume('}')) {
      return true;
    }
    if (!reader->consume(',')) {
      return false;
    }
  }
}

// Parses a flat object of string values, keeping key order.
bool parse_refs(const std::string& body, Refs* refs) {
  JsonReader reader(body);
  refs->clear();
  bool ok = parse_json_object(&reader, [&](const std::string& key) {
    std::string value;
    if (!reader.read_string(&value)) {
      return false;
    }
    refs->emplace_back(key, value);
    return true;
  });
  return ok && reader.at_end();
}

struct ArrayMeta {
  std::string dtype;
  std::vector<int> shape;
  std::string stype;
  std::vector<int> chunk_indices;
};

bool parse_array_meta(const std::string& body, ArrayMeta* meta) {
  JsonReader reader(body);
  bool ok = parse_json_object(&reader, [&](const std::string& key) {
    if (key == "dtype") {
      return reader.read_string(&meta->dtype);
    }
    if (key == "stype") {
      return reader.read_string(&meta->stype);
    }
    if (key == "shape") {
      return reader.read_int_list(&meta->shape);
    }
    if (key == "arraychunk_ids") {
      return reader.read_int_list(&meta->chunk_indices);
    }
    return false;
  });
  return ok && reader.at_end();
}

// ---------------------------------------------------------------------------
// Deflation: inline proto -> objects
// ---------------------------------------------------------------------------

// Child trees with duplicate object IDs removed, keeping first-seen order.
std::vector<flwr::proto::ObjectTree> unique_children(
    const std::vector<flwr::proto::ObjectTree>& children) {
  std::vector<flwr::proto::ObjectTree> unique;
  std::set<std::string> seen;
  for (const flwr::proto::ObjectTree& child : children) {
    if (seen.insert(child.object_id()).second) {
      unique.push_back(child);
    }
  }
  return unique;
}

// Builds an object from its type, unique children, and body. The object is
// stored in `objects` and its tree node is returned.
flwr::proto::ObjectTree store_object(
    const char* type, const std::vector<flwr::proto::ObjectTree>& children,
    const std::string& body, ObjectMap* objects) {
  std::string child_ids;
  for (size_t i = 0; i < children.size(); i++) {
    if (i > 0) {
      child_ids.push_back(',');
    }
    child_ids += children[i].object_id();
  }

  std::string content = std::string(type) + HEAD_VALUE_DIVIDER + child_ids +
                        HEAD_VALUE_DIVIDER + std::to_string(body.size());
  content.push_back(HEAD_BODY_DIVIDER);
  content += body;

  flwr::proto::ObjectTree node;
  node.set_object_id(sha256_hex(content));
  for (const flwr::proto::ObjectTree& child : children) {
    *node.add_children() = child;
  }
  objects->emplace(node.object_id(), std::move(content));
  return node;
}

template <typename ProtoT>
std::string serialize_proto(const ProtoT& proto) {
  std::string bytes;
  proto.SerializeToString(&bytes);
  return bytes;
}

flwr::proto::ObjectTree deflate_array(const flwr::proto::Array& array,
                                      ObjectMap* objects) {
  // Chunks may repeat; the header lists each unique chunk once and
  // arraychunk_ids indexes into that unique list.
  std::vector<flwr::proto::ObjectTree> chunks;
  const std::string& data = array.data();
  for (size_t offset = 0; offset < data.size(); offset += ARRAY_CHUNK_SIZE) {
    size_t length = std::min(ARRAY_CHUNK_SIZE, data.size() - offset);
    chunks.push_back(store_object(TYPE_ARRAY_CHUNK, {},
                                  data.substr(offset, length), objects));
  }

  std::vector<flwr::proto::ObjectTree> unique;
  std::map<std::string, int> index_of;
  std::vector<int> chunk_indices;
  for (const flwr::proto::ObjectTree& chunk : chunks) {
    auto found = index_of.find(chunk.object_id());
    if (found == index_of.end()) {
      found = index_of
                  .emplace(chunk.object_id(), static_cast<int>(unique.size()))
                  .first;
      unique.push_back(chunk);
    }
    chunk_indices.push_back(found->second);
  }

  std::vector<int> shape(array.shape().begin(), array.shape().end());
  std::string body = "{\"dtype\": ";
  append_json_string(array.dtype(), &body);
  body += ", \"shape\": ";
  append_json_int_list(shape, &body);
  body += ", \"stype\": ";
  append_json_string(array.stype(), &body);
  body += ", \"arraychunk_ids\": ";
  append_json_int_list(chunk_indices, &body);
  body += "}";
  return store_object(TYPE_ARRAY, unique, body, objects);
}

flwr::proto::ObjectTree deflate_array_record(
    const flwr::proto::ArrayRecord& record, ObjectMap* objects) {
  std::vector<flwr::proto::ObjectTree> arrays;
  Refs refs;
  for (const flwr::proto::ArrayRecord::Item& item : record.items()) {
    arrays.push_back(deflate_array(item.value(), objects));
    refs.emplace_back(item.key(), arrays.back().object_id());
  }
  return store_object(TYPE_ARRAY_RECORD, unique_children(arrays),
                      refs_json(refs), objects);
}

flwr::proto::ObjectTree deflate_record_dict(const flwr::proto::RecordDict& dict,
                                            ObjectMap* objects) {
  std::vector<flwr::proto::ObjectTree> records;
  Refs refs;
  for (const flwr::proto::RecordDict::Item& item : dict.items()) {
    flwr::proto::ObjectTree record;
    if (item.has_array_record()) {
      record = deflate_array_record(item.array_record(), objects);
    } else if (item.has_metric_record()) {
      record = store_object(TYPE_METRIC_RECORD, {},
                            serialize_proto(item.metric_record()), objects);
    } else if (item.has_config_record()) {
      record = store_object(TYPE_CONFIG_RECORD, {},
                            serialize_proto(item.config_record()), objects);
    } else {
      continue;
    }
    refs.emplace_back(item.key(), record.object_id());
    records.push_back(record);
  }
  return store_object(TYPE_RECORD_DICT, unique_children(records),
                      refs_json(refs), objects);
}

void collect_tree_ids(const flwr::proto::ObjectTree& tree,
                      std::set<std::string>* ids) {
  ids->insert(tree.object_id());
  for (const flwr::proto::ObjectTree& child : tree.children()) {
    collect_tree_ids(child, ids);
  }
}

// ---------------------------------------------------------------------------
// Inflation: objects -> inline proto
// ---------------------------------------------------------------------------

struct ParsedObject {
  std::string type;
  std::vector<std::string> child_ids;
  std::string body;
};

// Looks up an object, checks that its bytes hash to its ID, and splits its
// header and body.
bool parse_object(const std::string& object_id, const ObjectMap& objects,
                  ParsedObject* parsed, std::string* error) {
  auto found = objects.find(object_id);
  if (found == objects.end()) {
    *error = "missing object " + object_id;
    return false;
  }
  const std::string& content = found->second;
  if (sha256_hex(content) != object_id) {
    *error = "hash mismatch for object " + object_id;
    return false;
  }

  size_t divider = content.find(HEAD_BODY_DIVIDER);
  if (divider == std::string::npos) {
    *error = "missing body divider in object " + object_id;
    return false;
  }
  std::vector<std::string> fields =
      split(content.substr(0, divider), HEAD_VALUE_DIVIDER);
  if (fields.size() != 3) {
    *error = "malformed header in object " + object_id;
    return false;
  }

  parsed->body = content.substr(divider + 1);
  if (fields[2] != std::to_string(parsed->body.size())) {
    *error = "body length mismatch in object " + object_id;
    return false;
  }
  parsed->type = fields[0];
  parsed->child_ids = split(fields[1], ',');
  return true;
}

bool inflate_array(const std::string& object_id, const ObjectMap& objects,
                   flwr::proto::Array* array, std::string* error) {
  ParsedObject parsed;
  if (!parse_object(object_id, objects, &parsed, error)) {
    return false;
  }
  if (parsed.type != TYPE_ARRAY) {
    *error = "expected Array object, got " + parsed.type;
    return false;
  }
  ArrayMeta meta;
  if (!parse_array_meta(parsed.body, &meta)) {
    *error = "malformed Array metadata";
    return false;
  }

  std::string data;
  for (int index : meta.chunk_indices) {
    if (index < 0 || static_cast<size_t>(index) >= parsed.child_ids.size()) {
      *error = "invalid chunk index in Array";
      return false;
    }
    ParsedObject chunk;
    if (!parse_object(parsed.child_ids[index], objects, &chunk, error)) {
      return false;
    }
    if (chunk.type != TYPE_ARRAY_CHUNK) {
      *error = "expected ArrayChunk object, got " + chunk.type;
      return false;
    }
    data += chunk.body;
  }

  array->set_dtype(meta.dtype);
  array->clear_shape();
  for (int dim : meta.shape) {
    array->add_shape(dim);
  }
  array->set_stype(meta.stype);
  array->set_data(std::move(data));
  return true;
}

bool inflate_array_record(const std::string& object_id,
                          const ObjectMap& objects,
                          flwr::proto::ArrayRecord* record, std::string* error) {
  ParsedObject parsed;
  if (!parse_object(object_id, objects, &parsed, error)) {
    return false;
  }
  if (parsed.type != TYPE_ARRAY_RECORD) {
    *error = "expected ArrayRecord object, got " + parsed.type;
    return false;
  }
  Refs refs;
  if (!parse_refs(parsed.body, &refs)) {
    *error = "malformed ArrayRecord body";
    return false;
  }
  for (const auto& ref : refs) {
    flwr::proto::ArrayRecord::Item* item = record->add_items();
    item->set_key(ref.first);
    if (!inflate_array(ref.second, objects, item->mutable_value(), error)) {
      return false;
    }
  }
  return true;
}

bool inflate_record_dict(const std::string& object_id,
                         const ObjectMap& objects,
                         flwr::proto::RecordDict* dict, std::string* error) {
  ParsedObject parsed;
  if (!parse_object(object_id, objects, &parsed, error)) {
    return false;
  }
  if (parsed.type != TYPE_RECORD_DICT) {
    *error = "expected RecordDict object, got " + parsed.type;
    return false;
  }
  Refs refs;
  if (!parse_refs(parsed.body, &refs)) {
    *error = "malformed RecordDict body";
    return false;
  }
  for (const auto& ref : refs) {
    ParsedObject record;
    if (!parse_object(ref.second, objects, &record, error)) {
      return false;
    }
    flwr::proto::RecordDict::Item* item = dict->add_items();
    item->set_key(ref.first);
    if (record.type == TYPE_ARRAY_RECORD) {
      if (!inflate_array_record(ref.second, objects,
                                item->mutable_array_record(), error)) {
        return false;
      }
    } else if (record.type == TYPE_METRIC_RECORD) {
      if (!item->mutable_metric_record()->ParseFromString(record.body)) {
        *error = "malformed MetricRecord";
        return false;
      }
    } else if (record.type == TYPE_CONFIG_RECORD) {
      if (!item->mutable_config_record()->ParseFromString(record.body)) {
        *error = "malformed ConfigRecord";
        return false;
      }
    } else {
      *error = "unexpected record type " + record.type;
      return false;
    }
  }
  return true;
}

}  // namespace

void deflate_message(const flwr::proto::Message& message, DeflatedMessage* out) {
  std::vector<flwr::proto::ObjectTree> children;
  if (message.has_content()) {
    children.push_back(deflate_record_dict(message.content(), &out->objects));
  }

  // The message ID is the object ID, so it is left out of the body.
  flwr::proto::Message body;
  body.mutable_metadata()->CopyFrom(message.metadata());
  body.mutable_metadata()->clear_message_id();
  if (message.has_error()) {
    body.mutable_error()->CopyFrom(message.error());
  }

  out->tree = store_object(TYPE_MESSAGE, children, serialize_proto(body),
                           &out->objects);
  out->root_id = out->tree.object_id();
}

bool inflate_message(const std::string& root_id, const ObjectMap& objects,
                     flwr::proto::Message* message, std::string* error) {
  ParsedObject parsed;
  if (!parse_object(root_id, objects, &parsed, error)) {
    return false;
  }
  if (parsed.type != TYPE_MESSAGE) {
    *error = "expected Message object, got " + parsed.type;
    return false;
  }
  if (parsed.child_ids.size() > 1) {
    *error = "Message has more than one child";
    return false;
  }

  flwr::proto::Message body;
  if (!body.ParseFromString(parsed.body)) {
    *error = "malformed Message body";
    return false;
  }
  *message = body;
  message->mutable_metadata()->set_message_id(root_id);
  if (!parsed.child_ids.empty() &&
      !inflate_record_dict(parsed.child_ids[0], objects,
                           message->mutable_content(), error)) {
    return false;
  }
  return true;
}

std::set<std::string> tree_object_ids(const flwr::proto::ObjectTree& tree) {
  std::set<std::string> ids;
  collect_tree_ids(tree, &ids);
  return ids;
}

}  // namespace flwr_local

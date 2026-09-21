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
// message_codec.cc
#include "message_codec.h"
#include "parameter_codec.h"

#include <cstdint>
#include <iostream>

namespace flwr_local {

namespace {

constexpr const char* MSG_TYPE_TRAIN = "train";
constexpr const char* MSG_TYPE_EVALUATE = "evaluate";
constexpr const char* MSG_TYPE_GET_PARAMETERS = "get_parameters";

constexpr const char* KEY_FIT_PARAMETERS = "fitins.parameters";
constexpr const char* KEY_FIT_CONFIG = "fitins.config";
constexpr const char* KEY_FITRES_PARAMETERS = "fitres.parameters";
constexpr const char* KEY_FITRES_NUM_EXAMPLES = "fitres.num_examples";
constexpr const char* KEY_FITRES_METRICS = "fitres.metrics";
constexpr const char* KEY_FITRES_STATUS = "fitres.status";

constexpr const char* KEY_EVAL_PARAMETERS = "evaluateins.parameters";
constexpr const char* KEY_EVAL_CONFIG = "evaluateins.config";
constexpr const char* KEY_EVALRES_LOSS = "evaluateres.loss";
constexpr const char* KEY_EVALRES_NUM_EXAMPLES = "evaluateres.num_examples";
constexpr const char* KEY_EVALRES_METRICS = "evaluateres.metrics";
constexpr const char* KEY_EVALRES_STATUS = "evaluateres.status";

constexpr const char* KEY_GETPARAMS_PARAMETERS = "getparametersres.parameters";
constexpr const char* KEY_GETPARAMS_STATUS = "getparametersres.status";

constexpr const char* KEY_STATUS_CODE = "code";
constexpr const char* KEY_STATUS_MESSAGE = "message";

// ---------------------------------------------------------------------------
// RecordDict helpers
// ---------------------------------------------------------------------------

// Find an ArrayRecord by key. Returns nullptr if absent.
const flwr::proto::ArrayRecord* find_array_record(
    const flwr::proto::RecordDict& recorddict, const std::string& key) {
  for (int i = 0; i < recorddict.items_size(); i++) {
    const flwr::proto::RecordDict::Item& item = recorddict.items(i);
    if (item.key() == key && item.has_array_record()) {
      return &item.array_record();
    }
  }
  return nullptr;
}

// Find a ConfigRecord by key. Returns nullptr if absent.
const flwr::proto::ConfigRecord* find_config_record(
    const flwr::proto::RecordDict& recorddict, const std::string& key) {
  for (int i = 0; i < recorddict.items_size(); i++) {
    const flwr::proto::RecordDict::Item& item = recorddict.items(i);
    if (item.key() == key && item.has_config_record()) {
      return &item.config_record();
    }
  }
  return nullptr;
}

// Find a MetricRecord by key. Returns nullptr if absent.
const flwr::proto::MetricRecord* find_metric_record(
    const flwr::proto::RecordDict& recorddict, const std::string& key) {
  for (int i = 0; i < recorddict.items_size(); i++) {
    const flwr::proto::RecordDict::Item& item = recorddict.items(i);
    if (item.key() == key && item.has_metric_record()) {
      return &item.metric_record();
    }
  }
  return nullptr;
}

// Read a string value from a ConfigRecord by key. Returns false if absent.
bool config_record_get_string(const flwr::proto::ConfigRecord& record,
                              const std::string& key, std::string* out) {
  for (int i = 0; i < record.items_size(); i++) {
    const flwr::proto::ConfigRecord::Item& item = record.items(i);
    if (item.key() == key && item.has_value() &&
        item.value().has_string()) {
      *out = item.value().string();
      return true;
    }
  }
  return false;
}

// Read a sint64 value from a ConfigRecord by key. Returns false if absent.
bool config_record_get_sint64(const flwr::proto::ConfigRecord& record,
                              const std::string& key, int64_t* out) {
  for (int i = 0; i < record.items_size(); i++) {
    const flwr::proto::ConfigRecord::Item& item = record.items(i);
    if (item.key() == key && item.has_value() &&
        item.value().has_sint64()) {
      *out = item.value().sint64();
      return true;
    }
  }
  return false;
}

// Read a double value from a MetricRecord by key. Returns false if absent.
bool metric_record_get_double(const flwr::proto::MetricRecord& record,
                              const std::string& key, double* out) {
  for (int i = 0; i < record.items_size(); i++) {
    const flwr::proto::MetricRecord::Item& item = record.items(i);
    if (item.key() == key && item.has_value() &&
        item.value().has_double_()) {
      *out = item.value().double_();
      return true;
    }
  }
  return false;
}

// Read a sint64 value from a MetricRecord by key. Returns false if absent.
bool metric_record_get_sint64(const flwr::proto::MetricRecord& record,
                              const std::string& key, int64_t* out) {
  for (int i = 0; i < record.items_size(); i++) {
    const flwr::proto::MetricRecord::Item& item = record.items(i);
    if (item.key() == key && item.has_value() &&
        item.value().has_sint64()) {
      *out = item.value().sint64();
      return true;
    }
  }
  return false;
}

// Add a ConfigRecord with a single sint64 value.
void add_config_sint64(flwr::proto::RecordDict* recorddict,
                       const std::string& key, int64_t value) {
  flwr::proto::RecordDict::Item* item = recorddict->add_items();
  item->set_key(key);
  flwr::proto::ConfigRecord::Item* cfg_item =
      item->mutable_config_record()->add_items();
  cfg_item->set_key(KEY_STATUS_CODE);
  cfg_item->mutable_value()->set_sint64(value);
}

// Add a ConfigRecord with a single string value.
void add_config_string(flwr::proto::RecordDict* recorddict,
                       const std::string& key, const std::string& value) {
  flwr::proto::RecordDict::Item* item = recorddict->add_items();
  item->set_key(key);
  flwr::proto::ConfigRecord::Item* cfg_item =
      item->mutable_config_record()->add_items();
  cfg_item->set_key(KEY_STATUS_MESSAGE);
  cfg_item->mutable_value()->set_string(value);
}

// Add a MetricRecord with a single sint64 value.
void add_metric_sint64(flwr::proto::RecordDict* recorddict,
                       const std::string& key, const std::string& metric_key,
                       int64_t value) {
  flwr::proto::RecordDict::Item* item = recorddict->add_items();
  item->set_key(key);
  flwr::proto::MetricRecord::Item* metric_item =
      item->mutable_metric_record()->add_items();
  metric_item->set_key(metric_key);
  metric_item->mutable_value()->set_sint64(value);
}

// Add a MetricRecord with a single double value.
void add_metric_double(flwr::proto::RecordDict* recorddict,
                       const std::string& key, const std::string& metric_key,
                       double value) {
  flwr::proto::RecordDict::Item* item = recorddict->add_items();
  item->set_key(key);
  flwr::proto::MetricRecord::Item* metric_item =
      item->mutable_metric_record()->add_items();
  metric_item->set_key(metric_key);
  metric_item->mutable_value()->set_double_(value);
}

// Add an ArrayRecord under the given key.
void add_array_record(flwr::proto::RecordDict* recorddict,
                      const std::string& key,
                      const flwr::proto::ArrayRecord& array_record) {
  flwr::proto::RecordDict::Item* item = recorddict->add_items();
  item->set_key(key);
  *item->mutable_array_record() = array_record;
}

// Add a ConfigRecord with a single string value under the given key.
void add_config_string_value(flwr::proto::RecordDict* recorddict,
                             const std::string& key,
                             const std::string& cfg_key,
                             const std::string& value) {
  flwr::proto::RecordDict::Item* item = recorddict->add_items();
  item->set_key(key);
  flwr::proto::ConfigRecord::Item* cfg_item =
      item->mutable_config_record()->add_items();
  cfg_item->set_key(cfg_key);
  cfg_item->mutable_value()->set_string(value);
}

// ---------------------------------------------------------------------------
// Config decoding
// ---------------------------------------------------------------------------

// Decode a ConfigRecord into the trainer's Config (string values only).
Config config_from_record(const flwr::proto::ConfigRecord& record) {
  Config config;
  for (int i = 0; i < record.items_size(); i++) {
    const flwr::proto::ConfigRecord::Item& item = record.items(i);
    if (!item.has_value()) {
      continue;
    }
    const flwr::proto::ConfigRecordValue& value = item.value();
    if (value.has_string()) {
      config.values[item.key()] = value.string();
    } else if (value.has_sint64()) {
      config.values[item.key()] = std::to_string(value.sint64());
    } else if (value.has_double_()) {
      config.values[item.key()] = std::to_string(value.double_());
    } else if (value.has_bool_()) {
      config.values[item.key()] = value.bool_() ? "true" : "false";
    }
  }
  return config;
}

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

// get_parameters: reply with getparametersres.parameters + status.
flwr::proto::Message* handle_get_parameters(const flwr::proto::Message& incoming,
                                            uint64_t local_node_id,
                                            Trainer* trainer) {
  Parameters params = trainer->get_parameters();
  flwr::proto::ArrayRecord array_record = parameters_to_arrayrecord(params);

  flwr::proto::RecordDict content;
  add_array_record(&content, KEY_GETPARAMS_PARAMETERS, array_record);
  add_config_sint64(&content, KEY_GETPARAMS_STATUS, 0);  // Code.OK
  add_config_string(&content, KEY_GETPARAMS_STATUS, "Success");

  flwr::proto::Message reply;
  reply.mutable_metadata()->set_run_id(incoming.metadata().run_id());
  reply.mutable_metadata()->set_src_node_id(local_node_id);
  reply.mutable_metadata()->set_dst_node_id(incoming.metadata().src_node_id());
  reply.mutable_metadata()->set_reply_to_message_id(
      incoming.metadata().message_id());
  reply.mutable_metadata()->set_group_id(incoming.metadata().group_id());
  reply.mutable_metadata()->set_ttl(incoming.metadata().ttl());
  reply.mutable_metadata()->set_message_type(incoming.metadata().message_type());
  *reply.mutable_content() = content;

  return new flwr::proto::Message(reply);
}

// train: decode fitins, call trainer->fit, reply with fitres.
flwr::proto::Message* handle_train(const flwr::proto::Message& incoming,
                                   uint64_t local_node_id, Trainer* trainer) {
  const flwr::proto::RecordDict& content = incoming.content();

  const flwr::proto::ArrayRecord* array_record =
      find_array_record(content, KEY_FIT_PARAMETERS);
  if (array_record == nullptr) {
    std::cerr << "train: missing " << KEY_FIT_PARAMETERS << std::endl;
    return nullptr;
  }

  Parameters params;
  if (!arrayrecord_to_parameters(*array_record, &params)) {
    std::cerr << "train: malformed parameters" << std::endl;
    return nullptr;
  }

  const flwr::proto::ConfigRecord* config_record =
      find_config_record(content, KEY_FIT_CONFIG);
  Config config;
  if (config_record != nullptr) {
    config = config_from_record(*config_record);
  }

  FitResult result = trainer->fit(params, config);

  flwr::proto::RecordDict reply_content;
  add_array_record(&reply_content, KEY_FITRES_PARAMETERS,
                   parameters_to_arrayrecord(result.parameters));
  add_metric_sint64(&reply_content, KEY_FITRES_NUM_EXAMPLES, "num_examples",
                    result.num_examples);
  add_config_sint64(&reply_content, KEY_FITRES_STATUS, 0);  // Code.OK
  add_config_string(&reply_content, KEY_FITRES_STATUS, "Success");
  for (const auto& [key, value] : result.metrics) {
    add_config_string_value(&reply_content, KEY_FITRES_METRICS, key, value);
  }

  flwr::proto::Message reply;
  reply.mutable_metadata()->set_run_id(incoming.metadata().run_id());
  reply.mutable_metadata()->set_src_node_id(local_node_id);
  reply.mutable_metadata()->set_dst_node_id(incoming.metadata().src_node_id());
  reply.mutable_metadata()->set_reply_to_message_id(
      incoming.metadata().message_id());
  reply.mutable_metadata()->set_group_id(incoming.metadata().group_id());
  reply.mutable_metadata()->set_ttl(incoming.metadata().ttl());
  reply.mutable_metadata()->set_message_type(incoming.metadata().message_type());
  *reply.mutable_content() = reply_content;

  return new flwr::proto::Message(reply);
}

// evaluate: decode evaluateins, call trainer->evaluate, reply with evaluateres.
flwr::proto::Message* handle_evaluate(const flwr::proto::Message& incoming,
                                      uint64_t local_node_id,
                                      Trainer* trainer) {
  const flwr::proto::RecordDict& content = incoming.content();

  const flwr::proto::ArrayRecord* array_record =
      find_array_record(content, KEY_EVAL_PARAMETERS);
  if (array_record == nullptr) {
    std::cerr << "evaluate: missing " << KEY_EVAL_PARAMETERS << std::endl;
    return nullptr;
  }

  Parameters params;
  if (!arrayrecord_to_parameters(*array_record, &params)) {
    std::cerr << "evaluate: malformed parameters" << std::endl;
    return nullptr;
  }

  const flwr::proto::ConfigRecord* config_record =
      find_config_record(content, KEY_EVAL_CONFIG);
  Config config;
  if (config_record != nullptr) {
    config = config_from_record(*config_record);
  }

  EvaluateResult result = trainer->evaluate(params, config);

  flwr::proto::RecordDict reply_content;
  add_metric_double(&reply_content, KEY_EVALRES_LOSS, "loss", result.loss);
  add_metric_sint64(&reply_content, KEY_EVALRES_NUM_EXAMPLES, "num_examples",
                    result.num_examples);
  add_config_sint64(&reply_content, KEY_EVALRES_STATUS, 0);  // Code.OK
  add_config_string(&reply_content, KEY_EVALRES_STATUS, "Success");
  for (const auto& [key, value] : result.metrics) {
    add_config_string_value(&reply_content, KEY_EVALRES_METRICS, key, value);
  }

  flwr::proto::Message reply;
  reply.mutable_metadata()->set_run_id(incoming.metadata().run_id());
  reply.mutable_metadata()->set_src_node_id(local_node_id);
  reply.mutable_metadata()->set_dst_node_id(incoming.metadata().src_node_id());
  reply.mutable_metadata()->set_reply_to_message_id(
      incoming.metadata().message_id());
  reply.mutable_metadata()->set_group_id(incoming.metadata().group_id());
  reply.mutable_metadata()->set_ttl(incoming.metadata().ttl());
  reply.mutable_metadata()->set_message_type(incoming.metadata().message_type());
  *reply.mutable_content() = reply_content;

  return new flwr::proto::Message(reply);
}

}  // namespace

flwr::proto::Message* handle_message(const flwr::proto::Message& incoming,
                                     uint64_t local_node_id,
                                     Trainer* trainer) {
  const std::string& message_type = incoming.metadata().message_type();

  if (message_type == MSG_TYPE_TRAIN) {
    return handle_train(incoming, local_node_id, trainer);
  }
  if (message_type == MSG_TYPE_EVALUATE) {
    return handle_evaluate(incoming, local_node_id, trainer);
  }
  if (message_type == MSG_TYPE_GET_PARAMETERS) {
    return handle_get_parameters(incoming, local_node_id, trainer);
  }

  std::cerr << "Unknown message type: " << message_type << std::endl;
  return nullptr;
}

}  // namespace flwr_local
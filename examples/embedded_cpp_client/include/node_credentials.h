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
// node_credentials.h
//
// ECDSA P-256 key pair used to authenticate Fleet requests to the SuperLink.
// The SuperLink's NodeAuthServerInterceptor requires every Fleet call to carry
// the PEM public key, a timestamp, and an ECDSA-SHA256 signature of the
// timestamp.
#ifndef NODE_CREDENTIALS_H
#define NODE_CREDENTIALS_H

#include <memory>
#include <string>

typedef struct evp_pkey_st EVP_PKEY;

namespace flwr_local {

class NodeCredentials {
 public:
  // Generate a new P-256 key pair. Throws std::runtime_error on failure.
  static NodeCredentials generate();

  // Public key as PEM (SubjectPublicKeyInfo), the format the SuperLink parses.
  std::string public_key_pem() const;

  // DER-encoded ECDSA-SHA256 signature over the given message.
  std::string sign(const std::string& message) const;

 private:
  explicit NodeCredentials(std::shared_ptr<EVP_PKEY> key);

  std::shared_ptr<EVP_PKEY> key_;
};

}  // namespace flwr_local

#endif  // NODE_CREDENTIALS_H

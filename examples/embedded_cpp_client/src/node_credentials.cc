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
// node_credentials.cc
#include "node_credentials.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace flwr_local {

namespace {

[[noreturn]] void throw_openssl_error(const std::string& what) {
  throw std::runtime_error(what + " (OpenSSL error " +
                           std::to_string(ERR_get_error()) + ")");
}

}  // namespace

NodeCredentials::NodeCredentials(std::shared_ptr<EVP_PKEY> key)
    : key_(std::move(key)) {}

NodeCredentials NodeCredentials::generate() {
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
      EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr), EVP_PKEY_CTX_free);
  if (!ctx) {
    throw_openssl_error("EVP_PKEY_CTX_new_from_name failed");
  }

  EVP_PKEY* raw_key = nullptr;
  if (EVP_PKEY_keygen_init(ctx.get()) <= 0 ||
      EVP_PKEY_CTX_set_group_name(ctx.get(), "prime256v1") <= 0 ||
      EVP_PKEY_keygen(ctx.get(), &raw_key) <= 0) {
    throw_openssl_error("EC P-256 key generation failed");
  }
  return NodeCredentials(std::shared_ptr<EVP_PKEY>(raw_key, EVP_PKEY_free));
}

std::string NodeCredentials::public_key_pem() const {
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()),
                                                BIO_free);
  if (!bio || PEM_write_bio_PUBKEY(bio.get(), key_.get()) != 1) {
    throw_openssl_error("PEM encoding of public key failed");
  }
  BUF_MEM* mem = nullptr;
  BIO_get_mem_ptr(bio.get(), &mem);
  return std::string(mem->data, mem->length);
}

std::string NodeCredentials::sign(const std::string& message) const {
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!ctx ||
      EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr,
                         key_.get()) != 1) {
    throw_openssl_error("EVP_DigestSignInit failed");
  }

  // First call sizes the buffer; second call writes the DER signature.
  size_t sig_len = 0;
  if (EVP_DigestSign(ctx.get(), nullptr, &sig_len,
                     reinterpret_cast<const unsigned char*>(message.data()),
                     message.size()) != 1) {
    throw_openssl_error("EVP_DigestSign (size) failed");
  }
  std::string signature(sig_len, '\0');
  if (EVP_DigestSign(ctx.get(),
                     reinterpret_cast<unsigned char*>(&signature[0]), &sig_len,
                     reinterpret_cast<const unsigned char*>(message.data()),
                     message.size()) != 1) {
    throw_openssl_error("EVP_DigestSign failed");
  }
  signature.resize(sig_len);
  return signature;
}

}  // namespace flwr_local

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/algorithms/experimental/hash.h"

#include <vector>

#include <openssl/sha.h>

namespace cobalt {

namespace {
size_t TruncateDigest(uint64_t digest, size_t max) {
  // TODO(azani): Use a truncation method that preserves the uniformity of
  // the distribution even if max is not a power of 2.
  return digest % max;
}
}  // namespace

const size_t SHA256_SIZE = 32;

uint64_t Hash64WithSeed(const uint8_t* data, size_t len, uint64_t seed) {
  // TODO(azani): Replace hash function with FarmHash.
  std::vector<uint8_t> seeded_data(sizeof(uint64_t) + len);
  *(reinterpret_cast<uint64_t*>(seeded_data.data())) = seed;
  std::copy(data, data + len, seeded_data.data() + sizeof(uint64_t));
  uint8_t sha256_digest[SHA256_SIZE];
  SHA256(seeded_data.data(), seeded_data.size(), sha256_digest);
  return (*reinterpret_cast<uint64_t*>(sha256_digest));
}

uint64_t Hash64WithSeed(const std::string& data, uint64_t seed) {
  return Hash64WithSeed(reinterpret_cast<const uint8_t*>(data.data()), data.size(), seed);
}

size_t TruncatedDigest(const uint8_t* data, size_t len, uint64_t seed, size_t max) {
  return TruncateDigest(Hash64WithSeed(data, len, seed), max);
}

size_t TruncatedDigest(const std::string& data, uint64_t seed, size_t max) {
  return TruncateDigest(Hash64WithSeed(data, seed), max);
}

}  // namespace cobalt

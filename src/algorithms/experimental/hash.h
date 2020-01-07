// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_EXPERIMENTAL_HASH_H_
#define COBALT_SRC_ALGORITHMS_EXPERIMENTAL_HASH_H_

#include <cstdint>
#include <string>

namespace cobalt {

// Stable non-cryptographic hashes.
// The goal is to pick hash functions that will not change in the future.

// Seeded 64 bits hash.
uint64_t Hash64WithSeed(const uint8_t* data, size_t len, uint64_t seed);

// Seeded 64 bits hash.
uint64_t Hash64WithSeed(const std::string& data, uint64_t seed);

// Returns a hash value for data less than max.
size_t TruncatedDigest(const uint8_t* data, size_t len, uint64_t seed, size_t max);

// Returns a hash value for data less than max.
size_t TruncatedDigest(const std::string& data, uint64_t seed, size_t max);

}  // namespace cobalt
#endif  // COBALT_SRC_ALGORITHMS_EXPERIMENTAL_HASH_H_

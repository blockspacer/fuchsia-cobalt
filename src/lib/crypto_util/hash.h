// Copyright 2017 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef COBALT_SRC_LIB_CRYPTO_UTIL_HASH_H_
#define COBALT_SRC_LIB_CRYPTO_UTIL_HASH_H_

#include <cstddef>

#include "src/lib/crypto_util/types.h"

namespace cobalt {

namespace crypto {

namespace hash {

// Note(rudominer) Hash() is used for generating the Bloom Filter bits for
// String RAPPOR. We allow up to 1024 Bloom bits and so we need to consume
// two bytes of digest per hash in the Bloom filter. We allow up to 8 hashes
// and so the DIGEST_SIZE must be at least 16.
static const size_t DIGEST_SIZE = 32;  // SHA-256 outputs 32 bytes.

// Computes the SHA256 digest of |data_len| bytes from |data| and writes the
// result to |out| which must have length |DIGEST_SIZE|.
//
// Returns true for success or false for failure.
bool Hash(const byte *data, size_t data_len, byte out[DIGEST_SIZE]);

}  // namespace hash

}  // namespace crypto

}  // namespace cobalt
#endif  // COBALT_SRC_LIB_CRYPTO_UTIL_HASH_H_

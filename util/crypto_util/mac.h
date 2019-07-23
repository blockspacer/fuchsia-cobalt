// Copyright 2016 The Fuchsia Authors
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

#ifndef COBALT_UTIL_CRYPTO_UTIL_MAC_H_
#define COBALT_UTIL_CRYPTO_UTIL_MAC_H_

#include <cstddef>

#include "util/crypto_util/types.h"

namespace cobalt {

namespace crypto {

namespace hmac {

static const size_t TAG_SIZE = 32;  // SHA-256 outputs 32 bytes.

// Computes the HMAC-SHA256 of |data_len| bytes from |data| using the given
// |key| of length |key_len| and writes the result to |tag|.
//
// |key_len| may be any non-negative integer.
// |tag| must have length |TAG_SIZE|.
bool HMAC(const byte *key, size_t key_len, const byte *data, size_t data_len,
          byte tag[TAG_SIZE]);

}  // namespace hmac

}  // namespace crypto

}  // namespace cobalt

#endif  // COBALT_UTIL_CRYPTO_UTIL_MAC_H_

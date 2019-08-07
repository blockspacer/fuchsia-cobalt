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

#ifndef COBALT_SRC_LIB_CRYPTO_UTIL_RANDOM_H_
#define COBALT_SRC_LIB_CRYPTO_UTIL_RANDOM_H_

#include <cstdint>
#include <memory>
#include <string>

#include "src/lib/crypto_util/types.h"

namespace cobalt {

namespace crypto {

// An instance of Random provides some utility functions for retrieving
// randomness.
class Random {
 public:
  virtual ~Random() = default;

  // Writes |num| bytes of random data from a uniform distribution to buf.
  // The caller must ensure that |buf| has enough space.
  virtual void RandomBytes(byte* buf, std::size_t num);

  // Writes |buf->size()| bytes of random data from a uniform distribution
  // into buf.
  void RandomString(std::string* buf);

  // Returns a uniformly random integer in the range [0, 2^32-1].
  uint32_t RandomUint32();

  // Returns a uniformly random integer in the range [0, 2^64-1].
  uint64_t RandomUint64();

  // Writes 8 * |size| independent random bits to |buffer|. For each bit the
  // probability of being equal to one is the given p. p must be in the range
  // [0.0, 1.0] or the result is undefined. p will be rounded to the nearest
  // value of the form n/(2^32) where n is an integer in the range [0, 2^32].
  // |size| must be less than or equal to 256.
  // Returns false to indicate failure.
  bool RandomBits(float p, byte* buffer, std::size_t size);
};

}  // namespace crypto

}  // namespace cobalt

#endif  // COBALT_SRC_LIB_CRYPTO_UTIL_RANDOM_H_

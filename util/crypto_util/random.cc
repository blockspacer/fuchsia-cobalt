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

#include <openssl/rand.h>

#include <cmath>
#include <memory>

#include "util/crypto_util/random.h"

namespace cobalt {
namespace crypto {

void Random::RandomBytes(byte* buf, std::size_t num) { RAND_bytes(buf, num); }

void Random::RandomString(std::string* buf) {
  RandomBytes(reinterpret_cast<byte*>(&((*buf)[0])), buf->size());
}

uint32_t Random::RandomUint32() {
  uint32_t x;
  RandomBytes(reinterpret_cast<byte*>(&x), 4);
  return x;
}

uint64_t Random::RandomUint64() {
  uint64_t x;
  RandomBytes(reinterpret_cast<byte*>(&x), 8);
  return x;
}

bool Random::RandomBits(float p, byte* buffer, std::size_t size) {
  // For every byte, returned by this function we need to allocate 32 bytes.
  // In order to prevent excessive allocations, this function will only
  // return up to 256 bytes.
  if (size > 256) {
    return false;
  }

  if (p <= 0.0 || p > 1.0) {
    for (std::size_t i = 0; i < size; i++) {
      buffer[i] = 0;
    }
    return true;
  }

  // threshold is the integer n in the range [0, 2^32] such that
  // n/2^32 best approximates p.
  uint64_t threshold =
      round(static_cast<double>(p) * (static_cast<double>(UINT32_MAX) + 1));

  // For every bit in the output, we need a 32 bit number.
  auto random_bytes = std::make_unique<uint32_t[]>(8 * size);
  RandomBytes(reinterpret_cast<byte*>(random_bytes.get()),
              8 * size * sizeof(uint32_t));
  for (std::size_t byte_index = 0; byte_index < size; byte_index++) {
    buffer[byte_index] = 0;
    for (int i = 0; i < 8; i++) {
      uint8_t random_bit = (random_bytes[byte_index + i] < threshold);
      buffer[byte_index] |= random_bit << i;
    }
  }

  return true;
}

}  // namespace crypto

}  // namespace cobalt

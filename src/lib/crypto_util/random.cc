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

#include "src/lib/crypto_util/random.h"

#include <cmath>
#include <memory>
#include <vector>

#include <openssl/rand.h>

namespace cobalt {
namespace crypto {

namespace {
constexpr size_t kMaxRandomBits = 256;
constexpr size_t kBitsPerByte = 8;
constexpr size_t kBytesPerU32 = 4;
constexpr size_t kBytesPerU64 = 8;
}  // namespace

void Random::RandomBytes(byte* buf, std::size_t num) { RAND_bytes(buf, num); }

void Random::RandomString(std::string* buf) {
  RandomBytes(reinterpret_cast<byte*>(&((*buf)[0])), buf->size());
}

uint32_t Random::RandomUint32() {
  uint32_t x;
  RandomBytes(reinterpret_cast<byte*>(&x), kBytesPerU32);
  return x;
}

uint64_t Random::RandomUint64() {
  uint64_t x;
  RandomBytes(reinterpret_cast<byte*>(&x), kBytesPerU64);
  return x;
}

bool Random::RandomBits(float p, byte* buffer, std::size_t size) {
  // For every byte, returned by this function we need to allocate 32 bytes.
  // In order to prevent excessive allocations, this function will only
  // return up to 256 bytes.
  if (size > kMaxRandomBits) {
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
  uint64_t threshold = round(static_cast<double>(p) * (static_cast<double>(UINT32_MAX) + 1));

  // For every bit in the output, we need a 32 bit number.
  std::vector<uint32_t> random_bytes(kBitsPerByte * size);
  RandomBytes(reinterpret_cast<byte*>(random_bytes.data()), kBitsPerByte * size * sizeof(uint32_t));
  for (std::size_t byte_index = 0; byte_index < size; byte_index++) {
    buffer[byte_index] = 0;
    for (size_t i = 0; i < kBitsPerByte; i++) {
      uint8_t random_bit = (random_bytes[byte_index + i] < threshold);
      buffer[byte_index] |= random_bit << i;
    }
  }

  return true;
}

}  // namespace crypto
}  // namespace cobalt

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

#ifndef COBALT_SRC_LIB_CRYPTO_UTIL_RANDOM_TEST_UTILS_H_
#define COBALT_SRC_LIB_CRYPTO_UTIL_RANDOM_TEST_UTILS_H_

#include <openssl/chacha.h>

#include "src/lib/crypto_util/random.h"

namespace cobalt {
namespace crypto {

// DeterministicRandom is a subclass of Random that overrides RandomBytes()
// to use a deterministic PRNG.
class DeterministicRandom : public Random {
 public:
  DeterministicRandom() = default;

  ~DeterministicRandom() override = default;

  // Implementes a deterministic PRNG by using chacha20 with a zero key and a
  // counter for the nonce. This code was copied from
  // crypto/rand/deteriministic.c in Boring SSL. We use this particular
  // PRNG becuase it has some properties in common with the non-deterministic
  // version of RandomBytes we use in production and so we are testing using
  // a source of randomness that is similar to the production source of
  // randomness.
  void RandomBytes(byte *buf, std::size_t num) override {
    static const uint8_t kZeroKey[32] = {};
    uint8_t nonce[12];  // NOLINT readability-magic-numbers
    memset(nonce, 0, sizeof(nonce));
    memcpy(nonce, &num_calls_, sizeof(num_calls_));

    memset(buf, 0, num);
    CRYPTO_chacha_20(buf, buf, num, kZeroKey, nonce, 0);
    num_calls_++;
  }

 private:
  uint64_t num_calls_ = 0;
};

}  // namespace crypto
}  // namespace cobalt

#endif  // COBALT_SRC_LIB_CRYPTO_UTIL_RANDOM_TEST_UTILS_H_

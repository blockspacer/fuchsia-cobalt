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

#include "src/lib/crypto_util/mac.h"

#include <openssl/digest.h>
#include <openssl/hmac.h>

namespace cobalt {
namespace crypto {
namespace hmac {

bool HMAC(const byte *key, const size_t key_len, const byte *data, const size_t data_len,
          byte tag[TAG_SIZE]) {
  unsigned int out_len_unused;
  return HMAC(EVP_sha256(), key, key_len, data, data_len, tag, &out_len_unused);
}

}  // namespace hmac
}  // namespace crypto
}  // namespace cobalt

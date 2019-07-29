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

#include "util/crypto_util/errors.h"

#include <string>

#include <openssl/err.h>

namespace cobalt {
namespace crypto {

namespace {
constexpr size_t kOpenSSLErrorLen = 256;
}

std::string GetLastErrorMessage() {
  char buf[kOpenSSLErrorLen];
  ERR_error_string_n(ERR_peek_last_error(), &buf[0], kOpenSSLErrorLen);
  return std::string(buf);
}

}  // namespace crypto
}  // namespace cobalt

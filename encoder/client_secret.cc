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

#include "encoder/client_secret.h"

#include <utility>

#include "util/crypto_util/base64.h"
#include "util/crypto_util/random.h"

namespace cobalt {
namespace encoder {

// static
ClientSecret ClientSecret::GenerateNewSecret() {
  crypto::Random rand;
  return GenerateNewSecret(&rand);
}

// static
ClientSecret ClientSecret::GenerateNewSecret(crypto::Random* rand) {
  ClientSecret client_secret;
  client_secret.bytes_.resize(kNumSecretBytes);
  rand->RandomBytes(client_secret.bytes_.data(), kNumSecretBytes);
  return client_secret;
}

// static
ClientSecret ClientSecret::FromToken(const std::string& token) {
  ClientSecret client_secret;
  crypto::Base64Decode(token, &client_secret.bytes_);
  return client_secret;
}

std::string ClientSecret::GetToken() {
  std::string token;
  crypto::Base64Encode(bytes_, &token);
  return token;
}

}  // namespace encoder
}  // namespace cobalt

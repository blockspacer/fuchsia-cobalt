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

#include "src/system_data/client_secret.h"

#include <string>
#include <utility>

#include "./gtest.h"

namespace cobalt {
namespace encoder {

// Tests the basic functionality of ClientSecret.
TEST(ClientSecretTest, BasicTest) {
  // Generate two ClientSecrets and get tokens for them.
  ClientSecret secret1 = ClientSecret::GenerateNewSecret();
  std::string token1 = secret1.GetToken();

  ClientSecret secret2 = ClientSecret::GenerateNewSecret();
  std::string token2 = secret2.GetToken();

  // Now make copies of the secrets from their tokens.
  ClientSecret secret1b = ClientSecret::FromToken(token1);
  ClientSecret secret2b = ClientSecret::FromToken(token2);

  // Check that the two secrets are different from each other but
  // the copies are equal to their originals.
  EXPECT_EQ(secret1, secret1b);
  EXPECT_EQ(secret2, secret2b);
  EXPECT_NE(secret1, secret2);

  // Construct secret1c by moving data out of secret1b.
  // Now secret1 should equal secret 1c.
  ClientSecret secret1c(std::move(secret1b));
  EXPECT_EQ(secret1, secret1c);

  // All secrets are valid except for 1b because it was moved from.
  EXPECT_TRUE(secret1.valid());
  EXPECT_FALSE(secret1b.valid());  // NOLINT bugprone-use-after-move
  EXPECT_TRUE(secret1c.valid());
  EXPECT_TRUE(secret2.valid());
  EXPECT_TRUE(secret2b.valid());

  // A bad token yields an invalid ClientSecret.
  ClientSecret invalid_secret = ClientSecret::FromToken("fake token");
  EXPECT_FALSE(invalid_secret.valid());
  EXPECT_EQ(std::string(), invalid_secret.GetToken());
}

}  // namespace encoder
}  // namespace cobalt

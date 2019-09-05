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
#include "src/lib/crypto_util/base64.h"

#include <memory>
#include <vector>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::crypto {

// Tests the basic functionality of Base64Encode and Base64Decode.
TEST(Base64Test, BasicFunctionality) {
  // Encode
  std::vector<byte> data = {0, 1, 2, 3, 4, 5, 6, 255, 254, 253, 252, 251, 250};
  std::string encoded;
  EXPECT_TRUE(Base64Encode(data, &encoded));
  EXPECT_EQ(std::string("AAECAwQFBv/+/fz7+g=="), encoded);

  // Decode
  std::vector<byte> decoded;
  EXPECT_TRUE(Base64Decode(encoded, &decoded));
  EXPECT_EQ(data, decoded);
}

// Tests the function RegexEncode and RegexDecode.
TEST(Base64Test, RegexEncode) {
  // Encode
  std::vector<byte> data = {0, 1, 2, 3, 4, 5, 6, 255, 254, 253, 252, 251, 250};
  std::string data_str(data.begin(), data.end());
  std::string encoded;
  EXPECT_TRUE(RegexEncode(data_str, &encoded));

  EXPECT_EQ(std::string("AAECAwQFBv/_/fz7_g=="), encoded);
  // Decode
  std::string decoded;
  EXPECT_TRUE(RegexDecode(encoded, &decoded));
  EXPECT_EQ(data_str, decoded);

  // Expect the decoding to fail if the input string contain a '+' since we are
  // using "_" instead of "+" in our regex-friendly version of the encoding.
  encoded = "AAECAwQFBv/+/fz7_g==";
  EXPECT_FALSE(RegexDecode(encoded, &decoded));

  // Expect the decoding to fail if the input string contain an '&' since "&"
  // is not used in Base64 encoding.
  encoded = "AAECAwQFBv/&/fz7_g==";
  EXPECT_FALSE(RegexDecode(encoded, &decoded));
}

}  // namespace cobalt::crypto

// Copyright 2017 The Fuchsia Authors
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

#include "src/lib/crypto_util/hash.h"

#include <string>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::crypto::hash {

TEST(HashTest, TestHash) {
  std::string data =
      "The algorithms were first published in 2001 in the draft FIPS PUB "
      "180-2, at which time public review and comments were accepted. In "
      "August 2002, FIPS PUB 180-2 became the new Secure Hash Standard, "
      "replacing FIPS PUB 180-1, which was released in April 1995. The updated "
      "standard included the original SHA-1 algorithm, with updated technical "
      "notation consistent with that describing the inner workings of the "
      "SHA-2 family.[9]";

  // Hash the data into digest.
  byte digest[DIGEST_SIZE];
  EXPECT_TRUE(Hash(reinterpret_cast<byte*>(&data[0]), data.size(), digest));

  // Generate a human-readable string representing the bytes of digest.
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (unsigned char ch : digest) {
    stream << std::setw(2) << static_cast<int>(ch);
  }

  // Compare this to an expected result.
  EXPECT_EQ(std::string("fc11f3cbffea99f65944e50e72e5bfc09674eed67bcebcd76ec0f9dc90faef05"),
            stream.str());
}

}  // namespace cobalt::crypto::hash

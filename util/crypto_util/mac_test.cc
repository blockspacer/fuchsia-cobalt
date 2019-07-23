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

#include "util/crypto_util/mac.h"

#include <limits.h>

#include <string>
#include <vector>

#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/crypto_util/errors.h"
#include "util/crypto_util/random.h"

namespace cobalt {
namespace crypto {
namespace hmac {

// This is just a smoke test of the hmac function. We invoke it 101 times
// using a key length ranging from 0 to 100 and a data length ranging from
// 100 to 0 and we check that it completes without an error.
TEST(HmacTest, VariousKeyLengths) {
  byte key[100], data[100];
  Random rand;
  rand.RandomBytes(key, 100);
  rand.RandomBytes(data, 100);
  byte tag[hmac::TAG_SIZE];
  for (size_t key_len = 0; key_len <= 100; key_len++) {
    EXPECT_TRUE(HMAC(key, key_len, data, 100 - key_len, tag)) << GetLastErrorMessage();
  }
}

// A helper function for the EqualsAndNotEqual test. It takes
// two keys and two data arrays and invokes HMAC on each of them.
// Then it compares them for equality or inequality depending on the
// value of |expect_eq|.
void checkEqualHmacs(byte key1[32], byte key2[32], byte data1[100], byte data2[100],
                     bool expect_eq) {
  byte tag1[hmac::TAG_SIZE], tag2[hmac::TAG_SIZE];

  EXPECT_TRUE(HMAC(key1, 32, data1, 100, tag1)) << GetLastErrorMessage();
  EXPECT_TRUE(HMAC(key2, 32, data2, 100, tag2)) << GetLastErrorMessage();
  if (expect_eq) {
    EXPECT_EQ(std::string(reinterpret_cast<char*>(tag1), hmac::TAG_SIZE),
              std::string(reinterpret_cast<char*>(tag2), hmac::TAG_SIZE));
  } else {
    EXPECT_NE(std::string(reinterpret_cast<char*>(tag1), hmac::TAG_SIZE),
              std::string(reinterpret_cast<char*>(tag2), hmac::TAG_SIZE));
  }
}

// Tests that if we call HMAC twice with the same key and data we get
// the same tag but if we change either the key or the data we get a
// different tag.
TEST(HmacTest, EqualAndNotEqual) {
  byte key1[32], key2[32], data1[100], data2[100];
  Random rand;
  rand.RandomBytes(key1, 32);
  rand.RandomBytes(key2, 32);
  rand.RandomBytes(data1, 100);
  rand.RandomBytes(data2, 100);

  checkEqualHmacs(key1, key1, data1, data1, true);
  checkEqualHmacs(key1, key1, data1, data2, false);
  checkEqualHmacs(key1, key2, data1, data1, false);
}

}  // namespace hmac

}  // namespace crypto

}  // namespace cobalt

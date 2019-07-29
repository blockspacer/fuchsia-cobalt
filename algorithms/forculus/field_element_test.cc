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

#include "algorithms/forculus/field_element.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace forculus {

/****************************** Notice *************************************
 *
 * The tests currently in this file are based on the temporary, insecure
 * implementation of FieldElement that interprets the first 32 bits of
 * data as an integer in little-endian and discards the rest of the bytes.
 * These will be replaced by different tests when the field changes to
 *GF(2^128).
 *
 *****************************************************************************/

namespace {
// Make the FieldElement with the given vector of bytes. This wrapper around the
// constructor is necessary because the compiler can't tell which constructor
// to use with an expression like FieldElement({2}).
FieldElement FromBytes(std::vector<byte>&& bytes) { return FieldElement(std::move(bytes)); }
FieldElement FromString(const std::string& data) { return FieldElement(data); }

FieldElement FromInt(uint32_t x) {
  std::vector<byte> bytes(sizeof(x));
  std::memcpy(bytes.data(), &x, sizeof(x));
  return FieldElement(std::move(bytes));
}

}  // namespace

TEST(FieldElementTest, TestConstructors) {
  // Expect that the byte constructor discards all but the first 4 bytes.
  {
    const FieldElement el = FromBytes({0, 1, 2, 3, 4, 5});
    const byte* bytes = el.KeyBytes();
    EXPECT_EQ(0, bytes[0]);
    EXPECT_EQ(1, bytes[1]);
    EXPECT_EQ(2, bytes[2]);
    EXPECT_EQ(3, bytes[3]);
    EXPECT_EQ(0, bytes[4]);
    EXPECT_EQ(0, bytes[5]);
  }

  // Expect that the string constructor discards all but the first 4 bytes.
  {
    const FieldElement el = FromString({0, 1, 2, 3, 4, 5, 6});
    const byte* bytes = el.KeyBytes();
    EXPECT_EQ(0, bytes[0]);
    EXPECT_EQ(1, bytes[1]);
    EXPECT_EQ(2, bytes[2]);
    EXPECT_EQ(3, bytes[3]);
    EXPECT_EQ(0, bytes[4]);
    EXPECT_EQ(0, bytes[5]);
  }

  // Expect that 1 is represented in little-endian as 1 0 0 0 ...
  {
    const FieldElement el = FieldElement(true);
    const byte* bytes = el.KeyBytes();
    EXPECT_EQ(1, bytes[0]);
    EXPECT_EQ(0, bytes[1]);
    EXPECT_EQ(0, bytes[2]);
    EXPECT_EQ(0, bytes[3]);
    EXPECT_EQ(0, bytes[4]);
    EXPECT_EQ(0, bytes[5]);
  }

  // Expect that 0 is represented as 0 0 0 ...
  {
    const FieldElement el = FieldElement(false);
    const byte* bytes = el.KeyBytes();
    EXPECT_EQ(0, bytes[0]);
    EXPECT_EQ(0, bytes[1]);
    EXPECT_EQ(0, bytes[2]);
    EXPECT_EQ(0, bytes[3]);
    EXPECT_EQ(0, bytes[4]);
    EXPECT_EQ(0, bytes[5]);
  }

  // Test the copy constructor
  const FieldElement x = FromBytes({0, 1, 2, 3, 4, 5});
  FieldElement y(x);
  EXPECT_EQ(x, y);

  // Test the move constructor
  FieldElement z(std::move(y));
  EXPECT_EQ(x, z);
  EXPECT_NE(x, y);  // NOLINT bugprone-use-after-move

  // Test the copy assignment operator
  y = x;
  EXPECT_EQ(x, y);

  // Test the move assignment operator
  z = std::move(y);
  EXPECT_EQ(x, z);
  EXPECT_NE(x, y);  // NOLINT bugprone-use-after-move
}

TEST(FieldElementTest, TestCopyBytesToString) {
  const FieldElement el = FromBytes({0, 1, 2, 3, 4, 5});
  std::string s;
  el.CopyBytesToString(&s);
  EXPECT_EQ(FieldElement::kDataSize, s.size());
  std::string expected_string =
      std::string("\0\x1\x2\x3", 4) + std::string(FieldElement::kDataSize - 4, 0);
  EXPECT_EQ(expected_string, s);
}

TEST(FieldElementTest, TestArithmetic) {
  // Test that 2 + 3 = 5.
  EXPECT_EQ(FromInt(5), FromInt(2) + FromInt(3));

  // Test that 2 + 3 = 5 with +=
  FieldElement x = FromInt(2);
  FieldElement y = FromInt(3);
  FieldElement z = FromInt(5);
  x += y;
  EXPECT_EQ(z, x);

  // Test that -1 + 1 = 0.
  // The bytes are kLargestPrime -1 in little-endian.
  static const FieldElement kMinusOne = FromBytes({0xF4, 0xFE, 0xFF, 0xFF});
  EXPECT_EQ(FieldElement(false), kMinusOne + FieldElement(true));

  // Test that -1 + 1 = 0 with +=.
  x = kMinusOne;
  x += FieldElement(true);
  EXPECT_EQ(FieldElement(false), x);

  // Test that 5 - 2 = 3
  EXPECT_EQ(FromInt(3), FromInt(5) - FromInt(2));

  // Test that 5 - 2 = 3 using -=.
  x = FromInt(5);
  y = FromInt(2);
  x -= y;
  EXPECT_EQ(FromInt(3), x);

  // Test that 0 - 1 = -1
  EXPECT_EQ(kMinusOne, FieldElement(false) - FieldElement(true));

  // Test that 0 - 1 = -1 using -=.
  x = FieldElement(false);
  x -= FieldElement(true);
  EXPECT_EQ(kMinusOne, x);

  // Test that 1999000 - 1998999 = 1
  EXPECT_EQ(FieldElement(true), FromInt(1999000) - FromInt(1998999));

  // Test that 1999000 - 1998999 = 1 using -=
  x = FromInt(1999000);   // NOLINT readability-magic-numbers
  x -= FromInt(1998999);  // NOLINT readability-magic-numbers
  EXPECT_EQ(FieldElement(true), x);

  // Test that 3 * 5 = 15.
  EXPECT_EQ(FromInt(15), FromInt(3) * FromInt(5));

  // Test that 3 * 5 = 15 using *=
  x = FromInt(3);
  x *= FromInt(5);
  EXPECT_EQ(FromInt(15), x);

  // Test that -1 * 2 = -2.
  static const FieldElement kMinus2 = FromBytes({0xF3, 0xFE, 0xFF, 0xFF});
  EXPECT_EQ(kMinus2, kMinusOne * FromInt(2));

  // Test that -1 * 2 = -2 using *=
  x = kMinusOne;
  x *= FromInt(2);
  EXPECT_EQ(kMinus2, x);

  // Check that 1/1 = 1.
  x = FieldElement(true);
  EXPECT_EQ(x, x / x);

  // Check that 5/5 = 1
  x = FromInt(5);
  EXPECT_EQ(FieldElement(true), x / x);

  // Check that 10/5 = 2
  y = FromInt(10);
  EXPECT_EQ(FromInt(2), y / x);

  // Check that 10/5 = 2 using /=
  y = FromInt(10);
  y /= x;
  EXPECT_EQ(FromInt(2), y);

  // Check that 0/5 = 0
  y = FieldElement(false);
  EXPECT_EQ(y, y / x);

  // Check that 1/2 * 2 = 1.
  x = FieldElement(true) / FromInt(2);
  x *= FromInt(2);
  EXPECT_EQ(FieldElement(true), x);

  // Check that 2/3 * 3 = 2.
  x = FromInt(2) / FromInt(3);
  x *= FromInt(3);
  EXPECT_EQ(FromInt(2), x);

  // Check that 2/3 * 2/3 = 4/9
  x = FromInt(2) / FromInt(3);
  x *= x;
  EXPECT_EQ(FromInt(4) / FromInt(9), x);

  // Check that 1999*1000/(1000 - 999) + 2001*999/(999 - 1000) = 1.
  FieldElement x0 = FromInt(999);   // NOLINT readability-magic-numbers
  FieldElement y0 = FromInt(1999);  // NOLINT readability-magic-numbers
  FieldElement x1 = FromInt(1000);
  FieldElement y1 = FromInt(2001);  // NOLINT readability-magic-numbers
  EXPECT_EQ(FieldElement(true), y0 * x1 / (x1 - x0) + y1 * x0 / (x0 - x1));
}

}  // namespace forculus
}  // namespace cobalt

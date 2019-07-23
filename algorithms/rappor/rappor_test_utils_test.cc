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
#include "algorithms/rappor/rappor_test_utils.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::rappor {

// Tests the function DataToBinaryString().
TEST(TestUtilsTest, DataToBinaryString) {
  // One byte
  EXPECT_EQ(DataToBinaryString(std::string("\0", 1)), "00000000");
  EXPECT_EQ(DataToBinaryString(std::string("\x1", 1)), "00000001");
  EXPECT_EQ(DataToBinaryString(std::string("\x2", 1)), "00000010");
  EXPECT_EQ(DataToBinaryString(std::string("\x3", 1)), "00000011");
  EXPECT_EQ(DataToBinaryString(std::string("\xFE", 1)), "11111110");

  // Two bytes
  EXPECT_EQ(DataToBinaryString(std::string("\0\0", 2)), "0000000000000000");
  EXPECT_EQ(DataToBinaryString(std::string("\0\1", 2)), "0000000000000001");
  EXPECT_EQ(DataToBinaryString(std::string("\1\0", 2)), "0000000100000000");
  EXPECT_EQ(DataToBinaryString(std::string("\0\1", 2)), "0000000000000001");
  EXPECT_EQ(DataToBinaryString(std::string("\1\xFE", 2)), "0000000111111110");

  // Three bytes
  EXPECT_EQ(DataToBinaryString(std::string("\0\0\0", 3)), "000000000000000000000000");
  EXPECT_EQ(DataToBinaryString(std::string("\0\0\1", 3)), "000000000000000000000001");
  EXPECT_EQ(DataToBinaryString(std::string("\0\1\0", 3)), "000000000000000100000000");
  EXPECT_EQ(DataToBinaryString(std::string("\1\1\0", 3)), "000000010000000100000000");
}

// Test the function BinaryStringToData
TEST(TestUtilsTest, BinaryStringToData) {
  // One byte
  EXPECT_EQ(std::string("\0", 1), BinaryStringToData("00000000"));
  EXPECT_EQ(std::string("\1", 1), BinaryStringToData("00000001"));
  EXPECT_EQ(std::string("\x2", 1), BinaryStringToData("00000010"));
  EXPECT_EQ(std::string("\x3", 1), BinaryStringToData("00000011"));
  EXPECT_EQ(std::string("\xFE", 1), BinaryStringToData("11111110"));

  // Two bytes
  EXPECT_EQ(std::string("\0\0", 2), BinaryStringToData("0000000000000000"));
  EXPECT_EQ(std::string("\0\1", 2), BinaryStringToData("0000000000000001"));
  EXPECT_EQ(std::string("\1\0", 2), BinaryStringToData("0000000100000000"));
  EXPECT_EQ(std::string("\1\xFE", 2), BinaryStringToData("0000000111111110"));

  // Three bytes
  EXPECT_EQ(std::string("\0\0\0", 3), BinaryStringToData("000000000000000000000000"));
  EXPECT_EQ(std::string("\0\0\1", 3), BinaryStringToData("000000000000000000000001"));
  EXPECT_EQ(std::string("\0\1\0", 3), BinaryStringToData("000000000000000100000000"));
  EXPECT_EQ(std::string("\1\1\0", 3), BinaryStringToData("000000010000000100000000"));
}

}  // namespace cobalt::rappor

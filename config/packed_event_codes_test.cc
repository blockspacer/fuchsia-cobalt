// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config/packed_event_codes.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace config {

TEST(PackEventCodes, AllowsEmptyIterator) {
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){}), 0u);
}

TEST(PackEventCodes, AcceptsSingleElement) {
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){939}), 939u);
}

TEST(PackEventCodes, AcceptsFiveElements) {
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){
                0b1111100000,
                0b1110011100,
                0b1101111011,
                0b1111010110,
                0b1010110101,
            }),
            0b10101101011111010110110111101111100111001111100000u);
}

TEST(PackEventCodes, IgnoresExtraElements) {
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){
                0b1111100000,
                0b1110011100,
                0b1101111011,
                0b1111010110,
                0b1010110101,

                // These two should be ignored.
                0b1010101010,
                0b1101101111,
            }),
            0b10101101011111010110110111101111100111001111100000u);
}

TEST(PackEventCodes, DoesNotOverflow) {
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){1025}), 1u);
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){0, 1025}), 0b10000000000u);
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){0, 0, 1025}),
            0b100000000000000000000u);
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){0, 0, 0, 1025}),
            0b1000000000000000000000000000000u);
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){0, 0, 0, 0, 1025}),
            0b10000000000000000000000000000000000000000u);
}

TEST(UnpackEventCodes, AcceptsNullEventCodes) {
  ASSERT_EQ(UnpackEventCodes(0), ((std::vector<uint32_t>){0, 0, 0, 0, 0}));
}

TEST(UnpackEventCodes, DecodesSingleElement) {
  ASSERT_EQ(UnpackEventCodes(939u), ((std::vector<uint32_t>){939, 0, 0, 0, 0}));
}

TEST(UnpackEventCodes, UnpacksFiveElements) {
  ASSERT_EQ(
      UnpackEventCodes(0b10101101011111010110110111101111100111001111100000u),
      ((std::vector<uint32_t>){
          0b1111100000,
          0b1110011100,
          0b1101111011,
          0b1111010110,
          0b1010110101,
      }));
}

TEST(UnpackEventCodes, ReturnsZeroesOnUnknownVersion) {
  ASSERT_EQ(
      // This packed_event_codes has a version of 1, which we don't know how to
      // decode.
      UnpackEventCodes(0x100ABCDEF1234567),
      ((std::vector<uint32_t>){0, 0, 0, 0, 0}));
}

TEST(BackAndForth, PackUnpackIsStable) {
  std::vector<std::vector<uint32_t>> tests = {
      {0, 0, 0, 0, 0},       {100, 0, 0, 0, 0},       {100, 200, 0, 0, 0},
      {100, 200, 300, 0, 0}, {100, 200, 300, 400, 0}, {100, 200, 300, 400, 500},
  };

  for (auto test : tests) {
    ASSERT_EQ(UnpackEventCodes(PackEventCodes(test)), test);
  }
}

}  // namespace config
}  // namespace cobalt

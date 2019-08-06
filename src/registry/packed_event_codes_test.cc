// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/registry/packed_event_codes.h"

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
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){32769}), 0x1000000000000001u);
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){0, 32769}), 0x1000000000008000u);
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){0, 0, 32769}), 0x1000000040000000u);
  ASSERT_EQ(PackEventCodes((std::vector<uint32_t>){0, 0, 0, 32769}), 0x1000200000000000u);
}

TEST(PackEventCodes, UpgradesToV1) {
  ASSERT_EQ(PackEventCodes(std::vector<uint32_t>{1023}), 1023u);
  // Sets version field and encodes using 15 bits.
  ASSERT_EQ(PackEventCodes(std::vector<uint32_t>{1024}), 0x1000000000000400u);
}

TEST(UnpackEventCodes, AcceptsNullEventCodes) {
  ASSERT_EQ(UnpackEventCodes(0), ((std::vector<uint32_t>){0, 0, 0, 0, 0}));
}

TEST(UnpackEventCodes, DecodesSingleElement) {
  ASSERT_EQ(UnpackEventCodes(939u), ((std::vector<uint32_t>){939, 0, 0, 0, 0}));
}

TEST(UnpackEventCodes, UnpacksFiveElements) {
  ASSERT_EQ(UnpackEventCodes(0b10101101011111010110110111101111100111001111100000u),
            ((std::vector<uint32_t>){
                0b1111100000,
                0b1110011100,
                0b1101111011,
                0b1111010110,
                0b1010110101,
            }));
}

TEST(UnpackEventCodes, UnpacksFourV1Elements) {
  ASSERT_EQ(UnpackEventCodes(0b1101101010010101101011111010110110111101111100111001111100000u),
            ((std::vector<uint32_t>){
                0b111001111100000u,
                0b110111101111100u,
                0b101011111010110u,
                0b101101010010101u,
            }));
}

TEST(UnpackEventCodes, ReturnsZeroesOnUnknownVersion) {
  ASSERT_EQ(
      // This packed_event_codes has a version of 2, which we don't know how to
      // decode.
      UnpackEventCodes(0x200ABCDEF1234567), ((std::vector<uint32_t>){0, 0, 0, 0, 0}));
}

TEST(BackAndForth, PackUnpackIsStable) {
  // TODO(zmbush): Look into clearing up this test.
  std::vector<std::vector<uint32_t>> tests = {
      {0, 0, 0, 0, 0},        {100, 0, 0, 0, 0},        {100, 200, 0, 0, 0},
      {100, 200, 300, 0, 0},  {100, 200, 300, 400, 0},  {100, 200, 300, 400, 500},
      {1024 - 1, 0, 0, 0, 0}, {1024, 0, 0, 0},          {1024, 3005, 0, 0},
      {1024, 3005, 4010, 0},  {1024, 3005, 4010, 30000}};

  for (const auto &test : tests) {
    ASSERT_EQ(UnpackEventCodes(PackEventCodes(test)), test);
  }
}

}  // namespace config
}  // namespace cobalt

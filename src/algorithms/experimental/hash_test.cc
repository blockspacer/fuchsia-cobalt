// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/algorithms/experimental/hash.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

TEST(Hash64WithSeed, StableHash) {
  EXPECT_EQ(0xb5ab47d4df4eb16cu, Hash64WithSeed("hello world", 1));
  EXPECT_EQ(0x351e4687378f8240u, Hash64WithSeed("hi there earth", 1));
  EXPECT_EQ(0xe7b848f961c5d168u, Hash64WithSeed("bonjour monde", 1));
  EXPECT_EQ(0x826c0bf8b3d8ccc9u, Hash64WithSeed("hallo Welt", 1));
}

TEST(Hash64WithSeed, DifferentSeeds) {
  auto hash0 = Hash64WithSeed("hello world", 0);
  auto hash1 = Hash64WithSeed("hello world", 1);
  EXPECT_NE(hash0, hash1);
}

TEST(Hash64WithSeed, DifferentFirstLetter) {
  auto hash0 = Hash64WithSeed("hello world", 1);
  auto hash1 = Hash64WithSeed("iello world", 1);
  EXPECT_NE(hash0, hash1);
}

TEST(Hash64WithSeed, DifferentLastLetter) {
  auto hash0 = Hash64WithSeed("hello world", 1);
  auto hash1 = Hash64WithSeed("hello worle", 1);
  EXPECT_NE(hash0, hash1);
}

TEST(TruncatedDigest, StableDigest) {
  EXPECT_EQ(36u, TruncatedDigest("hello world", 1, 100));
  EXPECT_EQ(40u, TruncatedDigest("hi there earth", 1, 100));
  EXPECT_EQ(48u, TruncatedDigest("bonjour monde", 1, 100));
  EXPECT_EQ(37u, TruncatedDigest("hallo Welt", 1, 100));
}

TEST(TruncatedDigest, DifferentSeeds) {
  auto digest0 = TruncatedDigest("hello world", 0, 100);
  auto digest1 = TruncatedDigest("hello world", 1, 100);
  EXPECT_NE(digest0, digest1);
}

TEST(TruncatedDigest, DifferentFirstLetter) {
  auto digest0 = TruncatedDigest("hello world", 1, 100);
  auto digest1 = TruncatedDigest("iello world", 1, 100);
  EXPECT_NE(digest0, digest1);
}

TEST(TruncatedDigest, DifferentLastLetter) {
  auto digest0 = TruncatedDigest("hello world", 1, 100);
  auto digest1 = TruncatedDigest("hello worle", 1, 100);
  EXPECT_NE(digest0, digest1);
}

}  // namespace cobalt

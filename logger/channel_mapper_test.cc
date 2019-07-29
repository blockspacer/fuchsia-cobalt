// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/channel_mapper.h"

#include <vector>

#include "./gtest.h"

namespace cobalt {
namespace logger {

TEST(ChannelMapper, TestBasicMapper) {
  ChannelMapper mapper({"debug-channel-1", "debug-channel-2", "debug-channel-3"});

  // Only debug channels should map to DEBUG.
  EXPECT_EQ(mapper.ToReleaseStage("debug-channel-1"), ReleaseStage::DEBUG);
  EXPECT_EQ(mapper.ToReleaseStage("debug-channel-2"), ReleaseStage::DEBUG);
  EXPECT_EQ(mapper.ToReleaseStage("debug-channel-3"), ReleaseStage::DEBUG);

  // Anything else should map to GA.
  EXPECT_EQ(mapper.ToReleaseStage(""), ReleaseStage::GA);
  EXPECT_EQ(mapper.ToReleaseStage("<unknown>"), ReleaseStage::GA);
  EXPECT_EQ(mapper.ToReleaseStage("<unset>"), ReleaseStage::GA);
  EXPECT_EQ(mapper.ToReleaseStage("prod-main"), ReleaseStage::GA);
}

TEST(ChannelMapper, TestMapper) {
  ChannelMapper mapper({
      {ReleaseStage::DEBUG, {"debug-channel-1", "debug-channel-2"}},
      {ReleaseStage::FISHFOOD, {"fishfood-channel"}},
      {ReleaseStage::DOGFOOD, {"dogfood-channel-1", "dogfood-channel-2"}},
  });

  // Only debug channels should map to DEBUG.
  EXPECT_EQ(mapper.ToReleaseStage("debug-channel-1"), ReleaseStage::DEBUG);
  EXPECT_EQ(mapper.ToReleaseStage("debug-channel-2"), ReleaseStage::DEBUG);

  // Only fishfood channels should map to FISHFOOD.
  EXPECT_EQ(mapper.ToReleaseStage("fishfood-channel"), ReleaseStage::FISHFOOD);

  // Only dogfood channels should map to DOGFOOD.
  EXPECT_EQ(mapper.ToReleaseStage("dogfood-channel-1"), ReleaseStage::DOGFOOD);
  EXPECT_EQ(mapper.ToReleaseStage("dogfood-channel-2"), ReleaseStage::DOGFOOD);

  // Anything else should map to GA.
  EXPECT_EQ(mapper.ToReleaseStage(""), ReleaseStage::GA);
  EXPECT_EQ(mapper.ToReleaseStage("<unknown>"), ReleaseStage::GA);
  EXPECT_EQ(mapper.ToReleaseStage("<unset>"), ReleaseStage::GA);
  EXPECT_EQ(mapper.ToReleaseStage("prod-main"), ReleaseStage::GA);
}

}  // namespace logger
}  // namespace cobalt

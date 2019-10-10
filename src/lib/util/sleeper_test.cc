// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>

#include "src/lib/util/clock.h"
#include "src/lib/util/sleeper.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

using std::chrono::milliseconds;
using std::chrono::steady_clock;

namespace cobalt::util {

TEST(SleeperTest, BasicFakeSleeper) {
  FakeSleeper fake_sleeper;
  for (auto i = 42; i < 45; i++) {
    fake_sleeper.sleep_for(milliseconds(i));
    EXPECT_EQ(milliseconds(i), fake_sleeper.last_sleep_duration());
  }
  fake_sleeper.Reset();
  EXPECT_EQ(milliseconds(0), fake_sleeper.last_sleep_duration());
  fake_sleeper.sleep_for(milliseconds(45));
  EXPECT_EQ(milliseconds(45), fake_sleeper.last_sleep_duration());
}

TEST(SleeperTest, FakeSleeperWithSteadyClock) {
  FakeSleeper fake_sleeper;
  IncrementingSteadyClock clock;
  clock.set_time(steady_clock::time_point(milliseconds(100)));
  fake_sleeper.set_incrementing_steady_clock(&clock);
  fake_sleeper.sleep_for(milliseconds(17));
  EXPECT_EQ(clock.peek_now(), steady_clock::time_point(milliseconds(117)));
}

TEST(SleeperTest, FakeSleeperWithCallback) {
  FakeSleeper fake_sleeper;
  milliseconds sleep_duration(0);
  fake_sleeper.set_callback([&sleep_duration](const milliseconds& d){sleep_duration=d;});
  fake_sleeper.sleep_for(milliseconds(17));
  EXPECT_EQ(sleep_duration,milliseconds(17));
}

}  // namespace cobalt::util

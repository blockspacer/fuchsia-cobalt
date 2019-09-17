// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/uploader/upload_scheduler.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::encoder {

TEST(UploadScheduler, NoBackoff) {
  auto scheduler = UploadScheduler(std::chrono::hours(1), std::chrono::seconds(0));

  // Interval stays at 3600 seconds forever
  EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(3600));
  EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(3600));
  EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(3600));
  EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(3600));
}

TEST(UploadScheduler, QuickBackoff) {
  auto scheduler =
      UploadScheduler(std::chrono::hours(1), std::chrono::seconds(0), std::chrono::minutes(10));

  auto expected_seconds = {600, 1200, 2400, 3600};
  for (auto seconds : expected_seconds) {
    EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(seconds));
  }

  // Interval maxes out at 3600 seconds (1 hour)
  EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(3600));
  EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(3600));
  EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(3600));
  EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(3600));
}

TEST(UploadScheduler, LongBackoff) {
  auto scheduler =
      UploadScheduler(std::chrono::hours(1), std::chrono::seconds(0), std::chrono::seconds(3));

  // Backoff should double every call to Interval, until the interval is >= 1
  // hour.
  auto expected_seconds = {3, 6, 12, 24, 48, 96, 192, 384, 768, 1536, 3072, 3600};
  for (auto seconds : expected_seconds) {
    EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(seconds));
  }

  // Interval maxes out at 3600 seconds (1 hour)
  EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(3600));
  EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(3600));
  EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(3600));
  EXPECT_EQ(scheduler.Interval(), std::chrono::seconds(3600));
}

}  // namespace cobalt::encoder

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/local_aggregation/aggregation_utils.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::local_aggregation {

TEST(AggregationUtilsTest, MakeDayWindow) {
  const int kSevenDays = 7;
  auto window = MakeDayWindow(kSevenDays);
  ASSERT_EQ(OnDeviceAggregationWindow::kDays, window.units_case());
  EXPECT_EQ(kSevenDays, window.days());
}

TEST(AggregationUtilsTest, MakeHourWindow) {
  const int kOneHour = 1;
  auto window = MakeHourWindow(kOneHour);
  ASSERT_EQ(OnDeviceAggregationWindow::kHours, window.units_case());
  EXPECT_EQ(kOneHour, window.hours());
}

}  // namespace cobalt::local_aggregation

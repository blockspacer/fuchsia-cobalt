// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/local_aggregation/aggregation_utils.h"

#include <optional>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::local_aggregation {

using logger::kOK;

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

/*************************************GetUpdatedAggregate*****************************************/

/*** SUM tests ***/

TEST(AggregationUtilsTest, GetUpdatedAggregateSumNoPriorValue) {
  const int64_t kNewValue = 4;
  auto [status, updated_value] =
      GetUpdatedAggregate(ReportDefinition::SUM, std::nullopt, kNewValue);

  EXPECT_EQ(kOK, status);
  EXPECT_EQ(kNewValue, updated_value);
}

TEST(AggregationUtilsTest, GetUpdatedAggregateSumWithPriorValue) {
  const int64_t kStoredAggregate = 45;
  const int64_t kNewValue = 4;
  auto [status, updated_value] =
      GetUpdatedAggregate(ReportDefinition::SUM, kStoredAggregate, kNewValue);

  EXPECT_EQ(kOK, status);
  EXPECT_EQ(kNewValue + kStoredAggregate, updated_value);
}

/*** MAX tests ***/

TEST(AggregationUtilsTest, GetUpdatedAggregateMaxNoPriorValue) {
  const int64_t kNewValue = 4;
  auto [status, updated_value] =
      GetUpdatedAggregate(ReportDefinition::MAX, std::nullopt, kNewValue);

  EXPECT_EQ(kOK, status);
  EXPECT_EQ(kNewValue, updated_value);
}

// Check that the function does not update the aggregate if it is larger than the new value.
TEST(AggregationUtilsTest, GetUpdatedAggregateMaxWithLargerPriorValue) {
  const int64_t kStoredAggregate = 45;
  const int64_t kNewValue = 4;
  auto [status, updated_value] =
      GetUpdatedAggregate(ReportDefinition::MAX, kStoredAggregate, kNewValue);

  EXPECT_EQ(kOK, status);
  EXPECT_EQ(kStoredAggregate, updated_value);
}

// Check that the function updates the aggregate if it is smaller than the new value.
TEST(AggregationUtilsTest, GetUpdatedAggregateMaxWithSmallerPriorValue) {
  const int64_t kStoredAggregate = 2;
  const int64_t kNewValue = 4;
  auto [status, updated_value] =
      GetUpdatedAggregate(ReportDefinition::MAX, kStoredAggregate, kNewValue);

  EXPECT_EQ(kOK, status);
  EXPECT_EQ(kNewValue, updated_value);
}

/*** MIN tests ***/

TEST(AggregationUtilsTest, GetUpdatedAggregateMinNoPriorValue) {
  const int64_t kNewValue = 4;
  auto [status, updated_value] =
      GetUpdatedAggregate(ReportDefinition::MIN, std::nullopt, kNewValue);

  EXPECT_EQ(kOK, status);
  EXPECT_EQ(kNewValue, updated_value);
}

// Check that the function does not update the aggregate if it is smaller than the new value.
TEST(AggregationUtilsTest, GetUpdatedAggregateMinWithSmallerPriorValue) {
  const int64_t kStoredAggregate = 2;
  const int64_t kNewValue = 4;
  auto [status, updated_value] =
      GetUpdatedAggregate(ReportDefinition::MIN, kStoredAggregate, kNewValue);

  EXPECT_EQ(kOK, status);
  EXPECT_EQ(kStoredAggregate, updated_value);
}

// Check that the function updates the aggregate if it is larger than the new value.
TEST(AggregationUtilsTest, GetUpdatedAggregateMinWithLargerPriorValue) {
  const int64_t kStoredAggregate = 45;
  const int64_t kNewValue = 4;
  auto [status, updated_value] =
      GetUpdatedAggregate(ReportDefinition::MIN, kStoredAggregate, kNewValue);

  EXPECT_EQ(kOK, status);
  EXPECT_EQ(kNewValue, updated_value);
}

}  // namespace cobalt::local_aggregation

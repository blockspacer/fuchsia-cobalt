// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "algorithms/rappor/rappor_config_helper.h"

#include "./logging.h"
#include "config/metric_definition.pb.h"
#include "config/report_definition.pb.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace rappor {

TEST(RapporConfigHelperTest, ProbBitFlip) {
  ReportDefinition report_definition;
  EXPECT_EQ(
      RapporConfigHelper::kInvalidProbability,
      RapporConfigHelper::ProbBitFlip(report_definition, "my.test.metric"));

  report_definition.set_local_privacy_noise_level(ReportDefinition::NONE);
  EXPECT_EQ(0.0f, RapporConfigHelper::ProbBitFlip(report_definition,
                                                  "my.test.metric"));

  report_definition.set_local_privacy_noise_level(ReportDefinition::SMALL);
  EXPECT_EQ(0.01f, RapporConfigHelper::ProbBitFlip(report_definition,
                                                   "my.test.metric"));

  report_definition.set_local_privacy_noise_level(ReportDefinition::MEDIUM);
  EXPECT_EQ(0.1f, RapporConfigHelper::ProbBitFlip(report_definition,
                                                  "my.test.metric"));

  report_definition.set_local_privacy_noise_level(ReportDefinition::LARGE);
  EXPECT_EQ(0.25f, RapporConfigHelper::ProbBitFlip(report_definition,
                                                   "my.test.metric"));
}

TEST(RapporConfigHelperTest, BasicRapporNumCategories) {
  MetricDefinition metric_definition;
  EXPECT_EQ(1u,
            RapporConfigHelper::BasicRapporNumCategories(metric_definition));

  metric_definition.set_max_event_code(0);
  EXPECT_EQ(1u,
            RapporConfigHelper::BasicRapporNumCategories(metric_definition));

  metric_definition.set_max_event_code(10);
  EXPECT_EQ(11u,
            RapporConfigHelper::BasicRapporNumCategories(metric_definition));
}

TEST(RapporConfigHelperTest, StringRapporNumCohorts) {
  ReportDefinition report_definition;
  EXPECT_EQ(50u, RapporConfigHelper::StringRapporNumCohorts(report_definition));

  report_definition.set_expected_population_size(99);
  EXPECT_EQ(5u, RapporConfigHelper::StringRapporNumCohorts(report_definition));

  report_definition.set_expected_population_size(100);
  EXPECT_EQ(10u, RapporConfigHelper::StringRapporNumCohorts(report_definition));

  report_definition.set_expected_population_size(999);
  EXPECT_EQ(10u, RapporConfigHelper::StringRapporNumCohorts(report_definition));

  report_definition.set_expected_population_size(1000);
  EXPECT_EQ(50u, RapporConfigHelper::StringRapporNumCohorts(report_definition));

  report_definition.set_expected_population_size(9999);
  EXPECT_EQ(50u, RapporConfigHelper::StringRapporNumCohorts(report_definition));

  report_definition.set_expected_population_size(10000);
  EXPECT_EQ(100u,
            RapporConfigHelper::StringRapporNumCohorts(report_definition));

  report_definition.set_expected_population_size(10001);
  EXPECT_EQ(100u,
            RapporConfigHelper::StringRapporNumCohorts(report_definition));

  report_definition.set_expected_population_size(100000);
  EXPECT_EQ(100u,
            RapporConfigHelper::StringRapporNumCohorts(report_definition));
}

TEST(RapporConfigHelperTest, StringRapporNumBloomBits) {
  ReportDefinition report_definition;
  EXPECT_EQ(32u,
            RapporConfigHelper::StringRapporNumBloomBits(report_definition));

  report_definition.set_expected_string_set_size(99);
  EXPECT_EQ(8u,
            RapporConfigHelper::StringRapporNumBloomBits(report_definition));

  report_definition.set_expected_string_set_size(100);
  EXPECT_EQ(16u,
            RapporConfigHelper::StringRapporNumBloomBits(report_definition));

  report_definition.set_expected_string_set_size(999);
  EXPECT_EQ(16u,
            RapporConfigHelper::StringRapporNumBloomBits(report_definition));

  report_definition.set_expected_string_set_size(1000);
  EXPECT_EQ(64u,
            RapporConfigHelper::StringRapporNumBloomBits(report_definition));

  report_definition.set_expected_string_set_size(9999);
  EXPECT_EQ(64u,
            RapporConfigHelper::StringRapporNumBloomBits(report_definition));

  report_definition.set_expected_string_set_size(10000);
  EXPECT_EQ(128u,
            RapporConfigHelper::StringRapporNumBloomBits(report_definition));

  report_definition.set_expected_string_set_size(10001);
  EXPECT_EQ(128u,
            RapporConfigHelper::StringRapporNumBloomBits(report_definition));

  report_definition.set_expected_string_set_size(100000);
  EXPECT_EQ(128u,
            RapporConfigHelper::StringRapporNumBloomBits(report_definition));
}
}  // namespace rappor
}  // namespace cobalt

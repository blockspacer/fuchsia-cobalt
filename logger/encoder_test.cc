// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/encoder.h"

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./gtest.h"
#include "./logging.h"
#include "./observation.pb.h"
#include "./observation2.pb.h"
#include "encoder/fake_system_data.h"
#include "logger/project_context.h"
#include "logger/status.h"

namespace cobalt {

using encoder::ClientSecret;
using encoder::FakeSystemData;
using encoder::SystemDataInterface;
using google::protobuf::RepeatedPtrField;
using google::protobuf::util::MessageDifferencer;

namespace logger {

namespace {
static const uint32_t kCustomerId = 1;
static const uint32_t kProjectId = 1;
static const char kCustomerName[] = "Fuchsia";
static const char kProjectName[] = "Cobalt";

static const char kMetricDefinitions[] = R"(
metric {
  metric_name: "ErrorOccurred"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 1
  max_event_code: 100
  reports: {
    report_name: "ErrorCountsByType"
    id: 123
    report_type: SIMPLE_OCCURRENCE_COUNT
    local_privacy_noise_level: SMALL
  }
}

metric {
  metric_name: "ReadCacheHits"
  metric_type: EVENT_COUNT
  customer_id: 1
  project_id: 1
  id: 2
  reports: {
    report_name: "ReadCacheHitCounts"
    id: 124
    report_type: EVENT_COMPONENT_OCCURRENCE_COUNT
    system_profile_field: OS
  }
}

metric {
  metric_name: "FileSystemWriteTimes"
  metric_type: INT_HISTOGRAM
  int_buckets: {
    linear: {
      floor: 0
      num_buckets: 10
      step_size: 1
    }
  }
  customer_id: 1
  project_id: 1
  id: 6
  reports: {
    report_name: "FileSystemWriteTimes_Histogram"
    id: 151
    report_type: INT_RANGE_HISTOGRAM
    system_profile_field: OS
    system_profile_field: ARCH
  }
}

metric {
  metric_name: "ModuleDownloads"
  metric_type: STRING_USED
  customer_id: 1
  project_id: 1
  id: 7
  reports: {
    report_name: "ModuleDownloads_HeavyHitters"
    id: 161
    report_type: HIGH_FREQUENCY_STRING_COUNTS
    local_privacy_noise_level: SMALL
    expected_population_size: 20000
    expected_string_set_size: 10000
  }
  reports: {
    report_name: "ModuleDownloads_WithThreshold"
    id: 261
    report_type: STRING_COUNTS_WITH_THRESHOLD
    threshold: 200
  }
}

metric {
  metric_name: "ModuleInstalls"
  metric_type: CUSTOM
  customer_id: 1
  project_id: 1
  id: 8
  reports: {
    report_name: "ModuleInstalls_DetailedData"
    id: 125
    report_type: CUSTOM_RAW_DUMP
    system_profile_field: OS
    system_profile_field: ARCH
  }
}

metric {
  metric_name: "DeviceBoots"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 9
  max_event_code: 1
  reports: {
    report_name: "DeviceBoots_UniqueDevices"
    id: 91
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: SMALL
    window_size: 1
    system_profile_field: OS
    system_profile_field: ARCH
  }
}

)";

bool PopulateMetricDefinitions(MetricDefinitions* metric_definitions) {
  google::protobuf::TextFormat::Parser parser;
  return parser.ParseFromString(kMetricDefinitions, metric_definitions);
}

HistogramPtr NewHistogram(std::vector<uint32_t> indices,
                          std::vector<uint32_t> counts) {
  CHECK(indices.size() == counts.size());
  HistogramPtr histogram =
      std::make_unique<RepeatedPtrField<HistogramBucket>>();
  for (auto i = 0u; i < indices.size(); i++) {
    auto* bucket = histogram->Add();
    bucket->set_index(indices[i]);
    bucket->set_count(counts[i]);
  }
  return histogram;
}

EventValuesPtr NewCustomEvent(std::vector<std::string> dimension_names,
                              std::vector<CustomDimensionValue> values) {
  CHECK(dimension_names.size() == values.size());
  EventValuesPtr custom_event = std::make_unique<
      google::protobuf::Map<std::string, CustomDimensionValue>>();
  for (auto i = 0u; i < values.size(); i++) {
    (*custom_event)[dimension_names[i]] = values[i];
  }
  return custom_event;
}

void CheckSystemProfile(const Encoder::Result& result,
                        SystemProfile::OS expected_os,
                        SystemProfile::ARCH expected_arch,
                        const std::string& expected_board_name,
                        const std::string& expected_product) {
  EXPECT_TRUE(result.metadata->has_system_profile());
  EXPECT_EQ(expected_os, result.metadata->system_profile().os());
  EXPECT_EQ(expected_arch, result.metadata->system_profile().arch());
  EXPECT_EQ(expected_board_name,
            result.metadata->system_profile().board_name());
  EXPECT_EQ(expected_product, result.metadata->system_profile().product_name());
}

void CheckDefaultSystemProfile(const Encoder::Result& result) {
  return CheckSystemProfile(result, SystemProfile::UNKNOWN_OS,
                            SystemProfile::UNKNOWN_ARCH, "Testing Board",
                            "Testing Product");
}

void CheckResult(const Encoder::Result& result, uint32_t expected_metric_id,
                 uint32_t expected_report_id, uint32_t expected_day_index) {
  EXPECT_EQ(kOK, result.status);
  EXPECT_EQ(kCustomerId, result.metadata->customer_id());
  EXPECT_EQ(kProjectId, result.metadata->project_id());
  EXPECT_EQ(expected_metric_id, result.metadata->metric_id());
  EXPECT_EQ(expected_report_id, result.metadata->report_id());
  EXPECT_EQ(result.observation->random_id().size(), 8u);
  EXPECT_EQ(expected_day_index, result.metadata->day_index());
}

}  // namespace

class EncoderTest : public ::testing::Test {
 protected:
  void SetUp() {
    auto metric_definitions = std::make_unique<MetricDefinitions>();
    ASSERT_TRUE(PopulateMetricDefinitions(metric_definitions.get()));
    project_context_.reset(new ProjectContext(kCustomerId, kProjectId,
                                              kCustomerName, kProjectName,
                                              std::move(metric_definitions)));
    system_data_.reset(new FakeSystemData());
    encoder_.reset(
        new Encoder(ClientSecret::GenerateNewSecret(), system_data_.get()));
  }

  std::pair<const MetricDefinition*, const ReportDefinition*>
  GetMetricAndReport(const std::string& metric_name,
                     const std::string& report_name) {
    const auto* metric = project_context_->GetMetric(metric_name);
    CHECK(metric) << "No such metric: " << metric_name;
    const ReportDefinition* report = nullptr;
    for (const auto& rept : metric->reports()) {
      if (rept.report_name() == report_name) {
        report = &rept;
        break;
      }
    }
    CHECK(report) << "No such report: " << report_name;
    return {metric, report};
  }

  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<ProjectContext> project_context_;
  std::unique_ptr<SystemDataInterface> system_data_;
};

TEST_F(EncoderTest, EncodeBasicRapporObservation) {
  const char kMetricName[] = "ErrorOccurred";
  const char kReportName[] = "ErrorCountsByType";
  const uint32_t kExpectedMetricId = 1;
  const uint32_t kExpectedReportId = 123;

  uint32_t day_index = 111;
  uint32_t value_index = 9;
  uint32_t num_categories = 8;
  auto pair = GetMetricAndReport(kMetricName, kReportName);
  // This should fail with kInvalidArguments because 9 > 8.
  auto result = encoder_->EncodeBasicRapporObservation(
      project_context_->RefMetric(pair.first), pair.second, day_index,
      value_index, num_categories);
  EXPECT_EQ(kInvalidArguments, result.status);

  // This should fail with kInvalidConfig because num_categories is too large.
  num_categories = 999999;
  result = encoder_->EncodeBasicRapporObservation(
      project_context_->RefMetric(pair.first), pair.second, day_index,
      value_index, num_categories);
  EXPECT_EQ(kInvalidConfig, result.status);

  // If we use the wrong report, it won't have local_privacy_noise_level
  // set and we should get InvalidConfig
  num_categories = 128;
  value_index = 10;
  pair = GetMetricAndReport("ReadCacheHits", "ReadCacheHitCounts");
  result = encoder_->EncodeBasicRapporObservation(
      project_context_->RefMetric(pair.first), pair.second, day_index,
      value_index, num_categories);
  EXPECT_EQ(kInvalidConfig, result.status);

  // Finally we pass all valid parameters and the operation should succeed.
  pair = GetMetricAndReport(kMetricName, kReportName);
  result = encoder_->EncodeBasicRapporObservation(
      project_context_->RefMetric(pair.first), pair.second, day_index,
      value_index, num_categories);
  CheckResult(result, kExpectedMetricId, kExpectedReportId, day_index);
  CheckDefaultSystemProfile(result);
  ASSERT_TRUE(result.observation->has_basic_rappor());
  EXPECT_FALSE(result.observation->basic_rappor().data().empty());
}

TEST_F(EncoderTest, EncodeIntegerEventObservation) {
  const char kMetricName[] = "ReadCacheHits";
  const char kReportName[] = "ReadCacheHitCounts";
  const uint32_t kExpectedMetricId = 2;
  const uint32_t kExpectedReportId = 124;
  const char kComponent[] = "My Component";
  const uint32_t kValue = 314159;
  const uint32_t kDayIndex = 111;
  const uint32_t kEventCode = 9;

  auto pair = GetMetricAndReport(kMetricName, kReportName);
  auto result = encoder_->EncodeIntegerEventObservation(
      project_context_->RefMetric(pair.first), pair.second, kDayIndex,
      kEventCode, kComponent, kValue);
  CheckResult(result, kExpectedMetricId, kExpectedReportId, kDayIndex);
  // In the SystemProfile only the OS should be set.
  CheckSystemProfile(result, SystemProfile::FUCHSIA,
                     SystemProfile::UNKNOWN_ARCH, "", "");
  ASSERT_TRUE(result.observation->has_numeric_event());
  const IntegerEventObservation& obs = result.observation->numeric_event();
  EXPECT_EQ(kEventCode, obs.event_code());
  EXPECT_EQ(obs.component_name_hash().size(), 32u);
  EXPECT_EQ(kValue, obs.value());
}

TEST_F(EncoderTest, EncodeHistogramObservation) {
  const char kMetricName[] = "FileSystemWriteTimes";
  const char kReportName[] = "FileSystemWriteTimes_Histogram";
  const uint32_t kExpectedMetricId = 6;
  const uint32_t kExpectedReportId = 151;
  const char kComponent[] = "";
  const uint32_t kDayIndex = 111;
  const uint32_t kEventCode = 9;

  std::vector<uint32_t> indices = {0, 1, 2};
  std::vector<uint32_t> counts = {100, 200, 300};
  auto histogram = NewHistogram(indices, counts);
  auto pair = GetMetricAndReport(kMetricName, kReportName);
  auto result = encoder_->EncodeHistogramObservation(
      project_context_->RefMetric(pair.first), pair.second, kDayIndex,
      kEventCode, kComponent, std::move(histogram));
  CheckResult(result, kExpectedMetricId, kExpectedReportId, kDayIndex);
  // In the SystemProfile only the OS and ARCH should be set.
  CheckSystemProfile(result, SystemProfile::FUCHSIA, SystemProfile::ARM_64, "",
                     "");
  ASSERT_TRUE(result.observation->has_histogram());
  const HistogramObservation& obs = result.observation->histogram();
  EXPECT_EQ(kEventCode, obs.event_code());
  EXPECT_TRUE(obs.component_name_hash().empty());
  EXPECT_EQ(static_cast<size_t>(obs.buckets_size()), indices.size());
  for (auto i = 0u; i < indices.size(); i++) {
    const auto& bucket = obs.buckets(i);
    EXPECT_EQ(bucket.index(), indices[i]);
    EXPECT_EQ(bucket.count(), counts[i]);
  }
}

TEST_F(EncoderTest, EncodeRapporObservation) {
  const char kMetricName[] = "ModuleDownloads";
  const char kReportName[] = "ModuleDownloads_HeavyHitters";
  const uint32_t kExpectedMetricId = 7;
  const uint32_t kExpectedReportId = 161;
  const uint32_t kDayIndex = 111;
  auto pair = GetMetricAndReport(kMetricName, kReportName);
  auto result = encoder_->EncodeRapporObservation(
      project_context_->RefMetric(pair.first), pair.second, kDayIndex,
      "Supercalifragilistic");
  CheckResult(result, kExpectedMetricId, kExpectedReportId, kDayIndex);
  CheckDefaultSystemProfile(result);
  ASSERT_TRUE(result.observation->has_string_rappor());
  const RapporObservation& obs = result.observation->string_rappor();
  EXPECT_LT(obs.cohort(), 256u);
  // Expect 128 Bloom bits and so 16 bytes.
  EXPECT_EQ(obs.data().size(), 16u);

  // If we use the wrong report, it won't have local_privacy_noise_level
  // set and we should get InvalidConfig
  pair = GetMetricAndReport(kMetricName, "ModuleDownloads_WithThreshold");
  result = encoder_->EncodeRapporObservation(
      project_context_->RefMetric(pair.first), pair.second, kDayIndex,
      "Supercalifragilistic");
  EXPECT_EQ(kInvalidConfig, result.status);
}

TEST_F(EncoderTest, EncodeCustomObservation) {
  const char kMetricName[] = "ModuleInstalls";
  const char kReportName[] = "ModuleInstalls_DetailedData";
  const uint32_t kExpectedMetricId = 8;
  const uint32_t kExpectedReportId = 125;
  const uint32_t kDayIndex = 111;

  CustomDimensionValue module_value, number_value;
  module_value.set_string_value("gmail");
  number_value.set_int_value(3);
  std::vector<std::string> dimension_names = {"module", "number"};
  std::vector<CustomDimensionValue> values = {module_value, number_value};
  auto custom_event = NewCustomEvent(dimension_names, values);
  auto pair = GetMetricAndReport(kMetricName, kReportName);

  auto result = encoder_->EncodeCustomObservation(
      project_context_->RefMetric(pair.first), pair.second, kDayIndex,
      std::move(custom_event));
  CheckResult(result, kExpectedMetricId, kExpectedReportId, kDayIndex);
  // In the SystemProfile only the OS and ARCH should be set.
  CheckSystemProfile(result, SystemProfile::FUCHSIA, SystemProfile::ARM_64, "",
                     "");
  ASSERT_TRUE(result.observation->has_custom());
  const CustomObservation& obs = result.observation->custom();
  for (auto i = 0u; i < values.size(); i++) {
    auto obs_dimension = obs.values().at(dimension_names[i]);
    EXPECT_TRUE(MessageDifferencer::Equals(obs_dimension, values[i]));
  }
}

TEST_F(EncoderTest, EncodeUniqueActivesObservation) {
  const char kMetricName[] = "DeviceBoots";
  const char kReportName[] = "DeviceBoots_UniqueDevices";
  const uint32_t kExpectedMetricId = 9;
  const uint32_t kExpectedReportId = 91;
  const uint32_t kDayIndex = 111;
  const uint32_t kEventCode = 0;
  const uint32_t kWindowSize = 1;
  auto pair = GetMetricAndReport(kMetricName, kReportName);

  // Encode a valid UniqueActivesObservation of activity.
  auto result_active = encoder_->EncodeUniqueActivesObservation(
      project_context_->RefMetric(pair.first), pair.second, kDayIndex,
      kEventCode, true, kWindowSize);
  CheckResult(result_active, kExpectedMetricId, kExpectedReportId, kDayIndex);
  // In the SystemProfile only the OS and ARCH should be set.
  CheckSystemProfile(result_active, SystemProfile::FUCHSIA,
                     SystemProfile::ARM_64, "", "");
  ASSERT_TRUE(result_active.observation->has_unique_actives());
  EXPECT_EQ(kWindowSize,
            result_active.observation->unique_actives().window_size());
  EXPECT_EQ(kEventCode,
            result_active.observation->unique_actives().event_code());
  ASSERT_TRUE(
      result_active.observation->unique_actives().has_basic_rappor_obs());
  EXPECT_EQ(1u, result_active.observation->unique_actives()
                    .basic_rappor_obs()
                    .data()
                    .size());

  // Encode a valid UniqueActivesObservation of inactivity.
  auto result_inactive = encoder_->EncodeUniqueActivesObservation(
      project_context_->RefMetric(pair.first), pair.second, kDayIndex,
      kEventCode, false, kWindowSize);
  CheckResult(result_inactive, kExpectedMetricId, kExpectedReportId, kDayIndex);
  // In the SystemProfile only the OS and ARCH should be set.
  CheckSystemProfile(result_inactive, SystemProfile::FUCHSIA,
                     SystemProfile::ARM_64, "", "");
  ASSERT_TRUE(result_inactive.observation->has_unique_actives());
  EXPECT_EQ(kWindowSize,
            result_inactive.observation->unique_actives().window_size());
  EXPECT_EQ(kEventCode,
            result_active.observation->unique_actives().event_code());
  ASSERT_TRUE(
      result_inactive.observation->unique_actives().has_basic_rappor_obs());
  EXPECT_EQ(1u, result_active.observation->unique_actives()
                    .basic_rappor_obs()
                    .data()
                    .size());
}

}  // namespace logger
}  // namespace cobalt

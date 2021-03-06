// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logger/encoder.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/repeated_field.h>

#include "src/local_aggregation/aggregation_utils.h"
#include "src/logger/project_context.h"
#include "src/logger/project_context_factory.h"
#include "src/logger/status.h"
#include "src/logger/test_registries/encoder_test_registry.cb.h"
#include "src/logging.h"
#include "src/pb/observation.pb.h"
#include "src/pb/observation2.pb.h"
#include "src/registry/packed_event_codes.h"
#include "src/system_data/fake_system_data.h"
#include "third_party/abseil-cpp/absl/strings/escaping.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

using google::protobuf::RepeatedPtrField;
using local_aggregation::MakeDayWindow;
using local_aggregation::MakeHourWindow;
using system_data::ClientSecret;
using system_data::FakeSystemData;
using system_data::SystemDataInterface;

namespace logger {

namespace {

constexpr uint32_t kCustomerId = 1;
constexpr uint32_t kProjectId = 1;
constexpr uint32_t kDayIndex = 111;
constexpr uint32_t kAggregationDays = 7;
constexpr uint32_t kValue = 314159;

bool PopulateCobaltRegistry(CobaltRegistry* cobalt_registry) {
  std::string cobalt_registry_bytes;
  if (!absl::Base64Unescape(kCobaltRegistryBase64, &cobalt_registry_bytes)) {
    return false;
  }
  return cobalt_registry->ParseFromString(cobalt_registry_bytes);
}

HistogramPtr NewHistogram(std::vector<uint32_t> indices, std::vector<uint32_t> counts) {
  CHECK(indices.size() == counts.size());
  HistogramPtr histogram = std::make_unique<RepeatedPtrField<HistogramBucket>>();
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
  EventValuesPtr custom_event =
      std::make_unique<google::protobuf::Map<std::string, CustomDimensionValue>>();
  for (auto i = 0u; i < values.size(); i++) {
    (*custom_event)[dimension_names[i]] = values[i];
  }
  return custom_event;
}

void CheckSystemProfile(const Encoder::Result& result, SystemProfile::OS expected_os,
                        SystemProfile::ARCH expected_arch, const std::string& expected_board_name,
                        const std::string& expected_product) {
  EXPECT_TRUE(result.metadata->has_system_profile());
  EXPECT_EQ(expected_os, result.metadata->system_profile().os());
  EXPECT_EQ(expected_arch, result.metadata->system_profile().arch());
  EXPECT_EQ(expected_board_name, result.metadata->system_profile().board_name());
  EXPECT_EQ(expected_product, result.metadata->system_profile().product_name());
}

void CheckDefaultSystemProfile(const Encoder::Result& result) {
  return CheckSystemProfile(result, SystemProfile::UNKNOWN_OS, SystemProfile::UNKNOWN_ARCH,
                            "Testing Board", "Testing Product");
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
  void SetUp() override {
    auto cobalt_registry = std::make_unique<CobaltRegistry>();
    ASSERT_TRUE(PopulateCobaltRegistry(cobalt_registry.get()));
    ProjectContextFactory project_context_factory(std::move(cobalt_registry));
    ASSERT_TRUE(project_context_factory.is_single_project());
    project_context_ = project_context_factory.TakeSingleProjectContext();
    system_data_ = std::make_unique<FakeSystemData>();
    encoder_ = std::make_unique<Encoder>(ClientSecret::GenerateNewSecret(), system_data_.get());
  }

  std::pair<const MetricDefinition*, const ReportDefinition*> GetMetricAndReport(
      const std::string& metric_name, const std::string& report_name) {
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

 private:
  std::unique_ptr<SystemDataInterface> system_data_;
};

TEST_F(EncoderTest, EncodeBasicRapporObservation) {
  const char kMetricName[] = "ErrorOccurred";
  const char kReportName[] = "ErrorCountsByType";
  const uint32_t kExpectedMetricId = 1;

  auto pair = GetMetricAndReport(kMetricName, kReportName);
  const uint32_t day_index = 111;
  {
    const uint32_t value_index = 9;
    const uint32_t num_categories = 8;
    // This should fail with kInvalidArguments because 9 > 8.
    auto result =
        encoder_->EncodeBasicRapporObservation(project_context_->RefMetric(pair.first), pair.second,
                                               day_index, value_index, num_categories);
    EXPECT_EQ(kInvalidArguments, result.status);
  }

  {
    // This should fail with kInvalidConfig because num_categories is too large.
    const uint32_t value_index = 9;
    const uint32_t num_categories = 999999;
    auto result =
        encoder_->EncodeBasicRapporObservation(project_context_->RefMetric(pair.first), pair.second,
                                               day_index, value_index, num_categories);
    EXPECT_EQ(kInvalidConfig, result.status);
  }

  {
    // If we use the wrong report, it won't have local_privacy_noise_level
    // set and we should get InvalidConfig
    const uint32_t num_categories = 128;
    const uint32_t value_index = 10;
    pair = GetMetricAndReport("ReadCacheHits", "ReadCacheHitCounts");
    auto result =
        encoder_->EncodeBasicRapporObservation(project_context_->RefMetric(pair.first), pair.second,
                                               day_index, value_index, num_categories);
    EXPECT_EQ(kInvalidConfig, result.status);

    // Finally we pass all valid parameters and the operation should succeed.
    pair = GetMetricAndReport(kMetricName, kReportName);
    result =
        encoder_->EncodeBasicRapporObservation(project_context_->RefMetric(pair.first), pair.second,
                                               day_index, value_index, num_categories);
    CheckResult(result, kExpectedMetricId, kErrorOccurredErrorCountsByTypeReportId, day_index);
    CheckDefaultSystemProfile(result);
    ASSERT_TRUE(result.observation->has_basic_rappor());
    EXPECT_FALSE(result.observation->basic_rappor().data().empty());
  }
}

TEST_F(EncoderTest, EncodeIntegerEventObservation) {
  const char kMetricName[] = "ReadCacheHits";
  const char kReportName[] = "ReadCacheHitCounts";
  const uint32_t kExpectedMetricId = 2;
  const char kComponent[] = "My Component";
  const uint32_t kEventCode = 9;
  google::protobuf::RepeatedField<uint32_t> event_codes;
  *event_codes.Add() = kEventCode;

  auto pair = GetMetricAndReport(kMetricName, kReportName);
  auto result =
      encoder_->EncodeIntegerEventObservation(project_context_->RefMetric(pair.first), pair.second,
                                              kDayIndex, event_codes, kComponent, kValue);
  CheckResult(result, kExpectedMetricId, kReadCacheHitsReadCacheHitCountsReportId, kDayIndex);
  // In the SystemProfile only the OS should be set.
  CheckSystemProfile(result, SystemProfile::FUCHSIA, SystemProfile::UNKNOWN_ARCH, "", "");
  ASSERT_TRUE(result.observation->has_numeric_event());
  const IntegerEventObservation& obs = result.observation->numeric_event();
  EXPECT_EQ(kEventCode, obs.event_code());
  EXPECT_EQ(obs.component_name_hash().size(), 32u);
  EXPECT_EQ(kValue, obs.value());
}

TEST_F(EncoderTest, MultipleEventCodes) {
  const char kMetricName[] = "MultiEventCodeTest";
  const char kReportName[] = "MultiEventCodeCounts";
  const uint32_t kExpectedMetricId = 11;
  const char kComponent[] = "My Component";
  const uint32_t kEventCode1 = 7;
  const uint32_t kEventCode2 = 5;
  google::protobuf::RepeatedField<uint32_t> event_codes;
  *event_codes.Add() = kEventCode1;
  *event_codes.Add() = kEventCode2;

  auto pair = GetMetricAndReport(kMetricName, kReportName);
  auto result =
      encoder_->EncodeIntegerEventObservation(project_context_->RefMetric(pair.first), pair.second,
                                              kDayIndex, event_codes, kComponent, kValue);
  CheckResult(result, kExpectedMetricId, kMultiEventCodeTestMultiEventCodeCountsReportId,
              kDayIndex);
  // In the SystemProfile only the OS should be set.
  CheckSystemProfile(result, SystemProfile::FUCHSIA, SystemProfile::UNKNOWN_ARCH, "", "");
  ASSERT_TRUE(result.observation->has_numeric_event());
  const IntegerEventObservation& obs = result.observation->numeric_event();
  auto codes = config::UnpackEventCodes(obs.event_code());
  EXPECT_EQ(kEventCode1, codes[0]);
  EXPECT_EQ(kEventCode2, codes[1]);
  EXPECT_EQ(obs.component_name_hash().size(), 32u);
  EXPECT_EQ(kValue, obs.value());
}

TEST_F(EncoderTest, EncodeHistogramObservation) {
  const char kMetricName[] = "FileSystemWriteTimes";
  const char kReportName[] = "FileSystemWriteTimes_Histogram";
  const uint32_t kExpectedMetricId = 6;
  const std::string kComponent;
  const uint32_t kEventCode = 9;
  google::protobuf::RepeatedField<uint32_t> event_codes;
  *event_codes.Add() = kEventCode;

  const std::vector<uint32_t> indices = {0, 1, 2};
  const std::vector<uint32_t> counts = {100, 200, 300};
  auto histogram = NewHistogram(indices, counts);
  auto pair = GetMetricAndReport(kMetricName, kReportName);
  auto result = encoder_->EncodeHistogramObservation(project_context_->RefMetric(pair.first),
                                                     pair.second, kDayIndex, event_codes,
                                                     kComponent, std::move(histogram));
  CheckResult(result, kExpectedMetricId, kFileSystemWriteTimesFileSystemWriteTimesHistogramReportId,
              kDayIndex);
  // In the SystemProfile only the OS and ARCH should be set.
  CheckSystemProfile(result, SystemProfile::FUCHSIA, SystemProfile::ARM_64, "", "");
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

TEST_F(EncoderTest, EncodeCustomObservation) {
  const char kMetricName[] = "ModuleInstalls";
  const char kReportName[] = "ModuleInstalls_DetailedData";
  const uint32_t kExpectedMetricId = 8;

  CustomDimensionValue module_value, number_value;
  module_value.set_string_value("gmail");
  number_value.set_int_value(3);
  std::vector<std::string> dimension_names = {"module", "number"};
  std::vector<CustomDimensionValue> values = {module_value, number_value};
  auto custom_event = NewCustomEvent(dimension_names, values);
  auto pair = GetMetricAndReport(kMetricName, kReportName);

  auto result = encoder_->EncodeCustomObservation(project_context_->RefMetric(pair.first),
                                                  pair.second, kDayIndex, std::move(custom_event));
  CheckResult(result, kExpectedMetricId, kModuleInstallsModuleInstallsDetailedDataReportId,
              kDayIndex);
  // In the SystemProfile only the OS and ARCH should be set.
  CheckSystemProfile(result, SystemProfile::FUCHSIA, SystemProfile::ARM_64, "", "");
  ASSERT_TRUE(result.observation->has_custom());
  const CustomObservation& obs = result.observation->custom();
  for (auto i = 0u; i < values.size(); i++) {
    auto obs_dimension = obs.values().at(dimension_names[i]);
    EXPECT_EQ(obs_dimension.SerializeAsString(), values[i].SerializeAsString());
  }
}

TEST_F(EncoderTest, EncodeUniqueActivesObservation) {
  const char kMetricName[] = "DeviceBoots";
  const char kReportName[] = "DeviceBoots_UniqueDevices";
  const uint32_t kExpectedMetricId = 9;
  const uint32_t kEventCode = 0;
  const int kAggregationHours = 1;
  auto pair = GetMetricAndReport(kMetricName, kReportName);

  // Encode a valid UniqueActivesObservation of activity for a 7-day window.
  auto result_active = encoder_->EncodeUniqueActivesObservation(
      project_context_->RefMetric(pair.first), pair.second, kDayIndex, kEventCode, true,
      MakeDayWindow(kAggregationDays));
  CheckResult(result_active, kExpectedMetricId, kDeviceBootsDeviceBootsUniqueDevicesReportId,
              kDayIndex);
  // In the SystemProfile only the OS and ARCH should be set.
  CheckSystemProfile(result_active, SystemProfile::FUCHSIA, SystemProfile::ARM_64, "", "");
  ASSERT_TRUE(result_active.observation->has_unique_actives());
  auto active_obs = result_active.observation->unique_actives();
  ASSERT_EQ(OnDeviceAggregationWindow::kDays, active_obs.aggregation_window().units_case());
  EXPECT_EQ(kAggregationDays, active_obs.aggregation_window().days());
  EXPECT_EQ(kEventCode, active_obs.event_code());
  ASSERT_TRUE(active_obs.has_basic_rappor_obs());
  EXPECT_EQ(1u, active_obs.basic_rappor_obs().data().size());

  // Encode a valid UniqueActivesObservation of inactivity for a 1-hour window.
  auto result_inactive = encoder_->EncodeUniqueActivesObservation(
      project_context_->RefMetric(pair.first), pair.second, kDayIndex, kEventCode, false,
      MakeHourWindow(kAggregationHours));
  CheckResult(result_inactive, kExpectedMetricId, kDeviceBootsDeviceBootsUniqueDevicesReportId,
              kDayIndex);
  // In the SystemProfile only the OS and ARCH should be set.
  CheckSystemProfile(result_inactive, SystemProfile::FUCHSIA, SystemProfile::ARM_64, "", "");
  ASSERT_TRUE(result_inactive.observation->has_unique_actives());
  auto inactive_obs = result_inactive.observation->unique_actives();
  ASSERT_EQ(OnDeviceAggregationWindow::kHours, inactive_obs.aggregation_window().units_case());
  EXPECT_EQ(kAggregationHours, inactive_obs.aggregation_window().hours());
  EXPECT_EQ(kEventCode, inactive_obs.event_code());
  ASSERT_TRUE(inactive_obs.has_basic_rappor_obs());
  EXPECT_EQ(1u, inactive_obs.basic_rappor_obs().data().size());
}

TEST_F(EncoderTest, EncodePerDeviceNumericObservation) {
  const char kMetricName[] = "ConnectionFailures";
  const char kReportName[] = "ConnectionFailures_PerDeviceCount";
  const uint32_t kExpectedMetricId = 10;
  const char kComponent[] = "Some Component";
  const uint32_t kEventCode = 0;
  const int64_t kCount = 1728;
  auto pair = GetMetricAndReport(kMetricName, kReportName);

  google::protobuf::RepeatedField<uint32_t> event_codes;
  *event_codes.Add() = kEventCode;

  auto result = encoder_->EncodePerDeviceNumericObservation(
      project_context_->RefMetric(pair.first), pair.second, kDayIndex, kComponent, event_codes,
      kCount, MakeDayWindow(kAggregationDays));
  CheckResult(result, kExpectedMetricId,
              kConnectionFailuresConnectionFailuresPerDeviceCountReportId, kDayIndex);
  // In the SystemProfile only the OS and ARCH should be set.
  CheckSystemProfile(result, SystemProfile::FUCHSIA, SystemProfile::ARM_64, "", "");
  ASSERT_TRUE(result.observation->has_per_device_numeric());
  const auto& per_device_numeric_obs = result.observation->per_device_numeric();
  ASSERT_EQ(OnDeviceAggregationWindow::kDays,
            per_device_numeric_obs.aggregation_window().units_case());
  EXPECT_EQ(kAggregationDays, per_device_numeric_obs.aggregation_window().days());
  ASSERT_TRUE(per_device_numeric_obs.has_integer_event_obs());
  const auto& integer_obs = per_device_numeric_obs.integer_event_obs();
  EXPECT_EQ(kEventCode, integer_obs.event_code());
  EXPECT_EQ(32u, integer_obs.component_name_hash().size());
  EXPECT_EQ(kCount, integer_obs.value());
}

TEST_F(EncoderTest, EncodePerDeviceNumericHistogramObservation) {
  const char kMetricName[] = "ConnectionFailures";
  const char kReportName[] = "ConnectionFailures_PerDeviceHistogram";
  const uint32_t kExpectedMetricId = 10;
  const char kComponent[] = "Some Component";
  const uint32_t kEventCode = 0;
  const int64_t kCount = 41;
  const int64_t kExpectedBucket = 5;
  auto pair = GetMetricAndReport(kMetricName, kReportName);

  google::protobuf::RepeatedField<uint32_t> event_codes;
  *event_codes.Add() = kEventCode;

  auto result = encoder_->EncodePerDeviceHistogramObservation(
      project_context_->RefMetric(pair.first), pair.second, kDayIndex, kComponent, event_codes,
      kCount, MakeDayWindow(kAggregationDays));
  CheckResult(result, kExpectedMetricId,
              kConnectionFailuresConnectionFailuresPerDeviceHistogramReportId, kDayIndex);
  // In the SystemProfile only the OS and ARCH should be set.
  CheckSystemProfile(result, SystemProfile::FUCHSIA, SystemProfile::ARM_64, "", "");
  ASSERT_TRUE(result.observation->has_per_device_histogram());
  auto per_device_histogram_obs = result.observation->per_device_histogram();
  ASSERT_EQ(OnDeviceAggregationWindow::kDays,
            per_device_histogram_obs.aggregation_window().units_case());
  EXPECT_EQ(kAggregationDays, per_device_histogram_obs.aggregation_window().days());
  ASSERT_TRUE(result.observation->per_device_histogram().has_histogram());
  auto histogram = result.observation->per_device_histogram().histogram();
  EXPECT_EQ(kEventCode, histogram.event_code());
  EXPECT_EQ(32u, histogram.component_name_hash().size());
  EXPECT_EQ(1, histogram.buckets_size());
  EXPECT_EQ(kExpectedBucket, histogram.buckets(0).index());
  EXPECT_EQ(1, histogram.buckets(0).count());
}

TEST_F(EncoderTest, EncodeReportParticipationObservation) {
  const char kMetricName[] = "ConnectionFailures";
  const char kReportName[] = "ConnectionFailures_PerDeviceCount";
  const uint32_t kExpectedMetricId = 10;
  auto pair = GetMetricAndReport(kMetricName, kReportName);

  auto result = encoder_->EncodeReportParticipationObservation(
      project_context_->RefMetric(pair.first), pair.second, kDayIndex);
  CheckResult(result, kExpectedMetricId,
              kConnectionFailuresConnectionFailuresPerDeviceCountReportId, kDayIndex);
  // In the SystemProfile only the OS and ARCH should be set.
  CheckSystemProfile(result, SystemProfile::FUCHSIA, SystemProfile::ARM_64, "", "");
  ASSERT_TRUE(result.observation->has_report_participation());
}

}  // namespace logger
}  // namespace cobalt

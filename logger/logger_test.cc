// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/logger.h"

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>

#include <chrono>
#include <memory>
#include <string>

#include "./gtest.h"
#include "./observation.pb.h"
#include "./observation2.pb.h"
#include "algorithms/rappor/rappor_encoder.h"
#include "config/encodings.pb.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "logger/encoder.h"
#include "logger/event_aggregator.h"
#include "logger/logger_test_utils.h"
#include "logger/project_context.h"
#include "logger/status.h"
#include "util/clock.h"
#include "util/datetime_util.h"
#include "util/encrypted_message_util.h"

using ::google::protobuf::RepeatedPtrField;
using ::google::protobuf::util::MessageDifferencer;

namespace cobalt {

using encoder::ClientSecret;
using encoder::SystemDataInterface;
using rappor::BasicRapporEncoder;
using util::EncryptedMessageMaker;
using util::IncrementingClock;
using util::MessageDecrypter;
using util::TimeToDayIndex;

namespace logger {

using testing::CheckNumericEventObservations;
using testing::CheckUniqueActivesObservations;
using testing::ExpectedAggregationParams;
using testing::ExpectedUniqueActivesObservations;
using testing::FakeObservationStore;
using testing::FetchAggregatedObservations;
using testing::FetchObservations;
using testing::FetchSingleObservation;
using testing::MakeNullExpectedUniqueActivesObservations;
using testing::PopulateMetricDefinitions;
using testing::TestUpdateRecipient;

namespace {
// Number of seconds in an ideal day
const int kDay = 86400;
// Number of seconds in an ideal year
const int kYear = kDay * 365;

static const uint32_t kCustomerId = 1;
static const uint32_t kProjectId = 1;
static const char kCustomerName[] = "Fuchsia";
static const char kProjectName[] = "Cobalt";

// Metric IDs
const uint32_t kErrorOccurredMetricId = 1;
const uint32_t kReadCacheHitsMetricId = 2;
const uint32_t kModuleLoadTimeMetricId = 3;
const uint32_t kLoginModuleFrameRateMetricId = 4;
const uint32_t kLedgerMemoryUsageMetricId = 5;
const uint32_t kFileSystemWriteTimesMetricId = 6;
const uint32_t kModuleDownloadsMetricId = 7;
const uint32_t kModuleInstallsMetricId = 8;
const uint32_t kDeviceBootsMetricId = 9;
const uint32_t kFeaturesActiveMetricId = 10;
const uint32_t kEventsOccurredMetricId = 11;

// MetricReportIds
const MetricReportId kDeviceBoots_UniqueDevicesMetricReportId =
    MetricReportId(kDeviceBootsMetricId, 91);
const MetricReportId kFeaturesActive_UniqueDevicesMetricReportId =
    MetricReportId(kFeaturesActiveMetricId, 201);
const MetricReportId kEventsOccurred_SimpleOccurrenceCountMetricReportId =
    MetricReportId(kEventsOccurredMetricId, 301);
const MetricReportId kEventsOccurred_UniqueDevicesMetricReportId =
    MetricReportId(kEventsOccurredMetricId, 401);

static const char kMetricDefinitions[] = R"(
metric {
  metric_name: "ErrorOccurred"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 1
  max_event_code: 100
  reports: {
    report_name: "ErrorCountsByCode"
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
    id: 111
    report_type: EVENT_COMPONENT_OCCURRENCE_COUNT
  }
}

metric {
  metric_name: "ModuleLoadTime"
  metric_type: ELAPSED_TIME
  customer_id: 1
  project_id: 1
  id: 3
  reports: {
    report_name: "ModuleLoadTime_Aggregated"
    id: 121
    report_type: NUMERIC_AGGREGATION
  }
  reports: {
    report_name: "ModuleLoadTime_Histogram"
    id: 221
    report_type: INT_RANGE_HISTOGRAM
  }
  reports: {
    report_name: "ModuleLoadTime_RawDump"
    id: 321
    report_type: NUMERIC_PERF_RAW_DUMP
  }
}

metric {
  metric_name: "LoginModuleFrameRate"
  metric_type: FRAME_RATE
  customer_id: 1
  project_id: 1
  id: 4
  reports: {
    report_name: "LoginModuleFrameRate_Aggregated"
    id: 131
    report_type: NUMERIC_AGGREGATION
  }
  reports: {
    report_name: "LoginModuleFrameRate_Histogram"
    id: 231
    report_type: INT_RANGE_HISTOGRAM
  }
  reports: {
    report_name: "LoginModuleFrameRate_RawDump"
    id: 331
    report_type: NUMERIC_PERF_RAW_DUMP
  }
}

metric {
  metric_name: "LedgerMemoryUsage"
  metric_type: MEMORY_USAGE
  customer_id: 1
  project_id: 1
  id: 5
  reports: {
    report_name: "LedgerMemoryUsage_Aggregated"
    id: 141
    report_type: NUMERIC_AGGREGATION
  }
  reports: {
    report_name: "LedgerMemoryUsage_Histogram"
    id: 241
    report_type: INT_RANGE_HISTOGRAM
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
  }
}

metric {
  metric_name: "FeaturesActive"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 10
  max_event_code: 4
  reports: {
    report_name: "FeaturesActive_UniqueDevices"
    id: 201
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: LARGE
    window_size: 1
    window_size: 7
    window_size: 30
  }
}

metric {
  metric_name: "EventsOccurred"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 11
  max_event_code: 4
  reports: {
    report_name: "SomeEventsOccurred_SimpleCount"
    id: 301
    report_type: SIMPLE_OCCURRENCE_COUNT
    local_privacy_noise_level: MEDIUM
  }
  reports: {
    report_name: "EventsOccurred_UniqueDevices"
    id: 401
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: MEDIUM
    window_size: 1
    window_size: 7
  }
}

)";

// Expected parameters of the locally aggregated reports in
// |kMetricDefinitions|.
static const ExpectedAggregationParams kExpectedAggregationParams = {
    /* The total number of locally aggregated Observations which should be
       generated for each day index. */
    27,
    /* The MetricReportIds of the locally aggregated reports in this
configuration. */
    {kDeviceBoots_UniqueDevicesMetricReportId,
     kFeaturesActive_UniqueDevicesMetricReportId,
     kEventsOccurred_UniqueDevicesMetricReportId},
    /* The number of Observations which should be generated for each day
       index, broken down by MetricReportId. */
    {{kDeviceBoots_UniqueDevicesMetricReportId, 2},
     {kFeaturesActive_UniqueDevicesMetricReportId, 15},
     {kEventsOccurred_UniqueDevicesMetricReportId, 10}},
    /* The number of event codes for each MetricReportId. */
    {{kDeviceBoots_UniqueDevicesMetricReportId, 2},
     {kFeaturesActive_UniqueDevicesMetricReportId, 5},
     {kEventsOccurred_UniqueDevicesMetricReportId, 5}},
    /* The set of window sizes for each MetricReportId. */
    {{kDeviceBoots_UniqueDevicesMetricReportId, {1}},
     {kFeaturesActive_UniqueDevicesMetricReportId, {1, 7, 30}},
     {kEventsOccurred_UniqueDevicesMetricReportId, {1, 7}}}};

// A set of MetricDefinitions of type EVENT_OCCURRED, each of which has a
// UNIQUE_N_DAY_ACTIVES report with local_privacy_noise_level set to NONE.
static const char kNoiseFreeUniqueActivesMetricDefinitions[] = R"(
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
    local_privacy_noise_level: NONE
    window_size: 1
  }
}

metric {
  metric_name: "FeaturesActive"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 10
  max_event_code: 4
  reports: {
    report_name: "SomeFeaturesActive_UniqueDevices"
    id: 201
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: NONE
    window_size: 1
    window_size: 7
    window_size: 30
  }
}

metric {
  metric_name: "EventsOccurred"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 11
  max_event_code: 4
  reports: {
    report_name: "SomeEventsOccurred_SimpleCount"
    id: 301
    report_type: SIMPLE_OCCURRENCE_COUNT
    local_privacy_noise_level: NONE
  }
  reports: {
    report_name: "SomeEventsOccurred_UniqueDevices"
    id: 401
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: NONE
    window_size: 1
    window_size: 7
  }
}

)";

// Expected parameters of the locally aggregated reports in
// |kNoiseFreeUniqueActivesMetricDefinitions|.
static const ExpectedAggregationParams
    kNoiseFreeUniqueActivesExpectedAggregationParams = {
        /* The total number of locally aggregated Observations which should be
            generated for each day index. */
        27,
        /* The MetricReportIds of the locally aggregated reports in this
    configuration. */
        {kDeviceBoots_UniqueDevicesMetricReportId,
         kFeaturesActive_UniqueDevicesMetricReportId,
         kEventsOccurred_UniqueDevicesMetricReportId},
        /* The number of Observations which should be generated for each day
           index, broken down by MetricReportId. */
        {{kDeviceBoots_UniqueDevicesMetricReportId, 2},
         {kFeaturesActive_UniqueDevicesMetricReportId, 15},
         {kEventsOccurred_UniqueDevicesMetricReportId, 10}},
        /* The number of event codes for each MetricReportId. */
        {{kDeviceBoots_UniqueDevicesMetricReportId, 2},
         {kFeaturesActive_UniqueDevicesMetricReportId, 5},
         {kEventsOccurred_UniqueDevicesMetricReportId, 5}},
        /* The set of window sizes for each MetricReportId. */
        {{kDeviceBoots_UniqueDevicesMetricReportId, {1}},
         {kFeaturesActive_UniqueDevicesMetricReportId, {1, 7, 30}},
         {kEventsOccurred_UniqueDevicesMetricReportId, {1, 7}}}};

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

}  // namespace

class LoggerTest : public ::testing::Test {
 protected:
  void SetUp() {
    SetUpFromMetrics(kMetricDefinitions, kExpectedAggregationParams);
  }

  // Set up LoggerTest using serialized MetricDefinitions.
  void SetUpFromMetrics(
      const char metric_string[],
      const ExpectedAggregationParams expected_aggregation_params) {
    auto metric_definitions = std::make_unique<MetricDefinitions>();
    ASSERT_TRUE(
        PopulateMetricDefinitions(metric_string, metric_definitions.get()));
    expected_aggregation_params_ = expected_aggregation_params;
    project_context_.reset(new ProjectContext(kCustomerId, kProjectId,
                                              kCustomerName, kProjectName,
                                              std::move(metric_definitions)));
    observation_store_.reset(new FakeObservationStore);
    update_recipient_.reset(new TestUpdateRecipient);
    observation_encrypter_.reset(
        new EncryptedMessageMaker("", EncryptedMessage::NONE));
    observation_writer_.reset(
        new ObservationWriter(observation_store_.get(), update_recipient_.get(),
                              observation_encrypter_.get()));
    encoder_.reset(
        new Encoder(ClientSecret::GenerateNewSecret(), system_data_.get()));
    local_aggregate_store_.reset(new LocalAggregateStore);
    history_.reset(new AggregatedObservationHistory);
    event_aggregator_.reset(new EventAggregator(
        encoder_.get(), observation_writer_.get(), local_aggregate_store_.get(),
        history_.get(), /* backfill_days */ 0));
    logger_.reset(new Logger(encoder_.get(), event_aggregator_.get(),
                             observation_writer_.get(),
                             project_context_.get()));
    // Create a mock clock which does not increment by default when called.
    // Set the time to 1 year after the start of Unix time so that the start
    // date of any aggregation window falls after the start of time.
    mock_clock_ = new IncrementingClock(std::chrono::system_clock::duration(0));
    mock_clock_->set_time(
        std::chrono::system_clock::time_point(std::chrono::seconds(kYear)));
    logger_->SetClock(mock_clock_);
  }

  // Returns the day index of the current day according to |mock_clock_|, in
  // |time_zone|, without incrementing the clock.
  uint32_t CurrentDayIndex(MetricDefinition::TimeZonePolicy time_zone) {
    return TimeToDayIndex(
        std::chrono::system_clock::to_time_t(mock_clock_->peek_now()),
        time_zone);
  }

  // Advances |mock_clock_| by |num_days| days.
  void AdvanceDay(int num_days) {
    mock_clock_->increment_by(std::chrono::seconds(kDay) * num_days);
  }

  // Clears the FakeObservationStore and resets the TestUpdateRecipient's
  // count.
  void ResetObservationStore() {
    observation_store_->messages_received.clear();
    observation_store_->metadata_received.clear();
    update_recipient_->invocation_count = 0;
  }

  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<Logger> logger_;
  std::unique_ptr<EventAggregator> event_aggregator_;
  std::unique_ptr<ObservationWriter> observation_writer_;
  std::unique_ptr<LocalAggregateStore> local_aggregate_store_;
  std::unique_ptr<AggregatedObservationHistory> history_;
  std::unique_ptr<FakeObservationStore> observation_store_;
  std::unique_ptr<TestUpdateRecipient> update_recipient_;
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;
  std::unique_ptr<ProjectContext> project_context_;
  std::unique_ptr<SystemDataInterface> system_data_;
  IncrementingClock* mock_clock_;
  ExpectedAggregationParams expected_aggregation_params_;
};

// Creates a Logger whose ProjectContext contains only EVENT_OCCURRED metrics,
// each of which has a report of type UNIQUE_N_DAY_ACTIVES with
// local_privacy_noise_level set to NONE. Used to test the values of
// UniqueActivesObservations generated by the EventAggregator.
class UniqueActivesLoggerTest : public LoggerTest {
 protected:
  void SetUp() {
    SetUpFromMetrics(kNoiseFreeUniqueActivesMetricDefinitions,
                     kNoiseFreeUniqueActivesExpectedAggregationParams);
  }
};

// Tests the method LogEvent().
TEST_F(LoggerTest, LogEvent) {
  ASSERT_EQ(kOK, logger_->LogEvent(kErrorOccurredMetricId, 42));
  Observation2 observation;
  uint32_t expected_report_id = 123;
  ASSERT_TRUE(FetchSingleObservation(&observation, expected_report_id,
                                     observation_store_.get(),
                                     update_recipient_.get()));
  ASSERT_TRUE(observation.has_basic_rappor());
  EXPECT_FALSE(observation.basic_rappor().data().empty());
}

// Tests the method LogEventCount().
TEST_F(LoggerTest, LogEventcount) {
  std::vector<uint32_t> expected_report_ids = {111};
  ASSERT_EQ(kOK, logger_->LogEventCount(kReadCacheHitsMetricId, 43,
                                        "component2", 1, 303));
  EXPECT_TRUE(CheckNumericEventObservations(
      expected_report_ids, 43u, "component2", 303, observation_store_.get(),
      update_recipient_.get()));
}

// Tests the method LogElapsedTime().
TEST_F(LoggerTest, LogElapsedTime) {
  std::vector<uint32_t> expected_report_ids = {121, 221, 321};
  ASSERT_EQ(kOK, logger_->LogElapsedTime(kModuleLoadTimeMetricId, 44,
                                         "component4", 4004));
  EXPECT_TRUE(CheckNumericEventObservations(
      expected_report_ids, 44u, "component4", 4004, observation_store_.get(),
      update_recipient_.get()));
}

// Tests the method LogFrameRate().
TEST_F(LoggerTest, LogFrameRate) {
  std::vector<uint32_t> expected_report_ids = {131, 231, 331};
  ASSERT_EQ(kOK, logger_->LogFrameRate(kLoginModuleFrameRateMetricId, 45,
                                       "component5", 5.123));
  EXPECT_TRUE(CheckNumericEventObservations(
      expected_report_ids, 45u, "component5", 5123, observation_store_.get(),
      update_recipient_.get()));
}

// Tests the method LogMemoryUsage().
TEST_F(LoggerTest, LogMemoryUsage) {
  std::vector<uint32_t> expected_report_ids = {141, 241};
  ASSERT_EQ(kOK, logger_->LogMemoryUsage(kLedgerMemoryUsageMetricId, 46,
                                         "component6", 606));
  EXPECT_TRUE(CheckNumericEventObservations(
      expected_report_ids, 46u, "component6", 606, observation_store_.get(),
      update_recipient_.get()));
}

// Tests the method LogIntHistogram().
TEST_F(LoggerTest, LogIntHistogram) {
  std::vector<uint32_t> indices = {0, 1, 2, 3};
  std::vector<uint32_t> counts = {100, 101, 102, 103};
  auto histogram = NewHistogram(indices, counts);
  ASSERT_EQ(kOK, logger_->LogIntHistogram(kFileSystemWriteTimesMetricId, 47,
                                          "component7", std::move(histogram)));
  Observation2 observation;
  uint32_t expected_report_id = 151;
  ASSERT_TRUE(FetchSingleObservation(&observation, expected_report_id,
                                     observation_store_.get(),
                                     update_recipient_.get()));
  ASSERT_TRUE(observation.has_histogram());
  auto histogram_observation = observation.histogram();
  EXPECT_EQ(47u, histogram_observation.event_code());
  EXPECT_EQ(histogram_observation.component_name_hash().size(), 32u);
  EXPECT_EQ(static_cast<size_t>(histogram_observation.buckets_size()),
            indices.size());
  for (auto i = 0u; i < indices.size(); i++) {
    const auto& bucket = histogram_observation.buckets(i);
    EXPECT_EQ(bucket.index(), indices[i]);
    EXPECT_EQ(bucket.count(), counts[i]);
  }
}

// Tests the method LogString().
TEST_F(LoggerTest, LogString) {
  ASSERT_EQ(kOK,
            logger_->LogString(kModuleDownloadsMetricId, "www.mymodule.com"));
  std::vector<Observation2> observations(2);
  std::vector<uint32_t> expected_report_ids = {161, 261};
  ASSERT_TRUE(FetchObservations(&observations, expected_report_ids,
                                observation_store_.get(),
                                update_recipient_.get()));

  ASSERT_TRUE(observations[0].has_string_rappor());
  EXPECT_FALSE(observations[0].string_rappor().data().empty());

  ASSERT_TRUE(observations[1].has_forculus());
  EXPECT_FALSE(observations[1].forculus().ciphertext().empty());
}

// Tests the method LogCustomEvent().
TEST_F(LoggerTest, LogCustomEvent) {
  CustomDimensionValue module_value, number_value;
  module_value.set_string_value("gmail");
  number_value.set_int_value(3);
  std::vector<std::string> dimension_names = {"module", "number"};
  std::vector<CustomDimensionValue> values = {module_value, number_value};
  auto custom_event = NewCustomEvent(dimension_names, values);
  ASSERT_EQ(kOK, logger_->LogCustomEvent(kModuleInstallsMetricId,
                                         std::move(custom_event)));
  Observation2 observation;
  uint32_t expected_report_id = 125;
  ASSERT_TRUE(FetchSingleObservation(&observation, expected_report_id,
                                     observation_store_.get(),
                                     update_recipient_.get()));
  ASSERT_TRUE(observation.has_custom());
  const CustomObservation& custom_observation = observation.custom();
  for (auto i = 0u; i < values.size(); i++) {
    auto obs_dimension = custom_observation.values().at(dimension_names[i]);
    EXPECT_TRUE(MessageDifferencer::Equals(obs_dimension, values[i]));
  }
}

// Tests that the expected number of locally aggregated Observations are
// generated when no events have been logged.
TEST_F(LoggerTest, CheckNumAggregatedObsNoEvents) {
  ASSERT_EQ(kOK, event_aggregator_->GenerateObservations(
                     CurrentDayIndex(MetricDefinition::UTC)));
  std::vector<Observation2> observations(0);
  EXPECT_TRUE(FetchAggregatedObservations(
      &observations, expected_aggregation_params_, observation_store_.get(),
      update_recipient_.get()));
}

// Tests that the expected number of locally aggregated Observations are
// generated when one Event has been logged for a locally aggregated report.
TEST_F(LoggerTest, CheckNumAggregatedObsOneEvent) {
  // Log 1 occurrence of event code 0 for the DeviceBoots metric, which has no
  // immediate reports.
  ASSERT_EQ(kOK, logger_->LogEvent(kDeviceBootsMetricId, 0));
  // Check that no immediate Observation was generated.
  std::vector<Observation2> immediate_observations(0);
  std::vector<uint32_t> expected_immediate_report_ids = {};
  ASSERT_TRUE(
      FetchObservations(&immediate_observations, expected_immediate_report_ids,
                        observation_store_.get(), update_recipient_.get()));
  // Generate locally aggregated observations for the current day index.
  ASSERT_EQ(kOK, event_aggregator_->GenerateObservations(
                     CurrentDayIndex(MetricDefinition::UTC)));
  // Check that the expected numbers of aggregated observations were generated.
  std::vector<Observation2> aggregated_observations;
  EXPECT_TRUE(FetchAggregatedObservations(
      &aggregated_observations, expected_aggregation_params_,
      observation_store_.get(), update_recipient_.get()));
}

// Tests that the expected number of locally aggregated Observations are
// generated when multiple Events have been logged for locally aggregated
// reports.
TEST_F(LoggerTest, CheckNumAggregatedObsMultipleEvents) {
  // Log 2 occurrences of event code 0 for the DeviceBoots metric, which has 1
  // locally aggregated report and no immediate reports.
  ASSERT_EQ(kOK, logger_->LogEvent(kDeviceBootsMetricId, 0));
  ASSERT_EQ(kOK, logger_->LogEvent(kDeviceBootsMetricId, 0));
  // Log 2 occurrences of event codes for the FeaturesActive metric, which
  // has 1 locally aggregated report and no immediate reports.
  ASSERT_EQ(kOK, logger_->LogEvent(kFeaturesActiveMetricId, 0));
  ASSERT_EQ(kOK, logger_->LogEvent(kFeaturesActiveMetricId, 1));
  // Check that no immediate Observations were generated.
  std::vector<Observation2> immediate_observations(0);
  std::vector<uint32_t> expected_immediate_report_ids = {};
  ASSERT_TRUE(
      FetchObservations(&immediate_observations, expected_immediate_report_ids,
                        observation_store_.get(), update_recipient_.get()));
  // Generate locally aggregated observations for the current day index.
  ASSERT_EQ(kOK, event_aggregator_->GenerateObservations(
                     CurrentDayIndex(MetricDefinition::UTC)));
  // Check that the expected numbers of aggregated observations were
  // generated.
  std::vector<Observation2> aggregated_observations;
  EXPECT_TRUE(FetchAggregatedObservations(
      &aggregated_observations, expected_aggregation_params_,
      observation_store_.get(), update_recipient_.get()));
}

// Tests that the expected number of locally aggregated Observations are
// generated when multiple Events have been logged for locally aggregated
// and immediate reports.
TEST_F(LoggerTest, CheckNumAggregatedObsImmediateAndAggregatedEvents) {
  // Log 3 occurrences of event codes for the EventsOccurred metric, which
  // has 1 locally aggregated report and 1 immediate report.
  ASSERT_EQ(kOK, logger_->LogEvent(kEventsOccurredMetricId, 0));
  ASSERT_EQ(kOK, logger_->LogEvent(kEventsOccurredMetricId, 0));
  ASSERT_EQ(kOK, logger_->LogEvent(kEventsOccurredMetricId, 2));
  // Check that each of the 3 logged events resulted in an immediate
  // Observation.
  std::vector<Observation2> immediate_observations(3);
  std::vector<uint32_t> expected_immediate_report_ids(
      3, kEventsOccurred_SimpleOccurrenceCountMetricReportId.second);
  ASSERT_TRUE(
      FetchObservations(&immediate_observations, expected_immediate_report_ids,
                        observation_store_.get(), update_recipient_.get()));
  // Clear the FakeObservationStore.
  ResetObservationStore();
  // Generate locally aggregated observations for the current day index.
  ASSERT_EQ(kOK, event_aggregator_->GenerateObservations(
                     CurrentDayIndex(MetricDefinition::UTC)));
  // Check that the expected aggregated observations were generated.
  std::vector<Observation2> observations;
  EXPECT_TRUE(FetchAggregatedObservations(
      &observations, expected_aggregation_params_, observation_store_.get(),
      update_recipient_.get()));
}

// Tests that UniqueActivesObservations with the expected values are generated
// when no events have been logged.
TEST_F(UniqueActivesLoggerTest, CheckUniqueActivesObsValuesNoEvents) {
  auto current_day_index = CurrentDayIndex(MetricDefinition::UTC);
  // Generate locally aggregated Observations without logging any events.
  ASSERT_EQ(kOK, event_aggregator_->GenerateObservations(current_day_index));
  // Check that all generated Observations are of non-activity.
  auto expected_obs = MakeNullExpectedUniqueActivesObservations(
      expected_aggregation_params_, current_day_index);
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_obs, observation_store_.get(), update_recipient_.get()));
}

// Tests that UniqueActivesObservations with the expected values are generated
// when events have been logged for UNIQUE_N_DAY_ACTIVES reports on a single
// day.
TEST_F(UniqueActivesLoggerTest, CheckUniqueActivesObsValuesSingleDay) {
  auto current_day_index = CurrentDayIndex(MetricDefinition::UTC);
  // Log 2 occurrences of event code 0 for the DeviceBoots metric, which has 1
  // locally aggregated report and no immediate reports.
  ASSERT_EQ(kOK, logger_->LogEvent(kDeviceBootsMetricId, 0));
  ASSERT_EQ(kOK, logger_->LogEvent(kDeviceBootsMetricId, 0));
  // Log 2 occurrences of event codes for the SomeFeaturesActive metric, which
  // has 1 locally aggregated report and no immediate reports.
  ASSERT_EQ(kOK, logger_->LogEvent(kFeaturesActiveMetricId, 0));
  ASSERT_EQ(kOK, logger_->LogEvent(kFeaturesActiveMetricId, 1));
  // Generate locally aggregated observations for the current day index.
  ASSERT_EQ(kOK, event_aggregator_->GenerateObservations(current_day_index));
  // Form the expected observations for the current day index.
  auto expected_obs = MakeNullExpectedUniqueActivesObservations(
      expected_aggregation_params_, current_day_index);
  expected_obs[{kDeviceBoots_UniqueDevicesMetricReportId, current_day_index}] =
      {{1, {true, false}}};
  expected_obs[{kFeaturesActive_UniqueDevicesMetricReportId,
                current_day_index}] = {{1, {true, true, false, false, false}},
                                       {7, {true, true, false, false, false}},
                                       {30, {true, true, false, false, false}}};
  // Check that the expected aggregated observations were generated.
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_obs, observation_store_.get(), update_recipient_.get()));
}

// Tests that UniqueActivesObservations with the expected values are generated
// when events have been logged for a UniqueActives report over multiple days.
//
// Logs events for the EventsOccurred_UniqueDevices report (whose parent metric
// has max_event_code = 5, and whose report ID is 401) for 10 days, according to
// the following pattern:
//
// * Never log event code 0.
// * On the i-th day (0-indexed) of logging, log an event for event code k,
//   1 <= k < 5, if 3*k divides i.
//
// Each day following the first day, generates Observations for the previous
// day index and checks them against the expected set of Observations, then
// garbage-collects the LocalAggregateStore for the current day index.
//
// The EventsOccurred_UniqueDevices report has window sizes 1 and 7, and
// the expected pattern of those Observations' values on the i-th day is:
//
// (i, window size)            active for event codes
// ------------------------------------------------------
// (0, 1)                           1, 2, 3, 4
// (0, 7)                           1, 2, 3, 4
// (1, 1)                          ---
// (1, 7)                           1, 2, 3, 4
// (2, 1)                          ---
// (2, 7)                           1, 2, 3, 4
// (3, 1)                           1
// (3, 7)                           1, 2, 3, 4
// (4, 1)                          ---
// (4, 7)                           1, 2, 3, 4
// (5, 1)                          ---
// (5, 7)                           1, 2, 3, 4
// (6, 1)                           1, 2
// (6, 7)                           1, 2, 3, 4
// (7, 1)                          ---
// (7, 7)                           1, 2
// (8, 1)                          ---
// (8, 7)                           1, 2
// (9, 1)                           1, 3
// (9, 7)                           1, 2, 3
//
// All Observations for all other locally aggregated reports should be
// observations of non-occurrence.
TEST_F(UniqueActivesLoggerTest, CheckUniqueActivesObservationValues) {
  // Form expected Observations for the 10 days of logging.
  auto start_day_index = CurrentDayIndex(MetricDefinition::UTC);
  std::vector<ExpectedUniqueActivesObservations> expected_obs(10);
  for (uint32_t i = 0; i < expected_obs.size(); i++) {
    expected_obs[i] = MakeNullExpectedUniqueActivesObservations(
        expected_aggregation_params_, start_day_index + i);
  }
  expected_obs[0][{kEventsOccurred_UniqueDevicesMetricReportId,
                   start_day_index}] = {{1, {false, true, true, true, true}},
                                        {7, {false, true, true, true, true}}};
  expected_obs[1][{kEventsOccurred_UniqueDevicesMetricReportId,
                   start_day_index + 1}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[2][{kEventsOccurred_UniqueDevicesMetricReportId,
                   start_day_index + 2}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[3][{kEventsOccurred_UniqueDevicesMetricReportId,
                   start_day_index + 3}] = {
      {1, {false, true, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[4][{kEventsOccurred_UniqueDevicesMetricReportId,
                   start_day_index + 4}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[5][{kEventsOccurred_UniqueDevicesMetricReportId,
                   start_day_index + 5}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[6][{kEventsOccurred_UniqueDevicesMetricReportId,
                   start_day_index + 6}] = {
      {1, {false, true, true, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[7][{kEventsOccurred_UniqueDevicesMetricReportId,
                   start_day_index + 7}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_obs[8][{kEventsOccurred_UniqueDevicesMetricReportId,
                   start_day_index + 8}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_obs[9][{kEventsOccurred_UniqueDevicesMetricReportId,
                   start_day_index + 9}] = {
      {1, {false, true, false, true, false}},
      {7, {false, true, true, true, false}}};

  for (uint32_t i = 0; i < 10; i++) {
    if (i < 10) {
      for (uint32_t event_code = 1; event_code < 5; event_code++) {
        if (i % (3 * event_code) == 0) {
          ASSERT_EQ(kOK,
                    logger_->LogEvent(kEventsOccurredMetricId, event_code));
        }
      }
    }
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Advance the Logger's clock by 1 day.
    AdvanceDay(1);
    // Generate locally aggregated Observations for the previous day
    // index.
    ASSERT_EQ(kOK, event_aggregator_->GenerateObservations(
                       CurrentDayIndex(MetricDefinition::UTC) - 1));
    // Check the generated Observations against the expectation.
    EXPECT_TRUE(CheckUniqueActivesObservations(
        expected_obs[i], observation_store_.get(), update_recipient_.get()));
    // Garbage-collect the LocalAggregateStore for the current day index.
    ASSERT_EQ(kOK, event_aggregator_->GarbageCollect(
                       CurrentDayIndex(MetricDefinition::UTC)));
  }
}

}  // namespace logger
}  // namespace cobalt

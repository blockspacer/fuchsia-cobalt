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
#include "config/packed_event_codes.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "logger/encoder.h"
#include "logger/event_aggregator.h"
#include "logger/logger_test_utils.h"
#include "logger/project_context.h"
#include "logger/status.h"
#include "logger/testing_constants.h"
#include "util/clock.h"
#include "util/datetime_util.h"
#include "util/encrypted_message_util.h"

using ::google::protobuf::RepeatedPtrField;
using ::google::protobuf::util::MessageDifferencer;

namespace cobalt {

using config::PackEventCodes;
using config::ProjectConfigs;

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
using testing::ExpectedPerDeviceNumericObservations;
using testing::ExpectedReportParticipationObservations;
using testing::ExpectedUniqueActivesObservations;
using testing::FakeObservationStore;
using testing::FetchAggregatedObservations;
using testing::FetchObservations;
using testing::FetchSingleObservation;
using testing::GetTestProject;
using testing::MakeNullExpectedUniqueActivesObservations;
using testing::MockConsistentProtoStore;
using testing::TestUpdateRecipient;

namespace {
// Number of seconds in a day
const int kDay = 60 * 60 * 24;
// Number of seconds in an ideal year
const int kYear = kDay * 365;

// Filenames for constructors of ConsistentProtoStores
static const char kAggregateStoreFilename[] = "local_aggregate_store_backup";
static const char kObsHistoryFilename[] = "obs_history_backup";

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
    SetUpFromMetrics(testing::all_report_types::kCobaltRegistryBase64,
                     testing::all_report_types::kExpectedAggregationParams);
  }

  // Set up LoggerTest using a base64-encoded registry.
  void SetUpFromMetrics(
      const std::string& registry_base64,
      const ExpectedAggregationParams& expected_aggregation_params) {
    expected_aggregation_params_ = expected_aggregation_params;
    observation_store_.reset(new FakeObservationStore);
    update_recipient_.reset(new TestUpdateRecipient);
    observation_encrypter_ = EncryptedMessageMaker::MakeUnencrypted();
    observation_writer_.reset(
        new ObservationWriter(observation_store_.get(), update_recipient_.get(),
                              observation_encrypter_.get()));
    encoder_.reset(
        new Encoder(ClientSecret::GenerateNewSecret(), system_data_.get()));
    local_aggregate_proto_store_.reset(
        new MockConsistentProtoStore(kAggregateStoreFilename));
    obs_history_proto_store_.reset(
        new MockConsistentProtoStore(kObsHistoryFilename));
    event_aggregator_.reset(new EventAggregator(
        encoder_.get(), observation_writer_.get(),
        local_aggregate_proto_store_.get(), obs_history_proto_store_.get()));
    logger_.reset(new Logger(GetTestProject(registry_base64), encoder_.get(),
                             event_aggregator_.get(),
                             observation_writer_.get()));
    // Create a mock clock which does not increment by default when called.
    // Set the time to 1 year after the start of Unix time so that the start
    // date of any aggregation window falls after the start of time.
    mock_clock_ = new IncrementingClock(std::chrono::system_clock::duration(0));
    mock_clock_->set_time(
        std::chrono::system_clock::time_point(std::chrono::seconds(kYear)));
    logger_->SetClock(mock_clock_);
  }

  void TearDown() {
    event_aggregator_.reset();
    logger_.reset();
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

  // Clears the FakeObservationStore and resets counts of Observations received
  // by the FakeObservationStore and the TestUpdateRecipient.
  void ResetObservationStore() {
    observation_store_->messages_received.clear();
    observation_store_->metadata_received.clear();
    observation_store_->ResetObservationCounter();
    update_recipient_->invocation_count = 0;
  }

  Status GenerateAggregatedObservations(uint32_t day_index) {
    return event_aggregator_->GenerateObservationsNoWorker(day_index);
  }

  Status GarbageCollectAggregateStore(uint32_t day_index) {
    return event_aggregator_->GarbageCollect(day_index);
  }

  std::unique_ptr<Logger> logger_;
  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<EventAggregator> event_aggregator_;
  std::unique_ptr<ObservationWriter> observation_writer_;
  std::unique_ptr<FakeObservationStore> observation_store_;
  std::unique_ptr<TestUpdateRecipient> update_recipient_;
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;
  std::unique_ptr<SystemDataInterface> system_data_;
  std::unique_ptr<MockConsistentProtoStore> local_aggregate_proto_store_;
  std::unique_ptr<MockConsistentProtoStore> obs_history_proto_store_;
  ExpectedAggregationParams expected_aggregation_params_;
  IncrementingClock* mock_clock_;
};

// Creates a Logger whose ProjectContext contains only EVENT_OCCURRED metrics,
// each of which has a report of type UNIQUE_N_DAY_ACTIVES with
// local_privacy_noise_level set to NONE. Used to test the values of
// UniqueActivesObservations generated by the EventAggregator.
class UniqueActivesLoggerTest : public LoggerTest {
 protected:
  void SetUp() {
    SetUpFromMetrics(
        testing::unique_actives_noise_free::kCobaltRegistryBase64,
        testing::unique_actives_noise_free::kExpectedAggregationParams);
  }
};

// Creates a Logger for a ProjectContext where each of the metrics
// has a report of type PER_DEVICE_NUMERIC_STATS.
class PerDeviceNumericLoggerTest : public LoggerTest {
 protected:
  void SetUp() {
    SetUpFromMetrics(
        testing::per_device_numeric_stats::kCobaltRegistryBase64,
        testing::per_device_numeric_stats::kExpectedAggregationParams);
  }
};

// Tests the method LogEvent().
TEST_F(LoggerTest, LogEvent) {
  // Attempt to use an event code larger than max_event_code.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId,
                              101));

  // Now use good arguments.
  ASSERT_EQ(kOK, logger_->LogEvent(
                     testing::all_report_types::kErrorOccurredMetricId, 42));
  Observation2 observation;
  uint32_t expected_report_id =
      testing::all_report_types::kErrorCountsByCodeReportId;
  ASSERT_TRUE(FetchSingleObservation(&observation, expected_report_id,
                                     observation_store_.get(),
                                     update_recipient_.get()));
  ASSERT_TRUE(observation.has_basic_rappor());
  EXPECT_FALSE(observation.basic_rappor().data().empty());
}

// Tests the method LogEventCount().
TEST_F(LoggerTest, LogEventCount) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kReadCacheHitCountsReportId,
      testing::all_report_types::kReadCacheHitHistogramsReportId,
      testing::all_report_types::kReadCacheHitStatsReportId};

  // Attempt to use an event code larger than max_event_code.
  ASSERT_EQ(
      kInvalidArguments,
      logger_->LogEventCount(testing::all_report_types::kReadCacheHitsMetricId,
                             112, "component2", 1, 303));

  // All good
  ASSERT_EQ(kOK, logger_->LogEventCount(
                     testing::all_report_types::kReadCacheHitsMetricId, 43,
                     "component2", 1, 303));
  EXPECT_TRUE(CheckNumericEventObservations(
      expected_report_ids, 43u, "component2", 303, observation_store_.get(),
      update_recipient_.get()));

  // Clear the FakeObservationStore.
  ResetObservationStore();

  // No problem using event code 0.
  ASSERT_EQ(kOK, logger_->LogEventCount(
                     testing::all_report_types::kReadCacheHitsMetricId, 0, "",
                     1, 303));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 0u, "", 303,
                                            observation_store_.get(),
                                            update_recipient_.get()));
}

// Tests the version of the method LogEventCount() that accepts a vector of
// event codes.
TEST_F(LoggerTest, LogEventCountMultiDimension) {
  // Use a metric ID for the wrong type of metric. Expect kInvalidArguments.
  EXPECT_EQ(
      kInvalidArguments,
      logger_->LogEventCount(testing::all_report_types::kErrorOccurredMetricId,
                             std::vector<uint32_t>({43}), "", 0, 303));

  // Use no event codes when the metric has one dimension. Expect
  // kInvalidArguments.
  EXPECT_EQ(
      kInvalidArguments,
      logger_->LogEventCount(testing::all_report_types::kReadCacheHitsMetricId,
                             std::vector<uint32_t>({}), "", 0, 303));

  // Use two event codes when the metric has one dimension. Expect
  // kInvalidArguments.
  EXPECT_EQ(
      kInvalidArguments,
      logger_->LogEventCount(testing::all_report_types::kReadCacheHitsMetricId,
                             std::vector<uint32_t>({43, 44}), "", 0, 303));

  // Use an event code that exceeds the specified max_event_code. Expect
  // kInvalidArguments.
  EXPECT_EQ(
      kInvalidArguments,
      logger_->LogEventCount(testing::all_report_types::kReadCacheHitsMetricId,
                             std::vector<uint32_t>({200}), "", 0, 303));

  // All good, expect OK.
  EXPECT_EQ(kOK, logger_->LogEventCount(
                     testing::all_report_types::kReadCacheHitsMetricId,
                     std::vector<uint32_t>({43}), "", 0, 303));

  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kReadCacheHitCountsReportId,
      testing::all_report_types::kReadCacheHitHistogramsReportId,
      testing::all_report_types::kReadCacheHitStatsReportId};
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 43u, "", 303,
                                            observation_store_.get(),
                                            update_recipient_.get()));
}

// Tests the method LogElapsedTime().
TEST_F(LoggerTest, LogElapsedTime) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kModuleLoadTimeAggregatedReportId,
      testing::all_report_types::kModuleLoadTimeHistogramReportId,
      testing::all_report_types::kModuleLoadTimeRawDumpReportId};

  // Use a non-zero event code even though the metric does not have any
  // metric dimensions defined. Expect kInvalidArgument.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogElapsedTime(
                testing::all_report_types::kModuleLoadTimeMetricId, 44,
                "component4", 4004));

  // Use a zero event code when the metric does not have any metric dimensions
  // set. This is OK by convention. The zero will be ignored.
  ASSERT_EQ(kOK, logger_->LogElapsedTime(
                     testing::all_report_types::kModuleLoadTimeMetricId, 0,
                     "component4", 4004));

  EXPECT_TRUE(CheckNumericEventObservations(
      expected_report_ids, 0u, "component4", 4004, observation_store_.get(),
      update_recipient_.get()));
}

// Tests the version of the method LogElapsedTime() that accepts a vector of
// event codes.
TEST_F(LoggerTest, LogElapsedTimeMultiDimension) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kModuleLoadTimeAggregatedReportId,
      testing::all_report_types::kModuleLoadTimeHistogramReportId,
      testing::all_report_types::kModuleLoadTimeRawDumpReportId};

  // Use a non-zero event code even though the metric does not have any
  // metric dimensions defined. Expect kInvalidArgument.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogElapsedTime(
                testing::all_report_types::kModuleLoadTimeMetricId,
                std::vector<uint32_t>({44}), "component4", 4004));

  // Use a vector of length two even though the metric does not have any
  // metric dimensions defined. Expect kInvalidArgument.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogElapsedTime(
                testing::all_report_types::kModuleLoadTimeMetricId,
                std::vector<uint32_t>({0, 0}), "component4", 4004));

  // Use an empty vector of event codes when the metric does not have any metric
  // dimensions set. This is good.
  ASSERT_EQ(kOK, logger_->LogElapsedTime(
                     testing::all_report_types::kModuleLoadTimeMetricId,
                     std::vector<uint32_t>({}), "component4", 4004));

  EXPECT_TRUE(CheckNumericEventObservations(
      expected_report_ids, 0u, "component4", 4004, observation_store_.get(),
      update_recipient_.get()));
}

// Tests the method LogFrameRate().
TEST_F(LoggerTest, LogFrameRate) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kLoginModuleFrameRateAggregatedReportId,
      testing::all_report_types::kLoginModuleFrameRateHistogramReportId,
      testing::all_report_types::kLoginModuleFrameRateRawDumpReportId};
  // There is no max_event_code set so only the specified code of 45 is allowed.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogFrameRate(
                testing::all_report_types::kLoginModuleFrameRateMetricId, 0,
                "component5", 5.123));

  // All good.
  ASSERT_EQ(kOK, logger_->LogFrameRate(
                     testing::all_report_types::kLoginModuleFrameRateMetricId,
                     45, "component5", 5.123));
  EXPECT_TRUE(CheckNumericEventObservations(
      expected_report_ids, 45u, "component5", 5123, observation_store_.get(),
      update_recipient_.get()));
}

// Tests the version of the method LogFrameRate() that accepts a vector of
// event codes.
TEST_F(LoggerTest, LogFrameRateMultiDimension) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kLoginModuleFrameRateAggregatedReportId,
      testing::all_report_types::kLoginModuleFrameRateHistogramReportId,
      testing::all_report_types::kLoginModuleFrameRateRawDumpReportId};

  // Use a metric ID for the wrong type of metric. Expect kInvalidArguments.
  ASSERT_EQ(
      kInvalidArguments,
      logger_->LogFrameRate(testing::all_report_types::kModuleLoadTimeMetricId,
                            std::vector<uint32_t>({45}), "", 5.123));

  // Use no event codes when the metric has one dimension. Expect
  // kInvalidArguments.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogFrameRate(
                testing::all_report_types::kLoginModuleFrameRateMetricId,
                std::vector<uint32_t>({}), "", 5.123));

  // Use two event codes when the metric has one dimension. Expect
  // kInvalidArguments.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogFrameRate(
                testing::all_report_types::kLoginModuleFrameRateMetricId,
                std::vector<uint32_t>({45, 46}), "", 5.123));

  // There is no max_event_code set so only the specified code of 45 is allowed.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogFrameRate(
                testing::all_report_types::kLoginModuleFrameRateMetricId,
                std::vector<uint32_t>({44}), "", 5.123));

  // All good
  ASSERT_EQ(kOK, logger_->LogFrameRate(
                     testing::all_report_types::kLoginModuleFrameRateMetricId,
                     std::vector<uint32_t>({45}), "", 5.123));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 45u, "", 5123,
                                            observation_store_.get(),
                                            update_recipient_.get()));
}

// Tests the method LogMemoryUsage().
TEST_F(LoggerTest, LogMemoryUsage) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kLedgerMemoryUsageAggregatedReportId,
      testing::all_report_types::kLedgerMemoryUsageHistogramReportId};
  // The simple version of LogMemoryUsage() cannot be used at all since this
  // metric has two dimensions.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogMemoryUsage(
                testing::all_report_types::kLedgerMemoryUsageMetricId, 46,
                "component6", 606));
}

// Tests the version of the method LogMemoryUsage() that accepts a vector of
// event codes.
TEST_F(LoggerTest, LogMemoryUsageMultiDimension) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kLedgerMemoryUsageAggregatedReportId,
      testing::all_report_types::kLedgerMemoryUsageHistogramReportId};

  // Use a metric ID for the wrong type of metric. Expect kInvalidArguments.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogMemoryUsage(
                testing::all_report_types::kLoginModuleFrameRateMetricId,
                std::vector<uint32_t>({45, 46}), "component6", 606));

  // Use no event codes when the metric has two dimension. Expect
  // kInvalidArguments.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogMemoryUsage(
                testing::all_report_types::kLedgerMemoryUsageMetricId,
                std::vector<uint32_t>({}), "component6", 606));

  // Use one event code when the metric has two dimension. Expect
  // kInvalidArguments.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogMemoryUsage(
                testing::all_report_types::kLedgerMemoryUsageMetricId,
                std::vector<uint32_t>({45}), "component6", 606));

  // Use three event codes when the metric has two dimension. Expect
  // kInvalidArguments.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogMemoryUsage(
                testing::all_report_types::kLedgerMemoryUsageMetricId,
                std::vector<uint32_t>({45, 46, 47}), "component6", 606));

  // There is no max_event_code set for the second dimension so only the
  // specified codes of 46 or 47  are allowed.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogMemoryUsage(
                testing::all_report_types::kLedgerMemoryUsageMetricId,
                std::vector<uint32_t>({1, 45}), "component6", 606));

  // All good
  ASSERT_EQ(kOK, logger_->LogMemoryUsage(
                     testing::all_report_types::kLedgerMemoryUsageMetricId,
                     std::vector<uint32_t>({1, 46}), "component6", 606));
  uint64_t expected_packed_event_code = (1) | (46 << 10);

  EXPECT_TRUE(CheckNumericEventObservations(
      expected_report_ids, expected_packed_event_code, "component6", 606,
      observation_store_.get(), update_recipient_.get()));

  // Clear the FakeObservationStore.
  ResetObservationStore();

  // No problem with the first event code being zero.
  ASSERT_EQ(kOK, logger_->LogMemoryUsage(
                     testing::all_report_types::kLedgerMemoryUsageMetricId,
                     std::vector<uint32_t>({0, 46}), "component6", 606));
  expected_packed_event_code = (0) | (46 << 10);

  EXPECT_TRUE(CheckNumericEventObservations(
      expected_report_ids, expected_packed_event_code, "component6", 606,
      observation_store_.get(), update_recipient_.get()));
}

// Tests the method LogIntHistogram().
TEST_F(LoggerTest, LogIntHistogram) {
  std::vector<uint32_t> indices = {0, 1, 2, 3};
  std::vector<uint32_t> counts = {100, 101, 102, 103};
  auto histogram = NewHistogram(indices, counts);
  ASSERT_EQ(kOK, logger_->LogIntHistogram(
                     testing::all_report_types::kFileSystemWriteTimesMetricId,
                     47, "component7", std::move(histogram)));
  Observation2 observation;
  uint32_t expected_report_id =
      testing::all_report_types::kFileSystemWriteTimesHistogramReportId;
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
  ASSERT_EQ(kOK, logger_->LogString(
                     testing::all_report_types::kModuleDownloadsMetricId,
                     "www.mymodule.com"));
  std::vector<Observation2> observations(2);
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kModuleDownloadsHeavyHittersReportId,
      testing::all_report_types::kModuleDownloadsWithThresholdReportId};
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
  ASSERT_EQ(kOK, logger_->LogCustomEvent(
                     testing::all_report_types::kModuleInstallsMetricId,
                     std::move(custom_event)));
  Observation2 observation;
  uint32_t expected_report_id =
      testing::all_report_types::kModuleInstallsDetailedDataReportId;
  ASSERT_TRUE(FetchSingleObservation(&observation, expected_report_id,
                                     observation_store_.get(),
                                     update_recipient_.get()));
  ASSERT_TRUE(observation.has_custom());
  const CustomObservation& custom_observation = observation.custom();
  for (auto i = 0u; i < values.size(); i++) {
    auto obs_dimension = custom_observation.values().at(dimension_names[i]);
#ifdef PROTO_LITE
#else
    EXPECT_TRUE(MessageDifferencer::Equals(obs_dimension, values[i]));
#endif
  }
}

// Tests that the expected number of locally aggregated Observations are
// generated when no events have been logged.
TEST_F(LoggerTest, CheckNumAggregatedObsNoEvents) {
  ASSERT_EQ(kOK, GenerateAggregatedObservations(
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
  ASSERT_EQ(kOK, logger_->LogEvent(
                     testing::all_report_types::kDeviceBootsMetricId, 0));
  // Check that no immediate Observation was generated.
  std::vector<Observation2> immediate_observations(0);
  std::vector<uint32_t> expected_immediate_report_ids = {};
  ASSERT_TRUE(
      FetchObservations(&immediate_observations, expected_immediate_report_ids,
                        observation_store_.get(), update_recipient_.get()));
  // Generate locally aggregated observations for the current day index.
  ASSERT_EQ(kOK, GenerateAggregatedObservations(
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
  auto expected_params = expected_aggregation_params_;
  // Log 2 occurrences of event code 0 for the DeviceBoots metric, which has 1
  // locally aggregated report and no immediate reports.
  ASSERT_EQ(kOK, logger_->LogEvent(
                     testing::all_report_types::kDeviceBootsMetricId, 0));
  ASSERT_EQ(kOK, logger_->LogEvent(
                     testing::all_report_types::kDeviceBootsMetricId, 0));
  // Log 2 occurrences of distinct event codes for the FeaturesActive metric,
  // which has 1 locally aggregated report and no immediate reports.
  ASSERT_EQ(kOK, logger_->LogEvent(
                     testing::all_report_types::kFeaturesActiveMetricId, 0));
  ASSERT_EQ(kOK, logger_->LogEvent(
                     testing::all_report_types::kFeaturesActiveMetricId, 1));
  // Log 2 event counts for event code 0, for distinct components, for the
  // SettingsChanged metric. This metric has 1 locally aggregated report and no
  // immediate reports.
  ASSERT_EQ(kOK, logger_->LogEventCount(
                     testing::all_report_types::kSettingsChangedMetricId, 0,
                     "component_A", 0, 10));
  ASSERT_EQ(kOK, logger_->LogEventCount(
                     testing::all_report_types::kSettingsChangedMetricId, 0,
                     "component_B", 0, 15));
  // Check that no immediate Observations were generated.
  std::vector<Observation2> immediate_observations(0);
  std::vector<uint32_t> expected_immediate_report_ids = {};
  ASSERT_TRUE(
      FetchObservations(&immediate_observations, expected_immediate_report_ids,
                        observation_store_.get(), update_recipient_.get()));
  // Generate locally aggregated observations for the current day index.
  ASSERT_EQ(kOK, GenerateAggregatedObservations(
                     CurrentDayIndex(MetricDefinition::UTC)));
  // Update |expected_aggregation_params_| to account for the events logged by
  // this test. In addition to the initial values, we expect 1 Observation for
  // the SettingsChanged_PerDeviceCount report, for each of the 2 window sizes
  // of the report, for each of the 2 components appearing in logged events.
  expected_aggregation_params_.daily_num_obs += 4;
  expected_aggregation_params_.num_obs_per_report
      [testing::all_report_types::kSettingsChangedMetricReportId] += 4;
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
  ASSERT_EQ(kOK, logger_->LogEvent(
                     testing::all_report_types::kEventsOccurredMetricId, 0));
  ASSERT_EQ(kOK, logger_->LogEvent(
                     testing::all_report_types::kEventsOccurredMetricId, 0));
  ASSERT_EQ(kOK, logger_->LogEvent(
                     testing::all_report_types::kEventsOccurredMetricId, 2));
  // Check that each of the 3 logged events resulted in an immediate
  // Observation.
  std::vector<Observation2> immediate_observations(3);
  std::vector<uint32_t> expected_immediate_report_ids(
      3, testing::all_report_types::kEventsOccurredGlobalCountReportId);
  ASSERT_TRUE(
      FetchObservations(&immediate_observations, expected_immediate_report_ids,
                        observation_store_.get(), update_recipient_.get()));
  // Clear the FakeObservationStore.
  ResetObservationStore();
  // Generate locally aggregated observations for the current day index.
  ASSERT_EQ(kOK, GenerateAggregatedObservations(
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
  const auto current_day_index = CurrentDayIndex(MetricDefinition::UTC);
  // Generate locally aggregated Observations without logging any events.
  ASSERT_EQ(kOK, GenerateAggregatedObservations(current_day_index));
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
  const auto current_day_index = CurrentDayIndex(MetricDefinition::UTC);
  // Log 2 occurrences of event code 0 for the DeviceBoots metric, which has 1
  // locally aggregated report and no immediate reports.
  ASSERT_EQ(kOK,
            logger_->LogEvent(
                testing::unique_actives_noise_free::kDeviceBootsMetricId, 0));
  ASSERT_EQ(kOK,
            logger_->LogEvent(
                testing::unique_actives_noise_free::kDeviceBootsMetricId, 0));
  // Log 2 occurrences of different event codes for the SomeFeaturesActive
  // metric, which has 1 locally aggregated report and no immediate reports.
  ASSERT_EQ(
      kOK, logger_->LogEvent(
               testing::unique_actives_noise_free::kFeaturesActiveMetricId, 0));
  ASSERT_EQ(
      kOK, logger_->LogEvent(
               testing::unique_actives_noise_free::kFeaturesActiveMetricId, 1));
  // Generate locally aggregated observations for the current day index.
  ASSERT_EQ(kOK, GenerateAggregatedObservations(current_day_index));
  // Form the expected observations for the current day index.
  auto expected_obs = MakeNullExpectedUniqueActivesObservations(
      expected_aggregation_params_, current_day_index);
  expected_obs[{testing::unique_actives_noise_free::kDeviceBootsMetricReportId,
                current_day_index}] = {{1, {true, false}}};
  expected_obs[{
      testing::unique_actives_noise_free::kFeaturesActiveMetricReportId,
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
TEST_F(UniqueActivesLoggerTest, CheckUniqueActivesObsValuesMultiDay) {
  const auto& expected_id =
      testing::unique_actives_noise_free::kEventsOccurredMetricReportId;
  const auto start_day_index = CurrentDayIndex(MetricDefinition::UTC);

  // Form expected Observations for the 10 days of logging.
  std::vector<ExpectedUniqueActivesObservations> expected_obs(10);
  for (uint32_t i = 0; i < expected_obs.size(); i++) {
    expected_obs[i] = MakeNullExpectedUniqueActivesObservations(
        expected_aggregation_params_, start_day_index + i);
  }
  expected_obs[0][{expected_id, start_day_index}] = {
      {1, {false, true, true, true, true}},
      {7, {false, true, true, true, true}}};
  expected_obs[1][{expected_id, start_day_index + 1}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[2][{expected_id, start_day_index + 2}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[3][{expected_id, start_day_index + 3}] = {
      {1, {false, true, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[4][{expected_id, start_day_index + 4}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[5][{expected_id, start_day_index + 5}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[6][{expected_id, start_day_index + 6}] = {
      {1, {false, true, true, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[7][{expected_id, start_day_index + 7}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_obs[8][{expected_id, start_day_index + 8}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_obs[9][{expected_id, start_day_index + 9}] = {
      {1, {false, true, false, true, false}},
      {7, {false, true, true, true, false}}};

  for (uint32_t i = 0; i < 10; i++) {
    if (i < 10) {
      for (uint32_t event_code = 1; event_code < 5; event_code++) {
        if (i % (3 * event_code) == 0) {
          ASSERT_EQ(
              kOK,
              logger_->LogEvent(
                  testing::unique_actives_noise_free::kEventsOccurredMetricId,
                  event_code));
        }
      }
    }
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Advance the Logger's clock by 1 day.
    AdvanceDay(1);
    // Generate locally aggregated Observations for the previous day
    // index.
    ASSERT_EQ(kOK, GenerateAggregatedObservations(
                       CurrentDayIndex(MetricDefinition::UTC) - 1));
    // Check the generated Observations against the expectation.
    EXPECT_TRUE(CheckUniqueActivesObservations(
        expected_obs[i], observation_store_.get(), update_recipient_.get()));
    // Garbage-collect the LocalAggregateStore for the current day index.
    ASSERT_EQ(kOK, GarbageCollectAggregateStore(
                       CurrentDayIndex(MetricDefinition::UTC)));
  }
}

// Generate Observations without logging any events, and check that the
// resulting Observations are as expected: 1 ReportParticipationObservation for
// each PER_DEVICE_NUMERIC_STATS report in the config, and no
// PerDeviceNumericObservations.
TEST_F(PerDeviceNumericLoggerTest, CheckPerDeviceNumericObsValuesNoEvents) {
  const auto day_index = CurrentDayIndex(MetricDefinition::UTC);
  EXPECT_EQ(kOK, GenerateAggregatedObservations(day_index));
  const auto& expected_report_participation_obs =
      MakeExpectedReportParticipationObservations(
          testing::per_device_numeric_stats::kExpectedAggregationParams,
          day_index);
  EXPECT_TRUE(CheckPerDeviceNumericObservations(
      {}, expected_report_participation_obs, observation_store_.get(),
      update_recipient_.get()));
}

// Tests that Observations with the expected values are generated when events
// have been logged for EVENT_COUNT metrics with a PER_DEVICE_NUMERIC_STATS
// report, on a single day.
TEST_F(PerDeviceNumericLoggerTest, CheckPerDeviceNumericObsValuesSingleDay) {
  const auto day_index = CurrentDayIndex(MetricDefinition::UTC);
  // Log several events on |day_index|.
  EXPECT_EQ(kOK,
            logger_->LogEventCount(
                testing::per_device_numeric_stats::kConnectionFailuresMetricId,
                0u, "component_A", 0, 5));
  EXPECT_EQ(kOK,
            logger_->LogEventCount(
                testing::per_device_numeric_stats::kConnectionFailuresMetricId,
                0u, "component_B", 0, 5));
  EXPECT_EQ(kOK,
            logger_->LogEventCount(
                testing::per_device_numeric_stats::kConnectionFailuresMetricId,
                0u, "component_A", 0, 5));
  EXPECT_EQ(kOK,
            logger_->LogEventCount(
                testing::per_device_numeric_stats::kConnectionFailuresMetricId,
                1u, "component_A", 0, 5));
  EXPECT_EQ(kOK,
            logger_->LogEventCount(
                testing::per_device_numeric_stats::kSettingsChangedMetricId, 0u,
                "component_C", 0, 5));
  EXPECT_EQ(kOK,
            logger_->LogEventCount(
                testing::per_device_numeric_stats::kSettingsChangedMetricId, 0u,
                "component_C", 0, 5));
  // The ConnectionFailures metric has an immediate report of type
  // EVENT_COMPONENT_OCCURRENCE_COUNT. Check that 4 immediate Observations were
  // generated for that report.
  std::vector<Observation2> immediate_observations(4);
  std::vector<uint32_t> expected_immediate_report_ids(
      4, testing::per_device_numeric_stats::
             kConnectionFailuresGlobalCountReportId);
  EXPECT_TRUE(
      FetchObservations(&immediate_observations, expected_immediate_report_ids,
                        observation_store_.get(), update_recipient_.get()));

  // Clear the FakeObservationStore.
  ResetObservationStore();
  // Generate locally aggregated Observations for |day_index|.
  EXPECT_EQ(kOK, GenerateAggregatedObservations(day_index));
  // Form the expected locally aggregated Observations.
  auto expected_report_participation_obs =
      MakeExpectedReportParticipationObservations(
          testing::per_device_numeric_stats::kExpectedAggregationParams,
          day_index);
  ExpectedPerDeviceNumericObservations expected_per_device_numeric_obs;
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kConnectionFailuresMetricReportId,
      day_index}][1] = {
      {"component_A", 0u, 10}, {"component_A", 1u, 5}, {"component_B", 0u, 5}};
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kSettingsChangedMetricReportId,
      day_index}][7] = {{"component_C", 0u, 10}};
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kSettingsChangedMetricReportId,
      day_index}][30] = {{"component_C", 0u, 10}};
  EXPECT_TRUE(CheckPerDeviceNumericObservations(
      expected_per_device_numeric_obs, expected_report_participation_obs,
      observation_store_.get(), update_recipient_.get()));
}

// Tests that Observations with the expected values are generated when events
// have been logged for a FRAME_RATE metric with a PER_DEVICE_NUMERIC_STATS
// report, on a single day.
TEST_F(PerDeviceNumericLoggerTest,
       CheckPerDeviceNumericObsValuesFrameRateSingleDay) {
  const auto day_index = CurrentDayIndex(MetricDefinition::UTC);
  // Log several events on |day_index|.
  EXPECT_EQ(
      kOK, logger_->LogFrameRate(
               testing::per_device_numeric_stats::kLoginModuleFrameRateMetricId,
               0u, "component_A", 5.0));
  EXPECT_EQ(
      kOK, logger_->LogFrameRate(
               testing::per_device_numeric_stats::kLoginModuleFrameRateMetricId,
               0u, "component_B", 4.2));
  EXPECT_EQ(
      kOK, logger_->LogFrameRate(
               testing::per_device_numeric_stats::kLoginModuleFrameRateMetricId,
               0u, "component_A", 3.75));
  EXPECT_EQ(
      kOK, logger_->LogFrameRate(
               testing::per_device_numeric_stats::kLoginModuleFrameRateMetricId,
               1u, "component_A", 2.0));
  EXPECT_EQ(
      kOK, logger_->LogFrameRate(
               testing::per_device_numeric_stats::kLoginModuleFrameRateMetricId,
               0u, "component_C", 7.9));
  EXPECT_EQ(
      kOK, logger_->LogFrameRate(
               testing::per_device_numeric_stats::kLoginModuleFrameRateMetricId,
               0u, "component_C", 4.0));
  // The LoginModuleFrameRate metric has an immediate report of type
  // NUMERIC_AGGREGATION. Check that 6 immediate Observations were
  // generated for that report.
  std::vector<Observation2> immediate_observations(6);
  std::vector<uint32_t> expected_immediate_report_ids(
      6, testing::per_device_numeric_stats::
             kLoginModuleFrameRateAggregatedReportId);
  EXPECT_TRUE(
      FetchObservations(&immediate_observations, expected_immediate_report_ids,
                        observation_store_.get(), update_recipient_.get()));

  // Clear the FakeObservationStore.
  ResetObservationStore();
  // Generate locally aggregated Observations for |day_index|.
  EXPECT_EQ(kOK, GenerateAggregatedObservations(day_index));
  // Form the expected locally aggregated Observations.
  auto expected_report_participation_obs =
      MakeExpectedReportParticipationObservations(
          testing::per_device_numeric_stats::kExpectedAggregationParams,
          day_index);
  ExpectedPerDeviceNumericObservations expected_per_device_numeric_obs;
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kLoginModuleFrameRateMinMetricReportId,
      day_index}][1] = {{"component_A", 0u, 3750},
                        {"component_A", 1u, 2000},
                        {"component_B", 0u, 4200},
                        {"component_C", 0u, 4000}};
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kLoginModuleFrameRateMinMetricReportId,
      day_index}][7] = {{"component_A", 0u, 3750},
                        {"component_A", 1u, 2000},
                        {"component_B", 0u, 4200},
                        {"component_C", 0u, 4000}};
  EXPECT_TRUE(CheckPerDeviceNumericObservations(
      expected_per_device_numeric_obs, expected_report_participation_obs,
      observation_store_.get(), update_recipient_.get()));
}

// Tests that Observations with the expected values are generated when events
// have been logged for a MEMORY_USAGE metric with a PER_DEVICE_NUMERIC_STATS
// report, on a single day.
TEST_F(PerDeviceNumericLoggerTest,
       CheckPerDeviceNumericObsValuesMemoryUsageSingleDay) {
  const auto day_index = CurrentDayIndex(MetricDefinition::UTC);
  // Log several events on |day_index|.
  EXPECT_EQ(kOK,
            logger_->LogMemoryUsage(
                testing::per_device_numeric_stats::kLedgerMemoryUsageMetricId,
                std::vector<uint32_t>{0u, 0u}, "component_A", 5));
  EXPECT_EQ(kOK,
            logger_->LogMemoryUsage(
                testing::per_device_numeric_stats::kLedgerMemoryUsageMetricId,
                std::vector<uint32_t>{0u, 0u}, "component_B", 4));
  EXPECT_EQ(kOK,
            logger_->LogMemoryUsage(
                testing::per_device_numeric_stats::kLedgerMemoryUsageMetricId,
                std::vector<uint32_t>{0u, 0u}, "component_A", 3));
  EXPECT_EQ(kOK,
            logger_->LogMemoryUsage(
                testing::per_device_numeric_stats::kLedgerMemoryUsageMetricId,
                std::vector<uint32_t>{1u, 0u}, "component_A", 2));
  EXPECT_EQ(kOK,
            logger_->LogMemoryUsage(
                testing::per_device_numeric_stats::kLedgerMemoryUsageMetricId,
                std::vector<uint32_t>{0u, 0u}, "component_C", 7));
  EXPECT_EQ(kOK,
            logger_->LogMemoryUsage(
                testing::per_device_numeric_stats::kLedgerMemoryUsageMetricId,
                std::vector<uint32_t>{0u, 0u}, "component_C", 4));
  // The LedgerMemoryUsage metric has an immediate report of type
  // NUMERIC_AGGREGATION. Check that 6 immediate Observations were
  // generated for that report.
  std::vector<Observation2> immediate_observations(6);
  std::vector<uint32_t> expected_immediate_report_ids(
      6,
      testing::per_device_numeric_stats::kLedgerMemoryUsageAggregatedReportId);
  EXPECT_TRUE(
      FetchObservations(&immediate_observations, expected_immediate_report_ids,
                        observation_store_.get(), update_recipient_.get()));

  // Clear the FakeObservationStore.
  ResetObservationStore();
  // Generate locally aggregated Observations for |day_index|.
  EXPECT_EQ(kOK, GenerateAggregatedObservations(day_index));
  // Form the expected locally aggregated Observations.
  auto expected_report_participation_obs =
      MakeExpectedReportParticipationObservations(
          testing::per_device_numeric_stats::kExpectedAggregationParams,
          day_index);
  ExpectedPerDeviceNumericObservations expected_per_device_numeric_obs;
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kLedgerMemoryUsageMaxMetricReportId,
      day_index}][1] = {
      {"component_A", PackEventCodes(std::vector<uint32_t>{0u, 0u}), 5},
      {"component_A", PackEventCodes(std::vector<uint32_t>{1u, 0u}), 2},
      {"component_B", PackEventCodes(std::vector<uint32_t>{0u, 0u}), 4},
      {"component_C", PackEventCodes(std::vector<uint32_t>{0u, 0u}), 7}};
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kLedgerMemoryUsageMaxMetricReportId,
      day_index}][7] = {
      {"component_A", PackEventCodes(std::vector<uint32_t>{0u, 0u}), 5},
      {"component_A", PackEventCodes(std::vector<uint32_t>{1u, 0u}), 2},
      {"component_B", PackEventCodes(std::vector<uint32_t>{0u, 0u}), 4},
      {"component_C", PackEventCodes(std::vector<uint32_t>{0u, 0u}), 7}};

  EXPECT_TRUE(CheckPerDeviceNumericObservations(
      expected_per_device_numeric_obs, expected_report_participation_obs,
      observation_store_.get(), update_recipient_.get()));
}

// Checks that PerDeviceNumericObservations with the expected values are
// generated when some events have been logged for an EVENT_COUNT metric with a
// PER_DEVICE_NUMERIC_STATS report over multiple days and
// GenerateAggregatedObservations() is called each day.
//
// Logged events for the SettingsChanged_PerDeviceCount metric on the i-th day:
//
//  i            (component, event code, count)
// -----------------------------------------------------------------------
//  0
//  1          ("A", 1, 3)
//  2          ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
//  3          ("A", 1, 3)
//  4          ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
//  5          ("A", 1, 3)
//  6          ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
//  7          ("A", 1, 3)
//  8          ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
//  9          ("A", 1, 3)
//
// Expected PerDeviceNumericObservations for the SettingsChanged_PerDeviceCount
// report on the i-th day:
//
// (i, window size)          (component, event code, count)
// -----------------------------------------------------------------------
// (0, 7)
// (0, 30)
// (1, 7)     ("A", 1,  3)
// (1, 30)    ("A", 1,  3)
// (2, 7)     ("A", 1,  6),  ("A", 2,  3), ("B", 1, 2)
// (2, 30)    ("A", 1,  6),  ("A", 2,  3), ("B", 1, 2)
// (3, 7)     ("A", 1,  9),  ("A", 2,  3), ("B", 1, 2)
// (3, 30)    ("A", 1,  9),  ("A", 2,  3), ("B", 1, 2)
// (4, 7)     ("A", 1, 12),  ("A", 2,  6), ("B", 1, 4), ("B", 2, 2)
// (4, 30)    ("A", 1, 12),  ("A", 2,  6), ("B", 1, 4), ("B", 2, 2)
// (5, 7)     ("A", 1, 15),  ("A", 2,  6), ("B", 1, 4), ("B", 2, 2)
// (5, 30)    ("A", 1, 15),  ("A", 2,  6), ("B", 1, 4), ("B", 2, 2)
// (6, 7)     ("A", 1, 18),  ("A", 2,  9), ("B", 1, 6), ("B", 2, 2)
// (6, 30)    ("A", 1, 18),  ("A", 2,  9), ("B", 1, 6), ("B", 2, 2)
// (7, 7)     ("A", 1, 21),  ("A", 2,  9), ("B", 1, 6), ("B", 2, 2)
// (7, 30)    ("A", 1, 21),  ("A", 2,  9), ("B", 1, 6), ("B", 2, 2)
// (8, 7)     ("A", 1, 21),  ("A", 2, 12), ("B", 1, 8), ("B", 2, 4)
// (8, 30)    ("A", 1, 24),  ("A", 2, 12), ("B", 1, 8), ("B", 2, 4)
// (9, 7)     ("A", 1, 21),  ("A", 2,  9), ("B", 1, 6), ("B", 2, 4)
// (9, 30)    ("A", 1, 27),  ("A", 2, 12), ("B", 1, 8), ("B", 2, 4)
//
// In addition, expect 1 ReportParticipationObservations for each day, for each
// report in the registry.
TEST_F(PerDeviceNumericLoggerTest, CheckPerDeviceNumericObsValuesMultiDay) {
  const auto start_day_index = CurrentDayIndex(MetricDefinition::UTC);
  const auto& expected_id =
      testing::per_device_numeric_stats::kSettingsChangedMetricReportId;
  // Form expected Observations for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedPerDeviceNumericObservations>
      expected_per_device_numeric_obs(num_days);
  std::vector<ExpectedReportParticipationObservations>
      expected_report_participation_obs(num_days);
  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_report_participation_obs[offset] =
        MakeExpectedReportParticipationObservations(
            expected_aggregation_params_, start_day_index + offset);
  }
  expected_per_device_numeric_obs[0] = {};
  expected_per_device_numeric_obs[1][{expected_id, start_day_index + 1}] = {
      {7, {{"A", 1u, 3}}}, {30, {{"A", 1u, 3}}}};
  expected_per_device_numeric_obs[2][{expected_id, start_day_index + 2}] = {
      {7, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}},
      {30, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}}};
  expected_per_device_numeric_obs[3][{expected_id, start_day_index + 3}] = {
      {7, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}},
      {30, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}}};
  expected_per_device_numeric_obs[4][{expected_id, start_day_index + 4}] = {
      {7, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
      {30, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[5][{expected_id, start_day_index + 5}] = {
      {7, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
      {30, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[6][{expected_id, start_day_index + 6}] = {
      {7, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
      {30, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[7][{expected_id, start_day_index + 7}] = {
      {7, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
      {30, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[8][{expected_id, start_day_index + 8}] = {
      {7, {{"A", 1u, 21}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}},
      {30, {{"A", 1u, 24}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}}};
  expected_per_device_numeric_obs[9][{expected_id, start_day_index + 9}] = {
      {7, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 4}}},
      {30, {{"A", 1u, 27}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}}};

  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex(MetricDefinition::UTC);
    for (uint32_t event_code = 1; event_code < 3; event_code++) {
      if (offset > 0 && offset % event_code == 0) {
        EXPECT_EQ(
            kOK,
            logger_->LogEventCount(
                testing::per_device_numeric_stats::kSettingsChangedMetricId,
                event_code, "A", 0, 3));
      }
      if (offset > 0 && offset % (2 * event_code) == 0) {
        EXPECT_EQ(
            kOK,
            logger_->LogEventCount(
                testing::per_device_numeric_stats::kSettingsChangedMetricId,
                event_code, "B", 0, 2));
      }
    }
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Generate locally aggregated Observations.
    EXPECT_EQ(kOK, GenerateAggregatedObservations(day_index));
    EXPECT_TRUE(CheckPerDeviceNumericObservations(
        expected_per_device_numeric_obs[offset],
        expected_report_participation_obs[offset], observation_store_.get(),
        update_recipient_.get()))
        << "offset = " << offset;
    AdvanceDay(1);
  }
}

// Tests that the expected Observations are generated when events are logged for
// over multiple days for an ELAPSED_TIME metric with PER_DEVICE_NUMERIC_STATS
// reports with multiple aggregation types, when Observations are backfilled for
// some days during that period, and when the LocalAggregatedStore is
// garbage-collected after each call to GenerateObservations().
//
// Logged events for the StreamingTime metric on the i-th day:
//
//  i            (component, event code, count)
// -----------------------------------------------------------------------
//  0
//  1          ("A", 1, 3)
//  2          ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
//  3          ("A", 1, 3)
//  4          ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
//  5          ("A", 1, 3)
//  6          ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
//  7          ("A", 1, 3)
//  8          ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
//
// Expected PerDeviceNumericObservations for the
// StreamingTime_PerDeviceTotal report on the i-th day:
//
// (day, window size)            (event code, component, total)
// ---------------------------------------------------------------------------
// (0, 1)
// (0, 7)
// (1, 1)     ("A", 1,  3)
// (1, 7)     ("A", 1,  3)
// (2, 1)     ("A", 1,  3), ("A", 2,  3), ("B", 1, 2)
// (2, 7)     ("A", 1,  6), ("A", 2,  3), ("B", 1, 2)
// (3, 1)     ("A", 1,  3)
// (3, 7)     ("A", 1,  9), ("A", 2,  3), ("B", 1, 2)
// (4, 1)     ("A", 1,  3), ("A", 2,  3), ("B", 1, 2), ("B", 2, 2)
// (4, 7)     ("A", 1, 12), ("A", 2,  6), ("B", 1, 4), ("B", 2, 2)
// (5, 1)     ("A", 1,  3)
// (5, 7)     ("A", 1, 15), ("A", 2,  6), ("B", 1, 4), ("B", 2, 2)
// (6, 1)     ("A", 1,  3), ("A", 2,  3), ("B", 1, 2)
// (6, 7)     ("A", 1, 18), ("A", 2,  9), ("B", 1, 6), ("B", 2, 2)
// (7, 1)     ("A", 1,  3)
// (7, 7)     ("A", 1, 21), ("A", 2,  9), ("B", 1, 6), ("B", 2, 2)
// (8, 1)     ("A", 1,  3), ("A", 2,  3), ("B", 1, 2), ("B", 2, 2)
// (8, 7)     ("A", 1, 21), ("A", 2, 12), ("B", 1, 8), ("B", 2, 4)
//
//
// Expected PerDeviceNumericObservations for the
// StreamingTime_PerDeviceMin report on the i-th day:
//
// (day, window size)            (event code, component, total)
// ---------------------------------------------------------------------------
// (0, 1)
// (0. 7)
// (1, 1)     ("A", 1, 3)
// (1, 7)     ("A", 1, 3)
// (2, 1)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
// (2, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
// (3, 1)     ("A", 1, 3)
// (3, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
// (4, 1)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
// (4, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
// (5, 1)     ("A", 1, 3)
// (5, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
// (6, 1)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
// (6, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
// (7, 1)     ("A", 1, 3)
// (7, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
// (8, 1)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
// (8, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
//
// Expected PerDeviceNumericObservations for the
// StreamingTime_PerDeviceMax report on the i-th day:
//
// (day, window size)            (event code, component, total)
// ---------------------------------------------------------------------------
// (0, 1)
// (0. 7)
// (1, 1)     ("A", 1, 3)
// (1, 7)     ("A", 1, 3)
// (2, 1)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
// (2, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
// (3, 1)     ("A", 1, 3)
// (3, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
// (4, 1)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
// (4, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
// (5, 1)     ("A", 1, 3)
// (5, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
// (6, 1)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
// (6, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
// (7, 1)     ("A", 1, 3)
// (7, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
// (8, 1)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
// (8, 7)     ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
//
// In addition, expect 1 ReportParticipationObservation each day for each
// report in the registry.
TEST_F(PerDeviceNumericLoggerTest, ElapsedTimeCheckObservationValues) {
  const auto start_day_index = CurrentDayIndex(MetricDefinition::UTC);
  const auto& total_report_id =
      testing::per_device_numeric_stats::kStreamingTimeTotalMetricReportId;
  const auto& min_report_id =
      testing::per_device_numeric_stats::kStreamingTimeMinMetricReportId;
  const auto& max_report_id =
      testing::per_device_numeric_stats::kStreamingTimeMaxMetricReportId;
  // Form expected Observations for the 9 days of logging.
  uint32_t num_days = 9;
  std::vector<ExpectedPerDeviceNumericObservations>
      expected_per_device_numeric_obs(num_days);
  std::vector<ExpectedReportParticipationObservations>
      expected_report_participation_obs(num_days);

  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_report_participation_obs[offset] =
        MakeExpectedReportParticipationObservations(
            expected_aggregation_params_, start_day_index + offset);
  }
  expected_per_device_numeric_obs[0] = {};
  expected_per_device_numeric_obs[1][{total_report_id, start_day_index + 1}] = {
      {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 3}}}};
  expected_per_device_numeric_obs[2][{total_report_id, start_day_index + 2}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}},
      {7, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}}};
  expected_per_device_numeric_obs[3][{total_report_id, start_day_index + 3}] = {
      {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}}};
  expected_per_device_numeric_obs[4][{total_report_id, start_day_index + 4}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
      {7, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[5][{total_report_id, start_day_index + 5}] = {
      {1, {{"A", 1u, 3}}},
      {7, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[6][{total_report_id, start_day_index + 6}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}},
      {7, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[7][{total_report_id, start_day_index + 7}] = {
      {1, {{"A", 1u, 3}}},
      {7, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[8][{total_report_id, start_day_index + 8}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
      {7, {{"A", 1u, 21}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}}};

  expected_per_device_numeric_obs[1][{min_report_id, start_day_index + 1}] = {
      {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 3}}}};
  expected_per_device_numeric_obs[2][{min_report_id, start_day_index + 2}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}}};
  expected_per_device_numeric_obs[3][{min_report_id, start_day_index + 3}] = {
      {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}}};
  expected_per_device_numeric_obs[4][{min_report_id, start_day_index + 4}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[5][{min_report_id, start_day_index + 5}] = {
      {1, {{"A", 1u, 3}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[6][{min_report_id, start_day_index + 6}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[7][{min_report_id, start_day_index + 7}] = {
      {1, {{"A", 1u, 3}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[8][{min_report_id, start_day_index + 8}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};

  expected_per_device_numeric_obs[8][{max_report_id, start_day_index + 8}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[1][{max_report_id, start_day_index + 1}] = {
      {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 3}}}};
  expected_per_device_numeric_obs[2][{max_report_id, start_day_index + 2}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}}};
  expected_per_device_numeric_obs[3][{max_report_id, start_day_index + 3}] = {
      {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}}};
  expected_per_device_numeric_obs[4][{max_report_id, start_day_index + 4}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[5][{max_report_id, start_day_index + 5}] = {
      {1, {{"A", 1u, 3}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[6][{max_report_id, start_day_index + 6}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[7][{max_report_id, start_day_index + 7}] = {
      {1, {{"A", 1u, 3}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
  expected_per_device_numeric_obs[8][{max_report_id, start_day_index + 8}] = {
      {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
      {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};

  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex(MetricDefinition::UTC);
    for (uint32_t event_code = 1; event_code < 3; event_code++) {
      if (offset > 0 && offset % event_code == 0) {
        EXPECT_EQ(kOK,
                  logger_->LogElapsedTime(
                      testing::per_device_numeric_stats::kStreamingTimeMetricId,
                      event_code, "A", 3));
      }
      if (offset > 0 && offset % (2 * event_code) == 0) {
        EXPECT_EQ(kOK,
                  logger_->LogElapsedTime(
                      testing::per_device_numeric_stats::kStreamingTimeMetricId,
                      event_code, "B", 2));
      }
    }
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Generate locally aggregated Observations.
    EXPECT_EQ(kOK, GenerateAggregatedObservations(day_index));
    EXPECT_TRUE(CheckPerDeviceNumericObservations(
        expected_per_device_numeric_obs[offset],
        expected_report_participation_obs[offset], observation_store_.get(),
        update_recipient_.get()))
        << "offset = " << offset;
    AdvanceDay(1);
  }
}

}  // namespace logger
}  // namespace cobalt

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/logger.h"

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./gtest.h"
#include "./observation.pb.h"
#include "./observation2.pb.h"
#include "encoder/encoder.h"
#include "encoder/memory_observation_store.h"
#include "encoder/observation_store.h"
#include "encoder/send_retryer.h"
#include "encoder/shipping_manager.h"
#include "logger/encoder.h"
#include "logger/project_context.h"
#include "logger/status.h"
#include "util/encrypted_message_util.h"

namespace cobalt {

using encoder::ClientSecret;
using encoder::LegacyShippingManager;
using encoder::MemoryObservationStore;
using encoder::ObservationStoreUpdateRecipient;
using encoder::ObservationStoreWriterInterface;
using encoder::SendRetryerInterface;
using encoder::ShippingManager;
using encoder::SystemDataInterface;
using google::protobuf::RepeatedPtrField;
using google::protobuf::util::MessageDifferencer;
using util::EncryptedMessageMaker;
using util::MessageDecrypter;

namespace logger {

namespace {
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

class FakeObservationStore : public ObservationStoreWriterInterface {
 public:
  StoreStatus AddEncryptedObservation(
      std::unique_ptr<EncryptedMessage> message,
      std::unique_ptr<ObservationMetadata> metadata) override {
    messages_received.emplace_back(std::move(message));
    metadata_received.emplace_back(std::move(metadata));
    return kOk;
  }

  std::vector<std::unique_ptr<EncryptedMessage>> messages_received;
  std::vector<std::unique_ptr<ObservationMetadata>> metadata_received;
};

class TestUpdateRecipient : public ObservationStoreUpdateRecipient {
 public:
  void NotifyObservationsAdded() override { invocation_count++; }

  int invocation_count = 0;
};

}  // namespace

class LoggerTest : public ::testing::Test {
 protected:
  void SetUp() {
    auto metric_definitions = std::make_unique<MetricDefinitions>();
    ASSERT_TRUE(PopulateMetricDefinitions(metric_definitions.get()));
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
    logger_.reset(new Logger(encoder_.get(), observation_writer_.get(),
                             project_context_.get()));
  }

  // Populates |observations| with the contents of the FakeObservationStore.
  // |observations| should be a vector whose size is equal to the number
  // of expected observations. Checks the the ObservationStore contains
  // that number of Observations and that the report_ids of the Observations
  // are equal to |expected_report_ids|. Returns true iff all checks pass.
  bool FetchImmediateObservations(
      std::vector<Observation2>* observations,
      const std::vector<uint32_t>& expected_report_ids) {
    CHECK(observations);
    size_t expected_num_received = observations->size();
    CHECK(expected_report_ids.size() == expected_num_received);
    auto num_received = observation_store_->messages_received.size();
    EXPECT_EQ(num_received, observation_store_->metadata_received.size());
    if (num_received != observation_store_->metadata_received.size()) {
      return false;
    }
    EXPECT_EQ(num_received, expected_num_received);
    if (num_received != expected_num_received) {
      return false;
    }
    num_received = update_recipient_->invocation_count;
    EXPECT_EQ(num_received, expected_num_received);
    if (num_received != expected_num_received) {
      return false;
    }
    MessageDecrypter message_decrypter("");
    for (auto i = 0u; i < expected_num_received; i++) {
      bool isNull = (observation_store_->metadata_received[i].get() == nullptr);
      EXPECT_FALSE(isNull);
      if (isNull) {
        return false;
      }
      EXPECT_EQ(observation_store_->metadata_received[i]->report_id(),
                expected_report_ids[i])
          << "i=" << i;
      isNull = (observation_store_->messages_received[i].get() == nullptr);
      EXPECT_FALSE(isNull);
      if (isNull) {
        return false;
      }
      bool successfullyDeserialized = message_decrypter.DecryptMessage(
          *(observation_store_->messages_received[i]), &(observations->at(i)));
      EXPECT_TRUE(successfullyDeserialized);
      if (!successfullyDeserialized) {
        return false;
      }
      bool has_random_id = !(observations->at(i).random_id().empty());
      EXPECT_TRUE(has_random_id);
      if (!successfullyDeserialized) {
        return false;
      }
    }

    return true;
  }

  // Populates |observation| with the contents of the FakeObservationStore,
  // which is expected to contain a single Observation with a report_id
  // of |expected_report_id|. Returns true iff all checks pass.
  bool FetchSingleImmediateObservation(Observation2* observation,
                                       uint32_t expected_report_id) {
    std::vector<Observation2> observations(1);
    std::vector<uint32_t> expected_report_ids;
    expected_report_ids.push_back(expected_report_id);
    if (!FetchImmediateObservations(&observations, expected_report_ids)) {
      return false;
    }
    *observation = observations[0];
    return true;
  }

  // Checks that the contents of the FakeObservationStore is a sequence of
  // IntegerEventObservations specified by the various parameters. Returns
  // true if all checks pass.
  bool CheckNumericEventObservations(
      const std::vector<uint32_t>& expected_report_ids,
      uint32_t expected_event_code, const std::string expected_component_name,
      int64_t expected_int_value) {
    size_t expected_num_observations = expected_report_ids.size();
    std::vector<Observation2> observations(expected_num_observations);
    if (!FetchImmediateObservations(&observations, expected_report_ids)) {
      return false;
    }
    for (auto i = 0u; i < expected_num_observations; i++) {
      const auto& numeric_event = observations[i].numeric_event();
      EXPECT_EQ(expected_event_code, numeric_event.event_code());
      if (expected_event_code != numeric_event.event_code()) {
        return false;
      }
      if (expected_component_name.empty()) {
        EXPECT_TRUE(numeric_event.component_name_hash().empty());
        if (!numeric_event.component_name_hash().empty()) {
          return false;
        }
      } else {
        EXPECT_EQ(numeric_event.component_name_hash().size(), 32u);
        if (numeric_event.component_name_hash().size() != 32u) {
          return false;
        }
      }
      EXPECT_EQ(expected_int_value, numeric_event.value());
      if (expected_int_value != numeric_event.value()) {
        return false;
      }
    }
    return true;
  }

  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<Logger> logger_;
  std::unique_ptr<ObservationWriter> observation_writer_;
  std::unique_ptr<FakeObservationStore> observation_store_;
  std::unique_ptr<TestUpdateRecipient> update_recipient_;
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;
  std::unique_ptr<ProjectContext> project_context_;
  std::unique_ptr<SystemDataInterface> system_data_;
};

// Tests the method LogEvent().
TEST_F(LoggerTest, LogEvent) {
  ASSERT_EQ(kOK, logger_->LogEvent(kErrorOccurredMetricId, 42));
  Observation2 observation;
  uint32_t expected_report_id = 123;
  ASSERT_TRUE(
      FetchSingleImmediateObservation(&observation, expected_report_id));
  ASSERT_TRUE(observation.has_basic_rappor());
  EXPECT_FALSE(observation.basic_rappor().data().empty());
}

// Tests the method LogEventCount().
TEST_F(LoggerTest, LogEventcount) {
  std::vector<uint32_t> expected_report_ids = {111};
  ASSERT_EQ(kOK, logger_->LogEventCount(kReadCacheHitsMetricId, 43,
                                        "component2", 1, 303));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 43u,
                                            "component2", 303));
}

// Tests the method LogElapsedTime().
TEST_F(LoggerTest, LogElapsedTime) {
  std::vector<uint32_t> expected_report_ids = {121, 221, 321};
  ASSERT_EQ(kOK, logger_->LogElapsedTime(kModuleLoadTimeMetricId, 44,
                                         "component4", 4004));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 44u,
                                            "component4", 4004));
}

// Tests the method LogFrameRate().
TEST_F(LoggerTest, LogFrameRate) {
  std::vector<uint32_t> expected_report_ids = {131, 231, 331};
  ASSERT_EQ(kOK, logger_->LogFrameRate(kLoginModuleFrameRateMetricId, 45,
                                       "component5", 5.123));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 45u,
                                            "component5", 5123));
}

// Tests the method LogMemoryUsage().
TEST_F(LoggerTest, LogMemoryUsage) {
  std::vector<uint32_t> expected_report_ids = {141, 241};
  ASSERT_EQ(kOK, logger_->LogMemoryUsage(kLedgerMemoryUsageMetricId, 46,
                                         "component6", 606));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 46u,
                                            "component6", 606));
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
  ASSERT_TRUE(
      FetchSingleImmediateObservation(&observation, expected_report_id));
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
  ASSERT_TRUE(FetchImmediateObservations(&observations, expected_report_ids));

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
  ASSERT_TRUE(
      FetchSingleImmediateObservation(&observation, expected_report_id));
  ASSERT_TRUE(observation.has_custom());
  const CustomObservation& custom_observation = observation.custom();
  for (auto i = 0u; i < values.size(); i++) {
    auto obs_dimension = custom_observation.values().at(dimension_names[i]);
    EXPECT_TRUE(MessageDifferencer::Equals(obs_dimension, values[i]));
  }
}

}  // namespace logger
}  // namespace cobalt

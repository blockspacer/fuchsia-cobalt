// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/event_aggregator.h"

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "./event.pb.h"
#include "./gtest.h"
#include "logger/logger_test_utils.h"
#include "util/proto_util.h"

using ::google::protobuf::util::MessageDifferencer;

namespace cobalt {

using encoder::ClientSecret;
using encoder::ObservationStoreUpdateRecipient;
using encoder::ObservationStoreWriterInterface;
using encoder::SystemDataInterface;
using util::EncryptedMessageMaker;
using util::SerializeToBase64;

namespace logger {

using testing::ExpectedActivity;
using testing::ExpectedAggregationParams;
using testing::FakeObservationStore;
using testing::FetchAggregatedObservations;
using testing::MakeAggregationConfig;
using testing::MakeAggregationKey;
using testing::MakeNullExpectedActivity;
using testing::PopulateMetricDefinitions;
using testing::TestUpdateRecipient;

namespace {

static const uint32_t kCustomerId = 1;
static const uint32_t kProjectId = 1;
static const char kCustomerName[] = "Fuchsia";
static const char kProjectName[] = "Cobalt";
static const uint32_t kStartDayIndex = 100;

// Pairs (metric ID, report ID) for the locally aggregated reports defined in
// |kMetricDefinitions| and |kNoiseFreeUniqueActivesMetricDefinitions|.
const MetricReportId kDeviceBootsMetricReportId = MetricReportId(10, 101);
const MetricReportId kFeaturesActiveMetricReportId = MetricReportId(20, 201);
const MetricReportId kErrorsOccurredMetricReportId = MetricReportId(30, 302);
const MetricReportId kEventsOccurredMetricReportId = MetricReportId(40, 402);

// A set of metric definitions of type EVENT_OCCURRED, each of which has a
// UNIQUE_N_DAY_ACTIVES report.
static const char kUniqueActivesMetricDefinitions[] = R"(
metric {
  metric_name: "DeviceBoots"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 10
  max_event_code: 1
  reports: {
    report_name: "DeviceBoots_UniqueDevices"
    id: 101
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: MEDIUM
    window_size: 1
  }
}

metric {
  metric_name: "FeaturesActive"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 20
  max_event_code: 4
  reports: {
    report_name: "FeaturesActive_UniqueDevices"
    id: 201
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: LARGE
    window_size: 7
    window_size: 30
  }
}

metric {
  metric_name: "ErrorsOccurred"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 30
  max_event_code: 2
  reports: {
    report_name: "ErrorsOccurred_SimpleCount"
    id: 301
    report_type: SIMPLE_OCCURRENCE_COUNT
    local_privacy_noise_level: NONE
  }
  reports: {
    report_name: "ErrorsOccurred_UniqueDevices"
    id: 302
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: LARGE
    window_size: 1
    window_size: 7
    window_size: 30
  }
}

)";

// Properties of the locally aggregated reports in
// |kUniqueActivesMetricDefinitions|.
static const ExpectedAggregationParams kUniqueActivesExpectedParams = {
    /* The total number of locally aggregated Observations which should be
       generated for each day index. */
    21,
    /* The MetricReportIds of the locally aggregated reports in this
       configuration. */
    {kDeviceBootsMetricReportId, kFeaturesActiveMetricReportId,
     kErrorsOccurredMetricReportId},
    /* The number of Observations which should be generated for each day
       index, broken down by MetricReportId. */
    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 10},
     {kErrorsOccurredMetricReportId, 9}},
    /* The number of event codes for each MetricReportId. */
    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 5},
     {kErrorsOccurredMetricReportId, 3}},
    /* The set of window sizes for each MetricReportId. */
    {{kDeviceBootsMetricReportId, {1}},
     {kFeaturesActiveMetricReportId, {7, 30}},
     {kErrorsOccurredMetricReportId, {1, 7, 30}}}};

// A set of MetricDefinitions of type EVENT_OCCURRED, each of which has a
// UNIQUE_N_DAY_ACTIVES report with local_privacy_noise_level set to NONE.
static const char kNoiseFreeUniqueActivesMetricDefinitions[] = R"(
metric {
  metric_name: "DeviceBoots"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 10
  max_event_code: 1
  reports: {
    report_name: "DeviceBoots_UniqueDevices"
    id: 101
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
  id: 20
  max_event_code: 4
  reports: {
    report_name: "FeaturesActive_UniqueDevices"
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
  id: 40
  max_event_code: 4
  reports: {
    report_name: "EventsOccurred_SimpleCount"
    id: 401
    report_type: SIMPLE_OCCURRENCE_COUNT
    local_privacy_noise_level: NONE
  }
  reports: {
    report_name: "EventsOccurred_UniqueDevices"
    id: 402
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: NONE
    window_size: 1
    window_size: 7
  }
}
)";

// Properties of the locally aggregated reports in
// |kNoiseFreeUniqueActivesMetricDefinitions|.
static const ExpectedAggregationParams kNoiseFreeUniqueActivesExpectedParams = {
    /* The total number of locally aggregated Observations which should be
       generated for each day index. */
    27,
    /* The MetricReportIds of the locally aggregated reports in this
configuration. */
    {kDeviceBootsMetricReportId, kFeaturesActiveMetricReportId,
     kEventsOccurredMetricReportId},
    /* The number of Observations which should be generated for each day
       index, broken down by MetricReportId. */
    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 15},
     {kEventsOccurredMetricReportId, 10}},
    /* The number of event codes for each MetricReportId. */
    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 5},
     {kEventsOccurredMetricReportId, 5}},
    /* The set of window sizes for each MetricReportId. */
    {{kDeviceBootsMetricReportId, {1}},
     {kFeaturesActiveMetricReportId, {1, 7, 30}},
     {kEventsOccurredMetricReportId, {1, 7}}}};

// A map keyed by base64-encoded, serialized ReportAggregationKeys. The value at
// a key is a map of event codes to sets of day indices. Used in tests as
// a record, external to the LocalAggregateStore, of the activity logged for
// UNIQUE_N_DAY_ACTIVES reports.
typedef std::map<std::string, std::map<uint32_t, std::set<uint32_t>>>
    LoggedActivity;

// Given a string representing a MetricDefinitions proto message, creates a
// ProjectContext from that MetricDefinitions and returns a unique pointer.
std::unique_ptr<ProjectContext> MakeProjectContext(const char metric_string[]) {
  auto metric_definitions = std::make_unique<MetricDefinitions>();
  if (!PopulateMetricDefinitions(metric_string, metric_definitions.get())) {
    return nullptr;
  }
  auto project_context = std::make_unique<ProjectContext>(
      kCustomerId, kProjectId, kCustomerName, kProjectName,
      std::move(metric_definitions));
  return project_context;
}

}  // namespace

// EventAggregatorTest creates an EventAggregator which sends its Observations
// to a FakeObservationStore. The EventAggregator is not pre-populated with
// aggregation configurations.
class EventAggregatorTest : public ::testing::Test {
 protected:
  void SetUp() {
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
    event_aggregator_.reset(new EventAggregator(encoder_.get(),
                                                observation_writer_.get(),
                                                local_aggregate_store_.get()));
  }

  // Clears the FakeObservationStore and resets the TestUpdateRecipient's count
  // of received observations.
  void ResetObservationStore() {
    observation_store_->messages_received.clear();
    observation_store_->metadata_received.clear();
    update_recipient_->invocation_count = 0;
  }

  // Given a ProjectContext |project_context| and the MetricReportId of a
  // UNIQUE_N_DAY_ACTIVES report in |project_context|, as well as a day index
  // and an event code, logs a UniqueActivesEvent to the EventAggregator for
  // that report, day index, and event code. If a non-null LoggedActivity map is
  // provided, updates the map with information about the logged Event.
  Status LogUniqueActivesEvent(const ProjectContext& project_context,
                               const MetricReportId& metric_report_id,
                               uint32_t day_index, uint32_t event_code,
                               LoggedActivity* logged_activity = nullptr) {
    EventRecord event_record;
    event_record.metric = project_context.GetMetric(metric_report_id.first);
    event_record.event->set_day_index(day_index);
    event_record.event->mutable_occurrence_event()->set_event_code(event_code);
    auto status = event_aggregator_->LogUniqueActivesEvent(
        metric_report_id.second, &event_record);
    if (logged_activity == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(
            MakeAggregationKey(project_context, metric_report_id), &key)) {
      return kInvalidArguments;
    }
    if (logged_activity->count(key) == 0) {
      logged_activity->insert(
          std::make_pair(key, std::map<uint32_t, std::set<uint32_t>>({})));
    }
    if (logged_activity->at(key).count(event_code) == 0) {
      logged_activity->at(key).insert(
          std::make_pair(event_code, std::set<uint32_t>({})));
    }
    (logged_activity->at(key).at(event_code)).insert(day_index);
    return status;
  }

  // Given a LoggedActivity map describing the events that have been logged to
  // the EventAggregator, checks whether the contents of the LocalAggregateStore
  // are as expected, accounting for any garbage collection.
  //
  // logged_activity: a LoggedActivity representing event occurrences
  // since the LocalAggregateStore was created. All day indices should be
  // greater than or equal to |kStartDayIndex| and less than or equal to
  // |current_day_index|.
  //
  // current_day_index: The day index of the current day in the test's frame
  // of reference.
  bool CheckAggregateStore(const LoggedActivity& logged_activity,
                           uint32_t current_day_index) {
    // Check that the LocalAggregateStore contains no more aggregates than
    // |logged_activity| and |day_last_garbage_collected_| should imply.
    for (const auto& report_pair : local_aggregate_store_->aggregates()) {
      const auto& report_key = report_pair.first;
      const auto& aggregates = report_pair.second;
      // Check whether this ReportAggregationKey is in |logged_activity|. If
      // not, expect that its by_event_code map is empty.
      if (logged_activity.count(report_key) == 0u) {
        EXPECT_TRUE(aggregates.by_event_code().empty());
        if (!aggregates.by_event_code().empty()) {
          return false;
        }
        break;
      }
      auto expected_events = logged_activity.at(report_key);
      for (const auto& event_pair : aggregates.by_event_code()) {
        // Check that this event code is in |logged_activity| under this
        // ReportAggregationKey.
        auto event_code = event_pair.first;
        EXPECT_GT(expected_events.count(event_code), 0u);
        if (expected_events.count(event_code) == 0u) {
          return false;
        }
        const auto& expected_days = expected_events[event_code];
        for (const auto& day_pair : event_pair.second.by_day_index()) {
          // Check that this day index is in |logged_activity| under this
          // ReportAggregationKey and event code.
          const auto& day_index = day_pair.first;
          EXPECT_GT(expected_days.count(day_index), 0u);
          if (expected_days.count(day_index) == 0u) {
            return false;
          }
          // Check that the day index is no earlier than is implied by the
          // dates of store creation and garbage collection.
          EXPECT_GE(day_index,
                    EarliestAllowedDayIndex(aggregates.aggregation_config()));
          if (day_index <
              EarliestAllowedDayIndex(aggregates.aggregation_config())) {
            return false;
          }
        }
      }
    }

    // Check that the LocalAggregateStore contains aggregates for all events in
    // |logged_activity|, as long as they are recent enough to have survived any
    // garbage collection.
    for (const auto& logged_pair : logged_activity) {
      const auto& logged_key = logged_pair.first;
      const auto& logged_event_map = logged_pair.second;
      // Check that this ReportAggregationKey is in the LocalAggregateStore.
      EXPECT_GT(local_aggregate_store_->aggregates().count(logged_key), 0u);
      if (local_aggregate_store_->aggregates().count(logged_key) == 0u) {
        return false;
      }
      for (const auto& logged_event_pair : logged_event_map) {
        const auto& logged_event_code = logged_event_pair.first;
        const auto& logged_days = logged_event_pair.second;
        auto earliest_allowed =
            EarliestAllowedDayIndex(local_aggregate_store_->aggregates()
                                        .at(logged_key)
                                        .aggregation_config());
        // Check whether this event code is in the LocalAggregateStore
        // under this ReportAggregationKey. If not, check that all day indices
        // for this event code are smaller than the day index of the earliest
        // allowed aggregate.
        if (local_aggregate_store_->aggregates()
                .at(logged_key)
                .by_event_code()
                .count(logged_event_code) == 0u) {
          for (auto day_index : logged_days) {
            EXPECT_LT(day_index, earliest_allowed);
            if (day_index >= earliest_allowed) {
              return false;
            }
          }
          break;
        }
        // Check that all of the day indices in |logged_activity| under this
        // ReportAggregationKey and event code are in the
        // LocalAggregateStore, as long as they are recent enough to have
        // survived any garbage collection. Check that each aggregate has its
        // activity field set to true.
        for (const auto& logged_day_index : logged_days) {
          if (logged_day_index >= earliest_allowed) {
            EXPECT_GT(local_aggregate_store_->aggregates()
                          .at(logged_key)
                          .by_event_code()
                          .at(logged_event_code)
                          .by_day_index()
                          .count(logged_day_index),
                      0u);
            if (local_aggregate_store_->aggregates()
                    .at(logged_key)
                    .by_event_code()
                    .at(logged_event_code)
                    .by_day_index()
                    .count(logged_day_index) == 0u) {
              return false;
            }
            EXPECT_TRUE(local_aggregate_store_->aggregates()
                            .at(logged_key)
                            .by_event_code()
                            .at(logged_event_code)
                            .by_day_index()
                            .at(logged_day_index)
                            .activity_daily_aggregate()
                            .activity_indicator());
            if (!local_aggregate_store_->aggregates()
                     .at(logged_key)
                     .by_event_code()
                     .at(logged_event_code)
                     .by_day_index()
                     .at(logged_day_index)
                     .activity_daily_aggregate()
                     .activity_indicator()) {
              return false;
            }
          }
        }
      }
    }
    return true;
  }

  // Given the AggregationConfig of a locally aggregated report, returns the
  // earliest (smallest) day index for which an aggregate may exist in the
  // LocalAggregateStore for that report, accounting for garbage
  // collection.
  uint32_t EarliestAllowedDayIndex(const AggregationConfig& config) {
    // If the LocalAggregateStore has never been garbage-collected, then the
    // earliest allowed day index is just the day when the store was created;
    // i.e., |kStartDayIndex|.
    if (day_last_garbage_collected_ == 0u) {
      return kStartDayIndex;
    } else {
      // Otherwise, it is the later of:
      // (a) The day index on which the store was created.
      // (b) The day index for which the store was last garbage-collected,
      // minus the largest window size in the report associated to |config|,
      // plus 1.
      uint32_t max_window_size = 1;
      for (uint32_t window_size : config.report().window_size()) {
        if (window_size > max_window_size) {
          max_window_size = window_size;
        }
      }
      if (day_last_garbage_collected_ < (max_window_size + 1)) {
        return kStartDayIndex;
      }
      return (kStartDayIndex <
              (day_last_garbage_collected_ - max_window_size + 1))
                 ? (day_last_garbage_collected_ - max_window_size + 1)
                 : kStartDayIndex;
    }
  }

  std::unique_ptr<LocalAggregateStore> local_aggregate_store_;
  std::unique_ptr<EventAggregator> event_aggregator_;

  std::unique_ptr<TestUpdateRecipient> update_recipient_;
  std::unique_ptr<FakeObservationStore> observation_store_;

  // The day index on which the LocalAggregateStore was last garbage-collected.
  // A value of 0 indicates that the store has never been garbage-collected.
  uint32_t day_last_garbage_collected_ = 0u;

  std::unique_ptr<ObservationWriter> observation_writer_;
  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;

 private:
  std::unique_ptr<SystemDataInterface> system_data_;
};

// Creates an EventAggregator and provides it with MetricDefinitions from a
// serialized representation |metric_string|.
class EventAggregatorTestWithProjectContext : public EventAggregatorTest {
 protected:
  explicit EventAggregatorTestWithProjectContext(const char metric_string[]) {
    project_context_ = MakeProjectContext(metric_string);
  }

  void SetUp() {
    EventAggregatorTest::SetUp();
    event_aggregator_->UpdateAggregationConfigs(*project_context_);
  }

  // Logs a UniqueActivesEvent for the MetricReportId of a locally aggregated
  // report in |metric_string|. Overrides the method
  // EventAggregatorTest::LogUniqueActivesEvent.
  Status LogUniqueActivesEvent(const MetricReportId& metric_report_id,
                               uint32_t day_index, uint32_t event_code,
                               LoggedActivity* logged_activity = nullptr) {
    return EventAggregatorTest::LogUniqueActivesEvent(
        *project_context_, metric_report_id, day_index, event_code,
        logged_activity);
  }

 private:
  // A ProjectContext wrapping the MetricDefinitions passed to the constructor
  // in |metric_string|.
  std::unique_ptr<ProjectContext> project_context_;
};

// Creates an EventAggregator and provides it with
// |kUniqueActivesMetricDefinitions|.
class UniqueActivesEventAggregatorTest
    : public EventAggregatorTestWithProjectContext {
 protected:
  UniqueActivesEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(kUniqueActivesMetricDefinitions) {
  }

  void SetUp() { EventAggregatorTestWithProjectContext::SetUp(); }
};

// Creates an EventAggregator as in EventAggregatorTest and provides it with
// |kNoiseFreeUniqueActivesMetricDefinitions|.
class NoiseFreeUniqueActivesEventAggregatorTest
    : public EventAggregatorTestWithProjectContext {
 protected:
  NoiseFreeUniqueActivesEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(
            kNoiseFreeUniqueActivesMetricDefinitions) {}

  void SetUp() { EventAggregatorTestWithProjectContext::SetUp(); }
};

// Tests that an empty LocalAggregateStore is updated with ReportAggregationKeys
// and AggregationConfigs as expected when
// EventAggregator::UpdateAggregationConfigs is called.
TEST_F(EventAggregatorTest, UpdateAggregationConfigs) {
  // Check that the LocalAggregateStore is empty.
  EXPECT_EQ(0u, local_aggregate_store_->aggregates().size());
  // Provide |kUniqueActivesMetricDefinitions| to the EventAggregator.
  auto unique_actives_project_context =
      MakeProjectContext(kUniqueActivesMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(
                     *unique_actives_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in
  // |kUniqueActivesMetricDefinitions|.
  EXPECT_EQ(kUniqueActivesExpectedParams.metric_report_ids.size(),
            local_aggregate_store_->aggregates().size());
  // Check that the LocalAggregateStore contains the expected
  // ReportAggregationKey and AggregationConfig for each locally aggregated
  // report in |kUniqueActivesMetricDefinitions|,
  for (const auto& metric_report_id :
       kUniqueActivesExpectedParams.metric_report_ids) {
    std::string key;
    SerializeToBase64(
        MakeAggregationKey(*unique_actives_project_context, metric_report_id),
        &key);
    auto config = MakeAggregationConfig(*unique_actives_project_context,
                                        metric_report_id);
    EXPECT_NE(local_aggregate_store_->aggregates().end(),
              local_aggregate_store_->aggregates().find(key));
    EXPECT_TRUE(MessageDifferencer::Equals(
        config,
        local_aggregate_store_->aggregates().at(key).aggregation_config()));
  }
}

// Tests two assumptions about the behavior of
// EventAggregator::UpdateAggregationConfigs when two projects with the same
// customer ID and project ID provide configurations to the EventAggregator.
// These assumptions are:
// (1) If the second project provides a report with a
// ReportAggregationKey which was not provided by the first project, then the
// EventAggregator accepts the new report.
// (2) If a report provided by the second project has a ReportAggregationKey
// which was already provided by the first project, then the EventAggregator
// rejects the new report, even if its ReportDefinition differs from that of
// existing report with the same ReportAggregationKey.
TEST_F(EventAggregatorTest, UpdateAggregationConfigsWithSameKey) {
  // Provide the EventAggregator with |kUniqueActivesMetricDefinitions|.
  auto unique_actives_project_context =
      MakeProjectContext(kUniqueActivesMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(
                     *unique_actives_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in
  // |kUniqueActivesMetricDefinitions|.
  EXPECT_EQ(3u, local_aggregate_store_->aggregates().size());

  // Provide the EventAggregator with
  // |kNoiseFreeUniqueActivesMetricDefinitions|.
  auto noise_free_unique_actives_project_context =
      MakeProjectContext(kNoiseFreeUniqueActivesMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(
                     *noise_free_unique_actives_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of distinct MetricReportIds of locally aggregated
  // reports in |kUniqueActivesMetricDefinitions| and
  // |kNoiseFreeUniqueActivesMetricDefinitions|.
  EXPECT_EQ(4u, local_aggregate_store_->aggregates().size());
  // The MetricReportId |kFeaturesActiveMetricReportId| appears in both
  // |kUniqueActivesMetricDefinitions| and
  // |kNoiseFreeUniqueActivesMetricDefinitions|. The associated
  // ReportAggregationKeys are identical, but the AggregationConfigs are
  // different.
  //
  // Check that the AggregationConfig stored in the LocalAggregateStore under
  // the key associated to |kFeaturesActiveMetricReportId| is the first
  // AggregationConfig that was provided for that key; i.e., is derived from
  // |kUniqueActivesMetricDefinitions|.
  std::string key;
  EXPECT_TRUE(
      SerializeToBase64(MakeAggregationKey(*unique_actives_project_context,
                                           kFeaturesActiveMetricReportId),
                        &key));
  auto unique_actives_config = MakeAggregationConfig(
      *unique_actives_project_context, kFeaturesActiveMetricReportId);
  EXPECT_NE(local_aggregate_store_->aggregates().end(),
            local_aggregate_store_->aggregates().find(key));
  EXPECT_TRUE(MessageDifferencer::Equals(
      unique_actives_config,
      local_aggregate_store_->aggregates().at(key).aggregation_config()));
  auto noise_free_config =
      MakeAggregationConfig(*noise_free_unique_actives_project_context,
                            kFeaturesActiveMetricReportId);
  EXPECT_FALSE(MessageDifferencer::Equals(
      noise_free_config,
      local_aggregate_store_->aggregates().at(key).aggregation_config()));
}

// Tests that EventAggregator::LogUniqueActivesEvent returns
// |kInvalidArguments| when passed a report ID which is not associated to a
// key of the LocalAggregateStore, or when passed an EventRecord containing an
// Event proto message which is not of type OccurrenceEvent.
TEST_F(EventAggregatorTest, LogBadEvents) {
  // Provide the EventAggregator with |kUniqueActivesMetricDefinitions|.
  auto unique_actives_project_context =
      MakeProjectContext(kUniqueActivesMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(
                     *unique_actives_project_context));
  // Attempt to log a UniqueActivesEvent for |kEventsOccurredMetricReportId|,
  // which is not in |kUniqueActivesMetricDefinitions|. Check that the result
  // is |kInvalidArguments|.
  auto noise_free_project_context =
      MakeProjectContext(kNoiseFreeUniqueActivesMetricDefinitions);
  EventRecord bad_event_record;
  bad_event_record.metric = noise_free_project_context->GetMetric(
      kEventsOccurredMetricReportId.first);
  bad_event_record.event->set_day_index(kStartDayIndex);
  bad_event_record.event->mutable_occurrence_event()->set_event_code(0u);
  EXPECT_EQ(kInvalidArguments,
            event_aggregator_->LogUniqueActivesEvent(
                kEventsOccurredMetricReportId.second, &bad_event_record));
  // Attempt to call LogUniqueActivesEvent() with a valid metric and report
  // ID, but with an EventRecord wrapping an Event which is not an
  // OccurrenceEvent. Check that the result is |kInvalidArguments|.
  bad_event_record.metric = unique_actives_project_context->GetMetric(
      kFeaturesActiveMetricReportId.first);
  bad_event_record.event->mutable_count_event();
  EXPECT_EQ(kInvalidArguments,
            event_aggregator_->LogUniqueActivesEvent(
                kFeaturesActiveMetricReportId.second, &bad_event_record));
}

// Tests that the LocalAggregateStore is updated as expected when
// EventAggregator::LogUniqueActivesEvent() is called with valid arguments;
// i.e., with a report ID associated to an existing key of the
// LocalAggregateStore, and with an EventRecord which wraps an OccurrenceEvent.
//
// Logs some valid events each day for 35 days, checking the contents of the
// LocalAggregateStore each day.
TEST_F(UniqueActivesEventAggregatorTest, LogUniqueActivesEvents) {
  LoggedActivity logged_activity;
  uint32_t num_days = 35;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    // Log an event for the FeaturesActive_UniqueDevices report of
    // |kUniqueActivesMetricDefinitions| with event code 0. Check the contents
    // of the LocalAggregateStore.
    EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                         kStartDayIndex + offset, 0u,
                                         &logged_activity));
    EXPECT_TRUE(CheckAggregateStore(logged_activity, kStartDayIndex));
    // Log another event for the same report, event code, and day index.
    // Check the contents of the LocalAggregateStore.
    EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                         kStartDayIndex + offset, 0u,
                                         &logged_activity));
    EXPECT_TRUE(CheckAggregateStore(logged_activity, kStartDayIndex));
    // Log several more events for various valid reports and event codes. Check
    // the contents of the LocalAggregateStore.
    EXPECT_EQ(kOK, LogUniqueActivesEvent(kDeviceBootsMetricReportId,
                                         kStartDayIndex + offset, 0u,
                                         &logged_activity));
    EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                         kStartDayIndex + offset, 4u,
                                         &logged_activity));
    EXPECT_EQ(kOK, LogUniqueActivesEvent(kErrorsOccurredMetricReportId,
                                         kStartDayIndex + offset, 1u,
                                         &logged_activity));
    EXPECT_TRUE(CheckAggregateStore(logged_activity, kStartDayIndex + offset));
  }
}

// Tests the method EventAggregator::GarbageCollect().
//
// For each value of N in the range [0, 34], logs some UniqueActivesEvents each
// day for N consecutive days and then garbage-collect the LocalAggregateStore.
// After garbage collection, verifies the contents of the LocalAggregateStore.
TEST_F(UniqueActivesEventAggregatorTest, GarbageCollect) {
  uint32_t max_days_before_gc = 35;
  for (uint32_t days_before_gc = 0; days_before_gc < max_days_before_gc;
       days_before_gc++) {
    SetUp();
    day_last_garbage_collected_ = 0u;
    LoggedActivity logged_activity;
    uint32_t end_day_index = kStartDayIndex + days_before_gc;
    for (uint32_t day_index = kStartDayIndex; day_index < end_day_index;
         day_index++) {
      for (const auto& metric_report_id :
           kUniqueActivesExpectedParams.metric_report_ids) {
        // Log 2 events with event code 0.
        EXPECT_EQ(kOK, LogUniqueActivesEvent(metric_report_id, day_index, 0u,
                                             &logged_activity));
        EXPECT_EQ(kOK, LogUniqueActivesEvent(metric_report_id, day_index, 0u,
                                             &logged_activity));
        // Log 1 event with event code 1.
        EXPECT_EQ(kOK, LogUniqueActivesEvent(metric_report_id, day_index, 1u,
                                             &logged_activity));
      }
    }
    EXPECT_EQ(kOK, event_aggregator_->GarbageCollect(end_day_index));
    day_last_garbage_collected_ = end_day_index;
    EXPECT_TRUE(CheckAggregateStore(logged_activity, end_day_index));
    TearDown();
  }
}

// Tests that EventAggregator::GenerateObservations() returns a positive status
// and that the expected number of Observations is generated when no Events have
// been logged to the EventAggregator.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservationsNoEvents) {
  EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(kStartDayIndex));

  std::vector<Observation2> observations(0);
  EXPECT_TRUE(FetchAggregatedObservations(
      &observations, kUniqueActivesExpectedParams, observation_store_.get(),
      update_recipient_.get()));
}

// Tests that EventAggregator::GenerateObservations() returns a positive status
// and that the expected number of Observations is generated after some
// UniqueActivesEvents have been logged, without any garbage collection.
//
// For 35 days, logs 2 events each day for the ErrorsOccurred_UniqueDevices
// report and 2 events for the FeaturesActive_UniqueDevices report, all
// with event type index 0.
//
// Each day following the first day, calls GenerateObservations() with the day
// index of the previous day. Checks that a positive status is returned and
// that the FakeObservationStore has received the expected number of new
// observations for each locally aggregated report ID in
// |kUniqueActivesMetricDefinitions|.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservations) {
  int num_days = 35;
  std::vector<Observation2> observations(0);
  for (uint32_t day_index = kStartDayIndex;
       day_index < kStartDayIndex + num_days; day_index++) {
    if (day_index > kStartDayIndex) {
      observations.clear();
      ResetObservationStore();
      EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(day_index - 1));
      EXPECT_TRUE(FetchAggregatedObservations(
          &observations, kUniqueActivesExpectedParams, observation_store_.get(),
          update_recipient_.get()));
    }
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kErrorsOccurredMetricReportId,
                                           day_index, 0u));
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                           day_index, 0u));
    }
  }
  observations.clear();
  ResetObservationStore();
  EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(kStartDayIndex +
                                                         num_days - 1));
  EXPECT_TRUE(FetchAggregatedObservations(
      &observations, kUniqueActivesExpectedParams, observation_store_.get(),
      update_recipient_.get()));
}

// Tests that GenerateObservations() returns a positive status and that the
// expected number of Observations is generated after some UniqueActivesEvents
// have been logged, in the presence of daily garbage collection.
//
// For 35 days, logs 2 events each day for the ErrorsOccurred_UniqueDevices
// report and 2 events for the FeaturesActive_UniqueDevices report, all
// with event type index 0.
//
// Each day following the first day, calls GenerateObservations() and then
// GarbageCollect() with the day index of the current day. Checks that positive
// statuses are returned and that the FakeObservationStore has received the
// expected number of new observations for each locally aggregated report ID in
// |kUniqueActivesMetricDefinitions|.
TEST_F(UniqueActivesEventAggregatorTest,
       GenerateObservationWithGarbageCollection) {
  int num_days = 35;
  std::vector<Observation2> observations(0);
  for (uint32_t day_index = kStartDayIndex;
       day_index < kStartDayIndex + num_days; day_index++) {
    if (day_index > kStartDayIndex) {
      observations.clear();
      ResetObservationStore();
      EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(day_index - 1));
      EXPECT_TRUE(FetchAggregatedObservations(
          &observations, kUniqueActivesExpectedParams, observation_store_.get(),
          update_recipient_.get()));
      EXPECT_EQ(kOK, event_aggregator_->GarbageCollect(day_index));
    }
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kErrorsOccurredMetricReportId,
                                           day_index, 0u));
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                           day_index, 0u));
    }
  }
  observations.clear();
  ResetObservationStore();
  EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(kStartDayIndex +
                                                         num_days - 1));
  EXPECT_TRUE(FetchAggregatedObservations(
      &observations, kUniqueActivesExpectedParams, observation_store_.get(),
      update_recipient_.get()));
  EXPECT_EQ(kOK, event_aggregator_->GarbageCollect(kStartDayIndex + num_days));
}

// Checks that UniqueActivesObservations with the expected values (i.e.,
// non-active for all window sizes and event types) are generated when no Events
// have been logged to the EventAggregator.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesNoEvents) {
  EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(kStartDayIndex));

  auto expected_activity =
      MakeNullExpectedActivity(kNoiseFreeUniqueActivesExpectedParams);

  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_activity, kNoiseFreeUniqueActivesExpectedParams,
      observation_store_.get(), update_recipient_.get()));
}

// Checks that UniqueActivesObservations with the expected values are generated
// when some events have been logged for a UNIQUE_N_DAY_ACTIVES report for over
// multiple days, without garbage collection.
//
// Logs events for the SomeEventsOccurred_UniqueDevices report (whose parent
// metric has max_event_type_index = 4) for 10 days, according to the following
// pattern:
//
// * Never log event type 0.
// * On the i-th day (0-indexed) of logging, log an event for event type k,
// 1 <= k < 5, if 3*k divides i.
//
// Each day following the first day, generates Observations for the previous
// day index and check them against the expected set of Observations. Also
// generates and check Observations for the last day of logging.
//
// The SomeEventsOccurred_UniqueDevices report has window sizes 1 and 7, and
// the expected pattern of those Observations' values on the i-th day is:
//
// (i, window size)            true for event types
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
// All Observations for all other locally aggregated reports should be
// observations of non-occurrence.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesSingleDay) {
  // Log several events.
  EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                       kStartDayIndex, 0u));
  EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                       kStartDayIndex, 0u));
  EXPECT_EQ(kOK, LogUniqueActivesEvent(kEventsOccurredMetricReportId,
                                       kStartDayIndex, 1u));
  // Generate locally aggregated Observations.
  EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(kStartDayIndex));

  // Form the expected activity map.
  auto expected_activity =
      MakeNullExpectedActivity(kNoiseFreeUniqueActivesExpectedParams);
  expected_activity[kFeaturesActiveMetricReportId] = {
      {1, {true, false, false, false, false}},
      {7, {true, false, false, false, false}},
      {30, {true, false, false, false, false}}};
  expected_activity[kEventsOccurredMetricReportId] = {
      {1, {false, true, false, false, false}},
      {7, {false, true, false, false, false}}};

  // Check the contents of the FakeObservationStore.
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_activity, kNoiseFreeUniqueActivesExpectedParams,
      observation_store_.get(), update_recipient_.get()));
}

TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesMultiDay) {
  // Generate expected activity maps for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedActivity> expected_activity(num_days);
  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_activity[offset] =
        MakeNullExpectedActivity(kNoiseFreeUniqueActivesExpectedParams);
  }
  expected_activity[0][kEventsOccurredMetricReportId] = {
      {1, {false, true, true, true, true}},
      {7, {false, true, true, true, true}}};
  expected_activity[1][kEventsOccurredMetricReportId] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_activity[2][kEventsOccurredMetricReportId] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_activity[3][kEventsOccurredMetricReportId] = {
      {1, {false, true, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_activity[4][kEventsOccurredMetricReportId] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_activity[5][kEventsOccurredMetricReportId] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_activity[6][kEventsOccurredMetricReportId] = {
      {1, {false, true, true, false, false}},
      {7, {false, true, true, true, true}}};
  expected_activity[7][kEventsOccurredMetricReportId] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_activity[8][kEventsOccurredMetricReportId] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_activity[9][kEventsOccurredMetricReportId] = {
      {1, {false, true, false, true, false}},
      {7, {false, true, true, true, false}}};

  for (uint32_t offset = 0; offset < num_days; offset++) {
    for (uint32_t event_code = 1;
         event_code < kNoiseFreeUniqueActivesExpectedParams.num_event_codes.at(
                          kEventsOccurredMetricReportId);
         event_code++) {
      if (offset % (3 * event_code) == 0) {
        EXPECT_EQ(kOK,
                  LogUniqueActivesEvent(kEventsOccurredMetricReportId,
                                        kStartDayIndex + offset, event_code));
      }
    }
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Generate locally aggregated Observations.
    EXPECT_EQ(kOK,
              event_aggregator_->GenerateObservations(kStartDayIndex + offset));
    // Check the generated Observations against the expectation.
    EXPECT_TRUE(CheckUniqueActivesObservations(
        expected_activity[offset], kNoiseFreeUniqueActivesExpectedParams,
        observation_store_.get(), update_recipient_.get()));
  }
}

// Checks that UniqueActivesObservations with the expected values are generated
// when some events have been logged for a UNIQUE_N_DAY_ACTIVES report for over
// multiple days, with daily garbage collection.
//
// Logs events for the SomeEventsOccurred_UniqueDevices report (whose parent
// metric has max_event_type_index = 4) for 10 days, according to the following
// pattern:
//
// * Never log event type 0.
// * On the i-th day (0-indexed) of logging, log an event for event type k,
// 1 <= k < 5, if 3*k divides i.
//
// Each day following the first day, generates Observations for the previous
// day index and check them against the expected set of Observations. Also
// generates and check Observations for the last day of logging.
//
// The SomeEventsOccurred_UniqueDevices report has window sizes 1 and 7, and
// the expected pattern of those Observations' values on the i-th day is:
//
// (i, window size)            true for event types
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
// All Observations for all other locally aggregated reports should be
// observations of non-occurrence.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesMultiDayWithGarbageCollection) {
  // Generate expected activity maps for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedActivity> expected_activity(num_days);
  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_activity[offset] =
        MakeNullExpectedActivity(kNoiseFreeUniqueActivesExpectedParams);
  }
  expected_activity[0][kEventsOccurredMetricReportId] = {
      {1, {false, true, true, true, true}},
      {7, {false, true, true, true, true}}};
  expected_activity[1][kEventsOccurredMetricReportId] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_activity[2][kEventsOccurredMetricReportId] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_activity[3][kEventsOccurredMetricReportId] = {
      {1, {false, true, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_activity[4][kEventsOccurredMetricReportId] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_activity[5][kEventsOccurredMetricReportId] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_activity[6][kEventsOccurredMetricReportId] = {
      {1, {false, true, true, false, false}},
      {7, {false, true, true, true, true}}};
  expected_activity[7][kEventsOccurredMetricReportId] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_activity[8][kEventsOccurredMetricReportId] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_activity[9][kEventsOccurredMetricReportId] = {
      {1, {false, true, false, true, false}},
      {7, {false, true, true, true, false}}};

  for (uint32_t offset = 0; offset < num_days; offset++) {
    for (uint32_t event_code = 1;
         event_code < kNoiseFreeUniqueActivesExpectedParams.num_event_codes.at(
                          kEventsOccurredMetricReportId);
         event_code++) {
      if (offset % (3 * event_code) == 0) {
        EXPECT_EQ(kOK,
                  LogUniqueActivesEvent(kEventsOccurredMetricReportId,
                                        kStartDayIndex + offset, event_code));
      }
    }
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Generate locally aggregated Observations.
    EXPECT_EQ(kOK,
              event_aggregator_->GenerateObservations(kStartDayIndex + offset));
    // Check the generated Observations against the expectation.
    EXPECT_TRUE(CheckUniqueActivesObservations(
        expected_activity[offset], kNoiseFreeUniqueActivesExpectedParams,
        observation_store_.get(), update_recipient_.get()));
    // Garbage collect for the next day index.
    EXPECT_EQ(kOK,
              event_aggregator_->GarbageCollect(kStartDayIndex + offset + 1));
  }
}

}  // namespace logger
}  // namespace cobalt

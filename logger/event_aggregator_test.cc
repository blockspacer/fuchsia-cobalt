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

using testing::CheckUniqueActivesObservations;
using testing::ExpectedAggregationParams;
using testing::ExpectedUniqueActivesObservations;
using testing::FakeObservationStore;
using testing::FetchAggregatedObservations;
using testing::MakeAggregationConfig;
using testing::MakeAggregationKey;
using testing::MakeNullExpectedUniqueActivesObservations;
using testing::MockConsistentProtoStore;
using testing::PopulateMetricDefinitions;
using testing::TestUpdateRecipient;

namespace {

static const uint32_t kCustomerId = 1;
static const uint32_t kProjectId = 1;
static const char kCustomerName[] = "Fuchsia";
static const char kProjectName[] = "Cobalt";
static const uint32_t kStartDayIndex = 100;
static const size_t kDefaultBackfillDays = 0;

// Filenames for constructors of ConsistentProtoStores
static const char kAggregateStoreFilename[] = "local_aggregate_store_backup";
static const char kObsHistoryFilename[] = "obs_history_backup";

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
    local_aggregate_proto_store_.reset(
        new MockConsistentProtoStore(kAggregateStoreFilename));
    obs_history_proto_store_.reset(
        new MockConsistentProtoStore(kObsHistoryFilename));
    event_aggregator_.reset(new EventAggregator(
        encoder_.get(), observation_writer_.get(),
        local_aggregate_proto_store_.get(), obs_history_proto_store_.get(),
        kDefaultBackfillDays));
  }

  // Destruct the EventAggregator before destructing any other objects owned by
  // the test class.
  void TearDown() { event_aggregator_.reset(); }

  // Returns a copy of the LocalAggregateStore.
  LocalAggregateStore GetLocalAggregateStore() {
    return event_aggregator_->local_aggregate_store_;
  }

  size_t GetBackfillDays() { return event_aggregator_->backfill_days_; }

  void SetBackfillDays(size_t num_days) {
    event_aggregator_->backfill_days_ = num_days;
  }

  Status BackUpLocalAggregateStore() {
    return event_aggregator_->BackUpLocalAggregateStore();
  }

  Status BackUpObservationHistory() {
    return event_aggregator_->BackUpObservationHistory();
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
    (*logged_activity)[key][event_code].insert(day_index);
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
    auto local_aggregate_store = GetLocalAggregateStore();
    // Check that the LocalAggregateStore contains no more aggregates than
    // |logged_activity| and |day_last_garbage_collected_| should imply.
    for (const auto& report_pair : local_aggregate_store.aggregates()) {
      const auto& report_key = report_pair.first;
      const auto& aggregates = report_pair.second;
      // Check whether this ReportAggregationKey is in |logged_activity|. If
      // not, expect that its by_event_code map is empty.
      auto report_activity = logged_activity.find(report_key);
      if (report_activity == logged_activity.end()) {
        EXPECT_TRUE(aggregates.by_event_code().empty());
        if (!aggregates.by_event_code().empty()) {
          return false;
        }
        break;
      }
      auto expected_events = report_activity->second;
      for (const auto& event_pair : aggregates.by_event_code()) {
        // Check that this event code is in |logged_activity| under this
        // ReportAggregationKey.
        auto event_code = event_pair.first;
        auto event_activity = expected_events.find(event_code);
        EXPECT_NE(event_activity, expected_events.end());
        if (event_activity == expected_events.end()) {
          return false;
        }
        const auto& expected_days = event_activity->second;
        for (const auto& day_pair : event_pair.second.by_day_index()) {
          // Check that this day index is in |logged_activity| under this
          // ReportAggregationKey and event code.
          const auto& day_index = day_pair.first;
          auto day_activity = expected_days.find(day_index);
          EXPECT_NE(day_activity, expected_days.end());
          if (day_activity == expected_days.end()) {
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
      auto report_aggregates =
          local_aggregate_store.aggregates().find(logged_key);
      EXPECT_NE(report_aggregates, local_aggregate_store.aggregates().end());
      if (report_aggregates == local_aggregate_store.aggregates().end()) {
        return false;
      }
      for (const auto& logged_event_pair : logged_event_map) {
        const auto& logged_event_code = logged_event_pair.first;
        const auto& logged_days = logged_event_pair.second;
        auto earliest_allowed = EarliestAllowedDayIndex(
            report_aggregates->second.aggregation_config());
        // Check whether this event code is in the LocalAggregateStore
        // under this ReportAggregationKey. If not, check that all day indices
        // for this event code are smaller than the day index of the earliest
        // allowed aggregate.
        auto event_code_aggregates =
            report_aggregates->second.by_event_code().find(logged_event_code);
        if (event_code_aggregates ==
            report_aggregates->second.by_event_code().end()) {
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
          auto day_aggregate =
              event_code_aggregates->second.by_day_index().find(
                  logged_day_index);
          if (logged_day_index >= earliest_allowed) {
            EXPECT_NE(day_aggregate,
                      event_code_aggregates->second.by_day_index().end());
            if (day_aggregate ==
                event_code_aggregates->second.by_day_index().end()) {
              return false;
            }
            EXPECT_TRUE(day_aggregate->second.activity_daily_aggregate()
                            .activity_indicator());
            if (!day_aggregate->second.activity_daily_aggregate()
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
  // collection and the number of backfill days.
  uint32_t EarliestAllowedDayIndex(const AggregationConfig& config) {
    // If the LocalAggregateStore has never been garbage-collected, then the
    // earliest allowed day index is just the day when the store was created,
    // minus the number of backfill days.
    auto backfill_days = GetBackfillDays();
    EXPECT_GE(kStartDayIndex, backfill_days)
        << "The day index of store creation must be larger than the number "
           "of backfill days.";
    if (day_last_garbage_collected_ == 0u) {
      return kStartDayIndex - backfill_days;
    } else {
      // Otherwise, it is the later of:
      // (a) The day index on which the store was created minus the number
      // of backfill days.
      // (b) The day index for which the store was last garbage-collected minus
      // the number of backfill days, minus the largest window size in the
      // report associated to |config|, plus 1.
      EXPECT_GE(day_last_garbage_collected_, backfill_days)
          << "The day index of last garbage collection must be larger than "
             "the number of backfill days.";
      uint32_t max_window_size = 1;
      for (uint32_t window_size : config.report().window_size()) {
        if (window_size > max_window_size) {
          max_window_size = window_size;
        }
      }
      if (day_last_garbage_collected_ - backfill_days < (max_window_size + 1)) {
        return kStartDayIndex - backfill_days;
      }
      return (kStartDayIndex <
              (day_last_garbage_collected_ - max_window_size + 1))
                 ? (day_last_garbage_collected_ - backfill_days -
                    max_window_size + 1)
                 : kStartDayIndex - backfill_days;
    }
  }

  std::unique_ptr<MockConsistentProtoStore> local_aggregate_proto_store_;
  std::unique_ptr<MockConsistentProtoStore> obs_history_proto_store_;
  std::unique_ptr<EventAggregator> event_aggregator_;

  std::unique_ptr<TestUpdateRecipient> update_recipient_;
  std::unique_ptr<FakeObservationStore> observation_store_;

  // The day index on which the LocalAggregateStore was last
  // garbage-collected. A value of 0 indicates that the store has never been
  // garbage-collected.
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

  // Logs a UniqueActivesEvent for the MetricReportId of a locally
  // aggregated report in |metric_string|. Overrides the method
  // EventAggregatorTest::LogUniqueActivesEvent.
  Status LogUniqueActivesEvent(const MetricReportId& metric_report_id,
                               uint32_t day_index, uint32_t event_code,
                               LoggedActivity* logged_activity = nullptr) {
    return EventAggregatorTest::LogUniqueActivesEvent(
        *project_context_, metric_report_id, day_index, event_code,
        logged_activity);
  }

 private:
  // A ProjectContext wrapping the MetricDefinitions passed to the
  // constructor in |metric_string|.
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

// Tests that the Read() method of each ConsistentProtoStore is called once
// during construction of the EventAggregator.
TEST_F(EventAggregatorTest, ReadProtosFromFiles) {
  EXPECT_EQ(1, local_aggregate_proto_store_->read_count_);
  EXPECT_EQ(1, obs_history_proto_store_->read_count_);
}

// Tests that the BackUp*() methods return a positive status, and checks that
// the Write() method of a ConsistentProtoStore is called once when its
// respective BackUp*() method is called.
TEST_F(EventAggregatorTest, BackUpProtos) {
  EXPECT_EQ(kOK, BackUpLocalAggregateStore());
  EXPECT_EQ(kOK, BackUpObservationHistory());
  EXPECT_EQ(1, local_aggregate_proto_store_->write_count_);
  EXPECT_EQ(1, obs_history_proto_store_->write_count_);
}

// Tests that an empty LocalAggregateStore is updated with
// ReportAggregationKeys and AggregationConfigs as expected when
// EventAggregator::UpdateAggregationConfigs is called.
TEST_F(EventAggregatorTest, UpdateAggregationConfigs) {
  // Check that the LocalAggregateStore is empty.
  EXPECT_EQ(0u, GetLocalAggregateStore().aggregates().size());
  // Provide |kUniqueActivesMetricDefinitions| to the EventAggregator.
  auto unique_actives_project_context =
      MakeProjectContext(kUniqueActivesMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(
                     *unique_actives_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in
  // |kUniqueActivesMetricDefinitions|.
  EXPECT_EQ(kUniqueActivesExpectedParams.metric_report_ids.size(),
            GetLocalAggregateStore().aggregates().size());
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
    auto local_aggregate_store = GetLocalAggregateStore();
    EXPECT_NE(local_aggregate_store.aggregates().end(),
              local_aggregate_store.aggregates().find(key));
    EXPECT_TRUE(MessageDifferencer::Equals(
        config,
        local_aggregate_store.aggregates().at(key).aggregation_config()));
  }
}

// Tests two assumptions about the behavior of
// EventAggregator::UpdateAggregationConfigs when two projects with the same
// customer ID and project ID provide configurations to the EventAggregator.
// These assumptions are:
// (1) If the second project provides a report with a
// ReportAggregationKey which was not provided by the first project, then
// the EventAggregator accepts the new report. (2) If a report provided by
// the second project has a ReportAggregationKey which was already provided
// by the first project, then the EventAggregator rejects the new report,
// even if its ReportDefinition differs from that of existing report with
// the same ReportAggregationKey.
TEST_F(EventAggregatorTest, UpdateAggregationConfigsWithSameKey) {
  // Provide the EventAggregator with |kUniqueActivesMetricDefinitions|.
  auto unique_actives_project_context =
      MakeProjectContext(kUniqueActivesMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(
                     *unique_actives_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in
  // |kUniqueActivesMetricDefinitions|.
  EXPECT_EQ(3u, GetLocalAggregateStore().aggregates().size());

  // Provide the EventAggregator with
  // |kNoiseFreeUniqueActivesMetricDefinitions|.
  auto noise_free_unique_actives_project_context =
      MakeProjectContext(kNoiseFreeUniqueActivesMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(
                     *noise_free_unique_actives_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of distinct MetricReportIds of locally
  // aggregated reports in |kUniqueActivesMetricDefinitions| and
  // |kNoiseFreeUniqueActivesMetricDefinitions|.
  auto local_aggregate_store = GetLocalAggregateStore();
  EXPECT_EQ(4u, local_aggregate_store.aggregates().size());
  // The MetricReportId |kFeaturesActiveMetricReportId| appears in both
  // |kUniqueActivesMetricDefinitions| and
  // |kNoiseFreeUniqueActivesMetricDefinitions|. The associated
  // ReportAggregationKeys are identical, but the AggregationConfigs are
  // different.
  //
  // Check that the AggregationConfig stored in the LocalAggregateStore
  // under the key associated to |kFeaturesActiveMetricReportId| is the
  // first AggregationConfig that was provided for that key; i.e., is
  // derived from |kUniqueActivesMetricDefinitions|.
  std::string key;
  EXPECT_TRUE(
      SerializeToBase64(MakeAggregationKey(*unique_actives_project_context,
                                           kFeaturesActiveMetricReportId),
                        &key));
  auto unique_actives_config = MakeAggregationConfig(
      *unique_actives_project_context, kFeaturesActiveMetricReportId);
  EXPECT_NE(local_aggregate_store.aggregates().end(),
            local_aggregate_store.aggregates().find(key));
  EXPECT_TRUE(MessageDifferencer::Equals(
      unique_actives_config,
      local_aggregate_store.aggregates().at(key).aggregation_config()));
  auto noise_free_config =
      MakeAggregationConfig(*noise_free_unique_actives_project_context,
                            kFeaturesActiveMetricReportId);
  EXPECT_FALSE(MessageDifferencer::Equals(
      noise_free_config,
      local_aggregate_store.aggregates().at(key).aggregation_config()));
}

// Tests that EventAggregator::LogUniqueActivesEvent returns
// |kInvalidArguments| when passed a report ID which is not associated to a
// key of the LocalAggregateStore, or when passed an EventRecord containing
// an Event proto message which is not of type OccurrenceEvent.
TEST_F(EventAggregatorTest, LogBadEvents) {
  // Provide the EventAggregator with |kUniqueActivesMetricDefinitions|.
  auto unique_actives_project_context =
      MakeProjectContext(kUniqueActivesMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(
                     *unique_actives_project_context));
  // Attempt to log a UniqueActivesEvent for
  // |kEventsOccurredMetricReportId|, which is not in
  // |kUniqueActivesMetricDefinitions|. Check that the result is
  // |kInvalidArguments|.
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
// LocalAggregateStore, and with an EventRecord which wraps an
// OccurrenceEvent.
//
// Logs some valid events each day for 35 days, checking the contents of the
// LocalAggregateStore each day.
TEST_F(UniqueActivesEventAggregatorTest, LogUniqueActivesEvents) {
  LoggedActivity logged_activity;
  uint32_t num_days = 35;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    // Log an event for the FeaturesActive_UniqueDevices report of
    // |kUniqueActivesMetricDefinitions| with event code 0. Check the
    // contents of the LocalAggregateStore.
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
    // Log several more events for various valid reports and event codes.
    // Check the contents of the LocalAggregateStore.
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
// For each value of N in the range [0, 34], logs some UniqueActivesEvents
// each day for N consecutive days and then garbage-collect the
// LocalAggregateStore. After garbage collection, verifies the contents of
// the LocalAggregateStore.
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

// Tests that EventAggregator::GenerateObservations() returns a positive
// status and that the expected number of Observations is generated when no
// Events have been logged to the EventAggregator.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservationsNoEvents) {
  EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(kStartDayIndex));
  std::vector<Observation2> observations(0);
  EXPECT_TRUE(FetchAggregatedObservations(
      &observations, kUniqueActivesExpectedParams, observation_store_.get(),
      update_recipient_.get()));
}

// Tests that EventAggregator::GenerateObservations() only generates
// Observations the first time it is called for a given day index.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservationsTwice) {
  // Check that Observations are generated when GenerateObservations is called
  // for |kStartDayIndex| for the first time.
  EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(kStartDayIndex));
  std::vector<Observation2> observations(0);
  EXPECT_TRUE(FetchAggregatedObservations(
      &observations, kUniqueActivesExpectedParams, observation_store_.get(),
      update_recipient_.get()));
  // Check that no Observations are generated when GenerateObservations is
  // called for |kStartDayIndex| for the second time.
  ResetObservationStore();
  EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(kStartDayIndex));
  EXPECT_EQ(0u, observation_store_->messages_received.size());
}

// Tests that EventAggregator::GenerateObservations() returns a positive
// status and that the expected number of Observations is generated after
// some UniqueActivesEvents have been logged, without any garbage
// collection.
//
// For 35 days, logs 2 events each day for the ErrorsOccurred_UniqueDevices
// report and 2 events for the FeaturesActive_UniqueDevices report, all
// with event code 0.
//
// Each day following the first day, calls GenerateObservations() with the
// day index of the previous day. Checks that a positive status is returned
// and that the FakeObservationStore has received the expected number of new
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
// expected number of Observations is generated each day when Events are logged
// for UNIQUE_N_DAY_ACTIVES reports over multiple days, and when the
// LocalAggregateStore is garbage-collected each day.
//
// For 35 days, logs 2 events each day for the ErrorsOccurred_UniqueDevices
// report and 2 events for the FeaturesActive_UniqueDevices report, all
// with event code 0.
//
// Each day following the first day, calls GenerateObservations() and then
// GarbageCollect() with the day index of the current day. Checks that
// positive statuses are returned and that the FakeObservationStore has
// received the expected number of new observations for each locally
// aggregated report ID in |kUniqueActivesMetricDefinitions|.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservationsWithGc) {
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

// Tests that GenerateObservations() returns a positive status and that the
// expected number of Observations is generated when events are logged over
// multiple days and some of those days' Observations are backfilled, without
// any garbage collection of the LocalAggregateStore.
//
// Sets the |backfill_days_| field of the EventAggregator to 3.
//
// Logging pattern:
// For 35 days, logs 2 events each day for the SomeErrorsOccurred_UniqueDevices
// report and 2 events for the SomeFeaturesActive_Unique_Devices report, all
// with event code 0.
//
// Observation generation pattern:
// Calls GenerateObservations() on the 1st through 5th and the 7th out of
// every 10 days, for 35 days.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on
// the first day of every 10 (the day index for which GenerateObservations() was
// called, plus 3 days of backfill), that 1 day's worth of Observations
// are generated on the 2nd through 5th day of every 10, that 2 days'
// worth of Observations are generated on the 7th day of every 10 (the
// day index for which GenerateObservations() was called, plus 1 day of
// backfill), and that no Observations are generated on the remaining days.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservationsWithBackfill) {
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Starting on kStartDayIndex, log 2 events each day for 35 days. Call
  // GenerateObservations() on the first 5 day indices, and the 7th, out of
  // every 10.
  for (uint32_t day_index = kStartDayIndex; day_index < kStartDayIndex + 35;
       day_index++) {
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kErrorsOccurredMetricReportId,
                                           day_index, 0u));
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                           day_index, 0u));
    }
    auto num_obs_before = observation_store_->messages_received.size();
    if ((day_index - kStartDayIndex) % 10 < 5 ||
        (day_index - kStartDayIndex) % 10 == 6) {
      EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(day_index));
    }
    auto num_obs_after = observation_store_->messages_received.size();
    EXPECT_GE(num_obs_after, num_obs_before);
    // Check that the expected daily number of Observations was generated.
    switch ((day_index - kStartDayIndex) % 10) {
      case 0:
        EXPECT_EQ(
            kUniqueActivesExpectedParams.daily_num_obs * (backfill_days + 1),
            num_obs_after - num_obs_before);
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        EXPECT_EQ(kUniqueActivesExpectedParams.daily_num_obs,
                  num_obs_after - num_obs_before);
        break;
      case 6:
        EXPECT_EQ(kUniqueActivesExpectedParams.daily_num_obs * 2,
                  num_obs_after - num_obs_before);
        break;
      default:
        EXPECT_EQ(num_obs_after, num_obs_before);
    }
  }
}

// Tests that GenerateObservations() returns a positive status and that the
// expected number of Observations is generated when events are logged over
// multiple days and some of those days' Observations are backfilled, and when
// the LocalAggregateStore is garbage-collected after each call to
// GenerateObservations().
//
// Sets the |backfill_days_| field of the EventAggregator to 3.
//
// Logging pattern:
// For 35 days, logs 2 events each day for the SomeErrorsOccurred_UniqueDevices
// report and 2 events for the SomeFeaturesActive_Unique_Devices report, all
// with event code 0.
//
// Observation generation pattern:
// Calls GenerateObservations() on the 1st through 5th and the 7th out of
// every 10 days, for 35 days. Garbage-collects the LocalAggregateStore after
// each call.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on
// the first day of every 10 (the day index for which GenerateObservations() was
// called, plus 3 days of backfill), that 1 day's worth of Observations
// are generated on the 2nd through 5th day of every 10, that 2 days'
// worth of Observations are generated on the 7th day of every 10 (the
// day index for which GenerateObservations() was called, plus 1 day of
// backfill), and that no Observations are generated on the remaining days.
TEST_F(UniqueActivesEventAggregatorTest,
       GenerateObservationsWithBackfillAndGc) {
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Starting on kStartDayIndex, log 2 events each day for 35 days. Call
  // GenerateObservations() on the first 5 day indices, and the 7th, out of
  // every 10.
  for (uint32_t day_index = kStartDayIndex; day_index < kStartDayIndex + 35;
       day_index++) {
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kErrorsOccurredMetricReportId,
                                           day_index, 0u));
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                           day_index, 0u));
    }
    auto num_obs_before = observation_store_->messages_received.size();
    if ((day_index - kStartDayIndex) % 10 < 5 ||
        (day_index - kStartDayIndex) % 10 == 6) {
      EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(day_index));
      EXPECT_EQ(kOK, event_aggregator_->GarbageCollect(day_index));
    }
    auto num_obs_after = observation_store_->messages_received.size();
    EXPECT_GE(num_obs_after, num_obs_before);
    // Check that the expected daily number of Observations was generated.
    // This expected number is some multiple of the daily_num_obs field of
    // |kUniqueActivesExpectedParams|, depending on the number of days which
    // should have been backfilled when GenerateObservations() was called.
    switch ((day_index - kStartDayIndex) % 10) {
      case 0:
        EXPECT_EQ(
            kUniqueActivesExpectedParams.daily_num_obs * (backfill_days + 1),
            num_obs_after - num_obs_before);
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        EXPECT_EQ(kUniqueActivesExpectedParams.daily_num_obs,
                  num_obs_after - num_obs_before);
        break;
      case 6:
        EXPECT_EQ(kUniqueActivesExpectedParams.daily_num_obs * 2,
                  num_obs_after - num_obs_before);
        break;
      default:
        EXPECT_EQ(num_obs_after, num_obs_before);
    }
  }
}

// Checks that UniqueActivesObservations with the expected values (i.e.,
// non-active for all UNIQUE_N_DAY_ACTIVES reports, for all window sizes and
// event codes) are generated when no Events have been logged to the
// EventAggregator.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesNoEvents) {
  EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(kStartDayIndex));
  auto expected_obs = MakeNullExpectedUniqueActivesObservations(
      kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex);
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_obs, observation_store_.get(), update_recipient_.get()));
}

// Checks that UniqueActivesObservations with the expected values are generated
// when GenerateObservations() is called for a single day index after logging
// some events for UNIQUE_N_DAY_ACTIVES reports for that day index, without any
// garbage collection or backfill.
//
// Logging pattern:
// Logs 2 occurrences of event code 0 for the FeaturesActives_UniqueDevices
// report, and 1 occurrence of event code 1 for the EventsOccurred_UniqueDevices
// report, both for the day |kStartDayIndex|.
//
// Observation generation pattern:
// Calls GenerateObservations() for |kStartDayIndex| after logging all events.
//
// Expected numbers of Observations:
// The expected number of Observations is the daily_num_obs field of
// |kNoiseFreeUniqueActivesExpectedParams|.
//
// Expected Observation values:
// All Observations should have day index |kStartDayIndex|.
//
// For the FeaturesActive_UniqueDevices report, expect activity indicators:
//
// window size        active for event codes
// ------------------------------------------
// 1                           0
// 7                           0
// 30                          0
//
// For the EventsOccurred_UniqueDevices report, expected activity indicators:
// window size        active for event codes
// ------------------------------------------
// 1                           1
// 7                           1
//
// All other Observations should be of inactivity.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesSingleDay) {
  // Log several events on |kStartDayIndex|.
  EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                       kStartDayIndex, 0u));
  EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                       kStartDayIndex, 0u));
  EXPECT_EQ(kOK, LogUniqueActivesEvent(kEventsOccurredMetricReportId,
                                       kStartDayIndex, 1u));
  // Generate locally aggregated Observations.
  EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(kStartDayIndex));

  // Form the expected observations.
  auto expected_obs = MakeNullExpectedUniqueActivesObservations(
      kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex);
  expected_obs[{kFeaturesActiveMetricReportId, kStartDayIndex}] = {
      {1, {true, false, false, false, false}},
      {7, {true, false, false, false, false}},
      {30, {true, false, false, false, false}}};
  expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex}] = {
      {1, {false, true, false, false, false}},
      {7, {false, true, false, false, false}}};

  // Check the contents of the FakeObservationStore.
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_obs, observation_store_.get(), update_recipient_.get()));
}

// Checks that UniqueActivesObservations with the expected values are
// generated when some events have been logged for a UNIQUE_N_DAY_ACTIVES
// report for over multiple days and GenerateObservations() is called each day,
// without garbage collection or backfill.
//
// Logging pattern:
// Logs events for the SomeEventsOccurred_UniqueDevices report (whose parent
// metric has max_event_code = 4) for 10 days, according to the following
// pattern:
//
// * Never log event code 0.
// * On the i-th day (0-indexed) of logging, log an event for event code k,
// 1 <= k < 5, if 3*k divides i.
//
// Observation generation pattern:
// Each day following the first day, generates Observations for the previous
// day index.
//
// Expected number of Observations:
// Each call to GenerateObservations should generate a number of Observations
// equal to the daily_num_obs field of |kNoisefreeUniqueActivesExpectedParams|.
//
// Expected Observation values:
// The SomeEventsOccurred_UniqueDevices report has window sizes 1 and 7, and
// the expected activity indicators of Observations for that report on the i-th
// day are:
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
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesMultiDay) {
  // Form expected Obsevations for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedUniqueActivesObservations> expected_obs(num_days);
  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_obs[offset] = MakeNullExpectedUniqueActivesObservations(
        kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex + offset);
  }
  expected_obs[0][{kEventsOccurredMetricReportId, kStartDayIndex}] = {
      {1, {false, true, true, true, true}},
      {7, {false, true, true, true, true}}};
  expected_obs[1][{kEventsOccurredMetricReportId, kStartDayIndex + 1}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[2][{kEventsOccurredMetricReportId, kStartDayIndex + 2}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[3][{kEventsOccurredMetricReportId, kStartDayIndex + 3}] = {
      {1, {false, true, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[4][{kEventsOccurredMetricReportId, kStartDayIndex + 4}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[5][{kEventsOccurredMetricReportId, kStartDayIndex + 5}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[6][{kEventsOccurredMetricReportId, kStartDayIndex + 6}] = {
      {1, {false, true, true, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[7][{kEventsOccurredMetricReportId, kStartDayIndex + 7}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_obs[8][{kEventsOccurredMetricReportId, kStartDayIndex + 8}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_obs[9][{kEventsOccurredMetricReportId, kStartDayIndex + 9}] = {
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
    EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs[offset],
                                               observation_store_.get(),
                                               update_recipient_.get()));
  }
}

// Checks that UniqueActivesObservations with the expected values are
// generated when some events have been logged for a UNIQUE_N_DAY_ACTIVES
// report for over multiple days and GenerateObservations() is called each day,
// and when the LocalAggregateStore is garbage-collected after each call to
// GenerateObservations().
//
// Logging pattern:
// Logs events for the SomeEventsOccurred_UniqueDevices report (whose parent
// metric has max_event_code = 4) for 10 days, according to the following
// pattern:
//
// * Never log event code 0.
// * On the i-th day (0-indexed) of logging, log an event for event code k,
// 1 <= k < 5, if 3*k divides i.
//
// Observation generation pattern:
// Each day following the first day, generates Observations for the previous
// day index.
//
// Expected number of Observations:
// Each call to GenerateObservations should generate a number of Observations
// equal to the daily_num_obs field of |kNoisefreeUniqueActivesExpectedParams|.
//
// Expected Observation values:
// The SomeEventsOccurred_UniqueDevices report has window sizes 1 and 7, and
// the expected activity indicators of Observations for that report on the i-th
// day are:
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
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesMultiDayWithGarbageCollection) {
  // Form expected Observations for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedUniqueActivesObservations> expected_obs(num_days);
  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_obs[offset] = MakeNullExpectedUniqueActivesObservations(
        kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex + offset);
  }
  expected_obs[0][{kEventsOccurredMetricReportId, kStartDayIndex}] = {
      {1, {false, true, true, true, true}},
      {7, {false, true, true, true, true}}};
  expected_obs[1][{kEventsOccurredMetricReportId, kStartDayIndex + 1}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[2][{kEventsOccurredMetricReportId, kStartDayIndex + 2}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[3][{kEventsOccurredMetricReportId, kStartDayIndex + 3}] = {
      {1, {false, true, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[4][{kEventsOccurredMetricReportId, kStartDayIndex + 4}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[5][{kEventsOccurredMetricReportId, kStartDayIndex + 5}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[6][{kEventsOccurredMetricReportId, kStartDayIndex + 6}] = {
      {1, {false, true, true, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[7][{kEventsOccurredMetricReportId, kStartDayIndex + 7}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_obs[8][{kEventsOccurredMetricReportId, kStartDayIndex + 8}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_obs[9][{kEventsOccurredMetricReportId, kStartDayIndex + 9}] = {
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
    EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs[offset],
                                               observation_store_.get(),
                                               update_recipient_.get()));
    // Garbage collect for the current day index.
    EXPECT_EQ(kOK, event_aggregator_->GarbageCollect(kStartDayIndex + offset));
  }
}

// Tests that the expected UniqueActivesObservations are generated when events
// are logged over multiple days and when Observations are backfilled for some
// days during that period, without any garbage-collection of the
// LocalAggregateStore.
//
// The test sets the number of backfill days to 3.
//
// Logging pattern:
// Events for the EventsOccurred_UniqueDevices report are logged over the days
// |kStartDayIndex| to |kStartDayIndex + 8| according to the following pattern:
//
// * For i = 0 to i = 4, log an event with event code i on day
// |kStartDayIndex + i| and |kStartDayIndex + 2*i|.
//
// Observation generation pattern:
// The test calls GenerateObservations() on day |kStartDayIndex + i| for i = 0
// through i = 5 and for i = 8, skipping the days |kStartDayIndex + 6| and
// |kStartDayIndex + 7|.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on the
// first day (the day index for which GenerateObservations() was called, plus 3
// days of backfill), that 1 day's worth of Observations is generated on the
// 2nd through 6th day, that 3 days' worth of Observations are generated on the
// 9th day (the day index for which GenerateObservations() was called, plus 2
// days of backfill), and that no Observations are generated on the remaining
// days.
//
// Expected Observation values:
// The expected activity indicators of Observations for the
// EventsOccurred_UniqueDevices report for the i-th day of logging are:
//
// (i, window size)           active for event codes
// -------------------------------------------------------------------------
// (0, 1)                           0
// (0, 7)                           0
// (1, 1)                           1
// (1, 7)                           0, 1
// (2, 1)                           1, 2
// (2, 7)                           0, 1, 2
// (3, 1)                           3
// (3, 7)                           0, 1, 2, 3
// (4, 1)                           2, 4
// (4, 7)                           0, 1, 2, 3, 4
// (5, 1)                          ---
// (5, 7)                           0, 1, 2, 3, 4
// (6, 1)                           3
// (6, 7)                           0, 1, 2, 3, 4
// (7, 1)                          ---
// (7, 7)                           1, 2, 3, 4
// (8, 1)                           4
// (8, 7)                           1, 2, 3, 4
//
// All other Observations should be of non-activity.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesWithBackfill) {
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log events for 9 days. Call GenerateObservations() on the first 6 day
  // indices, and the 9th.
  for (uint32_t offset = 0; offset < 9; offset++) {
    ResetObservationStore();
    for (uint32_t event_code = 0;
         event_code < kNoiseFreeUniqueActivesExpectedParams.num_event_codes.at(
                          kEventsOccurredMetricReportId);
         event_code++) {
      if (event_code == offset || (2 * event_code) == offset) {
        EXPECT_EQ(kOK,
                  LogUniqueActivesEvent(kEventsOccurredMetricReportId,
                                        kStartDayIndex + offset, event_code));
      }
    }
    if (offset < 6 || offset == 8) {
      EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(kStartDayIndex +
                                                             offset));
    }
    // Make the set of Observations which are expected to be generated on
    // |kStartDayIndex + offset| and check it against the contents of the
    // FakeObservationStore.
    ExpectedUniqueActivesObservations expected_obs;
    switch (offset) {
      case 0: {
        for (uint32_t day_index = kStartDayIndex - backfill_days;
             day_index <= kStartDayIndex; day_index++) {
          for (const auto& pair : MakeNullExpectedUniqueActivesObservations(
                   kNoiseFreeUniqueActivesExpectedParams, day_index)) {
            expected_obs.insert(pair);
          }
        }
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex}] = {
            {1, {true, false, false, false, false}},
            {7, {true, false, false, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 1: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex + 1);
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 1}] = {
            {1, {false, true, false, false, false}},
            {7, {true, true, false, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 2: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex + 2);
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 2}] = {
            {1, {false, true, true, false, false}},
            {7, {true, true, true, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 3: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex + 3);
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 3}] = {
            {1, {false, false, false, true, false}},
            {7, {true, true, true, true, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 4: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex + 4);
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 4}] = {
            {1, {false, false, true, false, true}},
            {7, {true, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 5: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex + 5);
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 5}] = {
            {1, {false, false, false, false, false}},
            {7, {true, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 8: {
        for (uint32_t day_index = kStartDayIndex + 6;
             day_index <= kStartDayIndex + 8; day_index++) {
          for (const auto& pair : MakeNullExpectedUniqueActivesObservations(
                   kNoiseFreeUniqueActivesExpectedParams, day_index)) {
            expected_obs.insert(pair);
          }
        }
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 6}] = {
            {1, {false, false, false, true, false}},
            {7, {true, true, true, true, true}}};
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 7}] = {
            {1, {false, false, false, false, false}},
            {7, {false, true, true, true, true}}};
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 8}] = {
            {1, {false, false, false, false, true}},
            {7, {false, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      default:
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
    }
  }
}

// Tests that the expected UniqueActivesObservations are generated when events
// are logged over multiple days and when Observations are backfilled for some
// days during that period, and when the LocalAggregateStore is
// garbage-collected after each all to GenerateObservations().
//
// The test sets the number of backfill days to 3.
//
// Logging pattern:
// Events for the EventsOccurred_UniqueDevices report are logged over the days
// |kStartDayIndex| to |kStartDayIndex + 8| according to the following pattern:
//
// * For i = 0 to i = 4, log an event with event code i on day
// |kStartDayIndex + i| and |kStartDayIndex + 2*i|.
//
// Observation generation pattern:
// The test calls GenerateObservations() on day |kStartDayIndex + i| for i = 0
// through i = 5 and for i = 8, skipping the days |kStartDayIndex + 6| and
// |kStartDayIndex + 7|.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on the
// first day (the day index for which GenerateObservations() was called, plus 3
// days of backfill), that 1 day's worth of Observations is generated on the
// 2nd through 6th day, that 3 days' worth of Observations are generated on the
// 9th day (the day index for which GenerateObservations() was called, plus 2
// days of backfill), and that no Observations are generated on the remaining
// days.
//
// Expected Observation values:
// The expected activity indicators of Observations for the
// EventsOccurred_UniqueDevices report for the i-th day of logging are:
//
// (i, window size)           active for event codes
// -------------------------------------------------------------------------
// (0, 1)                           0
// (0, 7)                           0
// (1, 1)                           1
// (1, 7)                           0, 1
// (2, 1)                           1, 2
// (2, 7)                           0, 1, 2
// (3, 1)                           3
// (3, 7)                           0, 1, 2, 3
// (4, 1)                           2, 4
// (4, 7)                           0, 1, 2, 3, 4
// (5, 1)                          ---
// (5, 7)                           0, 1, 2, 3, 4
// (6, 1)                           3
// (6, 7)                           0, 1, 2, 3, 4
// (7, 1)                          ---
// (7, 7)                           1, 2, 3, 4
// (8, 1)                           4
// (8, 7)                           1, 2, 3, 4
//
// All other Observations should be of non-activity.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesWithBackfillAndGc) {
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log events for 9 days. Call GenerateObservations() on the first 6 day
  // indices, and the 9th.
  for (uint32_t offset = 0; offset < 8; offset++) {
    ResetObservationStore();
    for (uint32_t event_code = 0;
         event_code < kNoiseFreeUniqueActivesExpectedParams.num_event_codes.at(
                          kEventsOccurredMetricReportId);
         event_code++) {
      if (event_code == offset || (2 * event_code) == offset) {
        EXPECT_EQ(kOK,
                  LogUniqueActivesEvent(kEventsOccurredMetricReportId,
                                        kStartDayIndex + offset, event_code));
      }
    }
    if (offset < 6 || offset == 9) {
      EXPECT_EQ(kOK, event_aggregator_->GenerateObservations(kStartDayIndex +
                                                             offset));
      EXPECT_EQ(kOK,
                event_aggregator_->GarbageCollect(kStartDayIndex + offset));
    }
    // Make the set of Observations which are expected to be generated on
    // |kStartDayIndex + offset| and check it against the contents of the
    // FakeObservationStore.
    ExpectedUniqueActivesObservations expected_obs;
    switch (offset) {
      case 0: {
        for (uint32_t day_index = kStartDayIndex - backfill_days;
             day_index <= kStartDayIndex; day_index++) {
          for (const auto& pair : MakeNullExpectedUniqueActivesObservations(
                   kNoiseFreeUniqueActivesExpectedParams, day_index)) {
            expected_obs.insert(pair);
          }
        }
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex}] = {
            {1, {true, false, false, false, false}},
            {7, {true, false, false, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 1: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex + 1);
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 1}] = {
            {1, {false, true, false, false, false}},
            {7, {true, true, false, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 2: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex + 2);
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 2}] = {
            {1, {false, true, true, false, false}},
            {7, {true, true, true, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 3: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex + 3);
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 3}] = {
            {1, {false, false, false, true, false}},
            {7, {true, true, true, true, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 4: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex + 4);
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 4}] = {
            {1, {false, false, true, false, true}},
            {7, {true, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 5: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, kStartDayIndex + 5);
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 5}] = {
            {1, {false, false, false, false, false}},
            {7, {true, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 8: {
        for (uint32_t day_index = kStartDayIndex + 6;
             day_index <= kStartDayIndex + 8; day_index++) {
          for (const auto& pair : MakeNullExpectedUniqueActivesObservations(
                   kNoiseFreeUniqueActivesExpectedParams, day_index)) {
            expected_obs.insert(pair);
          }
        }
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 6}] = {
            {1, {false, false, false, true, false}},
            {7, {true, true, true, true, true}}};
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 7}] = {
            {1, {false, false, false, false, false}},
            {7, {false, true, true, true, true}}};
        expected_obs[{kEventsOccurredMetricReportId, kStartDayIndex + 8}] = {
            {1, {false, false, false, false, true}},
            {7, {false, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      default:
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
    }
  }
}

}  // namespace logger
}  // namespace cobalt

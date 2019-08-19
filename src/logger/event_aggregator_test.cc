#include <memory>

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include "src/gtest.h"
#include "src/lib/util/clock.h"
#include "src/lib/util/datetime_util.h"
#include "src/lib/util/proto_util.h"
#include "src/logger/event_aggregator.h"
#include "src/logger/logger_test_utils.h"
#include "src/logger/testing_constants.h"
#include "src/pb/event.pb.h"
#include "src/registry/packed_event_codes.h"
#include "src/registry/project_configs.h"

namespace cobalt {

using config::PackEventCodes;
using encoder::ClientSecret;
using encoder::SystemDataInterface;
using util::EncryptedMessageMaker;
using util::IncrementingClock;
using util::SerializeToBase64;
using util::TimeToDayIndex;

namespace logger {

using testing::CheckPerDeviceNumericObservations;
using testing::CheckUniqueActivesObservations;
using testing::ExpectedAggregationParams;
using testing::ExpectedPerDeviceNumericObservations;
using testing::ExpectedReportParticipationObservations;
using testing::ExpectedUniqueActivesObservations;
using testing::FakeObservationStore;
using testing::FetchAggregatedObservations;
using testing::GetTestProject;
using testing::MakeAggregationConfig;
using testing::MakeAggregationKey;
using testing::MakeExpectedReportParticipationObservations;
using testing::MakeNullExpectedUniqueActivesObservations;
using testing::MockConsistentProtoStore;
using testing::TestUpdateRecipient;

namespace {
// Number of seconds in a day
constexpr int kDay = 60 * 60 * 24;
// Number of seconds in an ideal year
constexpr int kYear = kDay * 365;

template <typename T>
std::string SerializeAsStringDeterministic(const T& message) {
  std::string s;
  {
    google::protobuf::io::StringOutputStream output(&s);
    google::protobuf::io::CodedOutputStream out(&output);
    out.SetSerializationDeterministic(true);
    message.SerializePartialToCodedStream(&out);
  }
  return s;
}

// Filenames for constructors of ConsistentProtoStores
constexpr char kAggregateStoreFilename[] = "local_aggregate_store_backup";
constexpr char kObsHistoryFilename[] = "obs_history_backup";

// A map keyed by base64-encoded, serialized ReportAggregationKeys. The value at
// a key is a map of event codes to sets of day indices. Used in tests as
// a record, external to the LocalAggregateStore, of the activity logged for
// UNIQUE_N_DAY_ACTIVES reports.
using LoggedActivity = std::map<std::string, std::map<uint32_t, std::set<uint32_t>>>;

// A map used in tests as a record, external to the LocalAggregateStore, of the
// activity logged for PER_DEVICE_NUMERIC_STATS reports. The keys are, in
// descending order, serialized ReportAggregationKeys, components, event codes,
// and day indices. Each day index maps to a vector of numeric values that were
// logged for that day index..
using LoggedValues =
    std::map<std::string,
             std::map<std::string, std::map<uint32_t, std::map<uint32_t, std::vector<int64_t>>>>>;

}  // namespace

// EventAggregatorTest creates an EventAggregator which sends its Observations
// to a FakeObservationStore. The EventAggregator is not pre-populated with
// aggregation configurations.
class EventAggregatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    observation_store_ = std::make_unique<FakeObservationStore>();
    update_recipient_ = std::make_unique<TestUpdateRecipient>();
    observation_encrypter_ = EncryptedMessageMaker::MakeUnencrypted();
    observation_writer_ = std::make_unique<ObservationWriter>(
        observation_store_.get(), update_recipient_.get(), observation_encrypter_.get());
    encoder_ = std::make_unique<Encoder>(ClientSecret::GenerateNewSecret(), system_data_.get());
    local_aggregate_proto_store_ =
        std::make_unique<MockConsistentProtoStore>(kAggregateStoreFilename);
    obs_history_proto_store_ = std::make_unique<MockConsistentProtoStore>(kObsHistoryFilename);
    ResetEventAggregator();
  }

  void ResetEventAggregator() {
    event_aggregator_ = std::make_unique<EventAggregator>(encoder_.get(), observation_writer_.get(),
                                                          local_aggregate_proto_store_.get(),
                                                          obs_history_proto_store_.get());
    // This clock is used to create and track times that are passed to the
    // EventAggregator, but is not passed to the EventAggregator itself.
    test_clock_ = std::make_unique<IncrementingClock>(std::chrono::system_clock::duration(0));
    // Initilize it to 10 years after the beginning of time.
    test_clock_->set_time(std::chrono::system_clock::time_point(std::chrono::seconds(10 * kYear)));
    day_store_created_ = CurrentDayIndex();
  }

  // Destruct the EventAggregator (thus calling EventAggregator::ShutDown())
  // before destructing the objects which the EventAggregator points to but does
  // not own.
  void TearDown() override { event_aggregator_.reset(); }

  // Advances |test_clock_| by |num_seconds| seconds.
  void AdvanceClock(int num_seconds) {
    test_clock_->increment_by(std::chrono::seconds(num_seconds));
  }

  // Returns the day index of the current day according to |test_clock_|, in
  // |time_zone|, without incrementing the clock.
  uint32_t CurrentDayIndex(MetricDefinition::TimeZonePolicy time_zone = MetricDefinition::UTC) {
    return TimeToDayIndex(std::chrono::system_clock::to_time_t(test_clock_->peek_now()), time_zone);
  }

  size_t GetBackfillDays() { return event_aggregator_->backfill_days_; }

  void SetBackfillDays(size_t num_days) { event_aggregator_->backfill_days_ = num_days; }

  Status BackUpLocalAggregateStore() { return event_aggregator_->BackUpLocalAggregateStore(); }

  Status BackUpObservationHistory() { return event_aggregator_->BackUpObservationHistory(); }

  LocalAggregateStore CopyLocalAggregateStore() {
    return event_aggregator_->CopyLocalAggregateStore();
  }

  Status GenerateObservations(uint32_t final_day_index_utc, uint32_t final_day_index_local = 0u) {
    return event_aggregator_->GenerateObservationsNoWorker(final_day_index_utc,
                                                           final_day_index_local);
  }

  Status GarbageCollect(uint32_t day_index_utc, uint32_t day_index_local = 0u) {
    return event_aggregator_->GarbageCollect(day_index_utc, day_index_local);
  }

  void DoScheduledTasksNow() {
    // Steady values don't matter, just tell DoScheduledTasks to run everything.
    auto steady_time = std::chrono::steady_clock::now();
    event_aggregator_->next_generate_obs_ = steady_time;
    event_aggregator_->next_gc_ = steady_time;
    event_aggregator_->DoScheduledTasks(test_clock_->now(), steady_time);
  }

  // Clears the FakeObservationStore and resets the counts of Observations
  // received by the FakeObservationStore and the TestUpdateRecipient.
  void ResetObservationStore() {
    observation_store_->messages_received.clear();
    observation_store_->metadata_received.clear();
    observation_store_->ResetObservationCounter();
    update_recipient_->invocation_count = 0;
  }

  // Given a ProjectContext |project_context| and the MetricReportId of a
  // UNIQUE_N_DAY_ACTIVES report in |project_context|, as well as a day index
  // and an event code, logs an OccurrenceEvent to the EventAggregator for
  // that report, day index, and event code. If a non-null LoggedActivity map is
  // provided, updates the map with information about the logged Event.
  Status LogUniqueActivesEvent(const ProjectContext& project_context,
                               const MetricReportId& metric_report_id, uint32_t day_index,
                               uint32_t event_code, LoggedActivity* logged_activity = nullptr) {
    EventRecord event_record;
    event_record.metric = project_context.GetMetric(metric_report_id.first);
    event_record.event->set_day_index(day_index);
    event_record.event->mutable_occurrence_event()->set_event_code(event_code);
    auto status = event_aggregator_->LogUniqueActivesEvent(metric_report_id.second, &event_record);
    if (logged_activity == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(MakeAggregationKey(project_context, metric_report_id), &key)) {
      return kInvalidArguments;
    }
    (*logged_activity)[key][event_code].insert(day_index);
    return status;
  }

  // Given a ProjectContext |project_context| and the MetricReportId of an
  // EVENT_COUNT metric with a PER_DEVICE_NUMERIC_STATS report in
  // |project_context|, as well as a day index, a component string, and an event
  // code, logs a CountEvent to the EventAggregator for that report, day
  // index, component, and event code. If a non-null LoggedValues map is
  // provided, updates the map with information about the logged Event.
  Status LogPerDeviceCountEvent(const ProjectContext& project_context,
                                const MetricReportId& metric_report_id, uint32_t day_index,
                                const std::string& component, uint32_t event_code, int64_t count,
                                LoggedValues* logged_values = nullptr) {
    EventRecord event_record;
    event_record.metric = project_context.GetMetric(metric_report_id.first);
    event_record.event->set_day_index(day_index);
    auto count_event = event_record.event->mutable_count_event();
    count_event->set_component(component);
    count_event->add_event_code(event_code);
    count_event->set_count(count);
    auto status = event_aggregator_->LogCountEvent(metric_report_id.second, &event_record);
    if (logged_values == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(MakeAggregationKey(project_context, metric_report_id), &key)) {
      return kInvalidArguments;
    }
    (*logged_values)[key][component][event_code][day_index].push_back(count);
    return status;
  }

  // Given a ProjectContext |project_context| and the MetricReportId of an
  // ELAPSED_TIME metric with a PER_DEVICE_NUMERIC_STATS report in
  // |project_context|, as well as a day index, a component string, and an event
  // code, logs an ElapsedTimeEvent to the EventAggregator for that report, day
  // index, component, and event code. If a non-null LoggedValues map is
  // provided, updates the map with information about the logged Event.
  Status LogPerDeviceElapsedTimeEvent(const ProjectContext& project_context,
                                      const MetricReportId& metric_report_id, uint32_t day_index,
                                      const std::string& component, uint32_t event_code,
                                      int64_t micros, LoggedValues* logged_values = nullptr) {
    EventRecord event_record;
    event_record.metric = project_context.GetMetric(metric_report_id.first);
    event_record.event->set_day_index(day_index);
    auto elapsed_time_event = event_record.event->mutable_elapsed_time_event();
    elapsed_time_event->set_component(component);
    elapsed_time_event->add_event_code(event_code);
    elapsed_time_event->set_elapsed_micros(micros);
    auto status = event_aggregator_->LogElapsedTimeEvent(metric_report_id.second, &event_record);
    if (logged_values == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(MakeAggregationKey(project_context, metric_report_id), &key)) {
      return kInvalidArguments;
    }
    (*logged_values)[key][component][event_code][day_index].push_back(micros);
    return status;
  }

  // Given a ProjectContext |project_context| and the MetricReportId of a
  // FRAME_RATE metric with a PER_DEVICE_NUMERIC_STATS report in
  // |project_context|, as well as a day index, a component string, and an event
  // code, logs a FrameRateEvent to the EventAggregator for that report, day
  // index, component, and event code. If a non-null LoggedValues map is
  // provided, updates the map with information about the logged Event.
  Status LogPerDeviceFrameRateEvent(const ProjectContext& project_context,
                                    const MetricReportId& metric_report_id, uint32_t day_index,
                                    const std::string& component, uint32_t event_code, float fps,
                                    LoggedValues* logged_values = nullptr) {
    EventRecord event_record;
    event_record.metric = project_context.GetMetric(metric_report_id.first);
    event_record.event->set_day_index(day_index);
    auto frame_rate_event = event_record.event->mutable_frame_rate_event();
    frame_rate_event->set_component(component);
    frame_rate_event->add_event_code(event_code);
    int64_t frames_per_1000_seconds = std::round(fps * 1000.0);
    frame_rate_event->set_frames_per_1000_seconds(frames_per_1000_seconds);
    auto status = event_aggregator_->LogFrameRateEvent(metric_report_id.second, &event_record);
    if (logged_values == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(MakeAggregationKey(project_context, metric_report_id), &key)) {
      return kInvalidArguments;
    }
    (*logged_values)[key][component][event_code][day_index].push_back(frames_per_1000_seconds);
    return status;
  }

  // Given a ProjectContext |project_context| and the MetricReportId of a
  // MEMORY_USAGE metric with a PER_DEVICE_NUMERIC_STATS report in
  // |project_context|, as well as a day index, a component string, and an event
  // code, logs a MemoryUsageEvent to the EventAggregator for that report, day
  // index, component, and event code. If a non-null LoggedValues map is
  // provided, updates the map with information about the logged Event.
  Status LogPerDeviceMemoryUsageEvent(const ProjectContext& project_context,
                                      const MetricReportId& metric_report_id, uint32_t day_index,
                                      const std::string& component,
                                      const std::vector<uint32_t>& event_codes, int64_t bytes,
                                      LoggedValues* logged_values = nullptr) {
    EventRecord event_record;
    event_record.metric = project_context.GetMetric(metric_report_id.first);
    event_record.event->set_day_index(day_index);
    auto memory_usage_event = event_record.event->mutable_memory_usage_event();
    memory_usage_event->set_component(component);
    for (auto event_code : event_codes) {
      memory_usage_event->add_event_code(event_code);
    }
    memory_usage_event->set_bytes(bytes);
    auto status = event_aggregator_->LogMemoryUsageEvent(metric_report_id.second, &event_record);
    if (logged_values == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(MakeAggregationKey(project_context, metric_report_id), &key)) {
      return kInvalidArguments;
    }
    (*logged_values)[key][component][PackEventCodes(event_codes)][day_index].push_back(bytes);
    return status;
  }

  // Given a LoggedActivity map describing the events that have been logged
  // to the EventAggregator, checks whether the contents of the
  // LocalAggregateStore are as expected, accounting for any garbage
  // collection.
  //
  // logged_activity: a LoggedActivity representing event occurrences
  // since the LocalAggregateStore was created. All day indices should be
  // greater than or equal to |day_store_created_| and less than or equal to
  // |current_day_index|.
  //
  // current_day_index: The day index of the current day in the test's frame
  // of reference.
  bool CheckUniqueActivesAggregates(const LoggedActivity& logged_activity,
                                    uint32_t /*current_day_index*/) {
    auto local_aggregate_store = event_aggregator_->CopyLocalAggregateStore();
    // Check that the LocalAggregateStore contains no more UniqueActives
    // aggregates than |logged_activity| and |day_last_garbage_collected_|
    // should imply.
    for (const auto& report_pair : local_aggregate_store.by_report_key()) {
      const auto& aggregates = report_pair.second;
      if (aggregates.type_case() != ReportAggregates::kUniqueActivesAggregates) {
        continue;
      }
      const auto& report_key = report_pair.first;
      // Check whether this ReportAggregationKey is in |logged_activity|. If
      // not, expect that its by_event_code map is empty.
      auto report_activity = logged_activity.find(report_key);
      if (report_activity == logged_activity.end()) {
        EXPECT_TRUE(aggregates.unique_actives_aggregates().by_event_code().empty());
        if (!aggregates.unique_actives_aggregates().by_event_code().empty()) {
          return false;
        }
        break;
      }
      auto expected_events = report_activity->second;
      for (const auto& event_pair : aggregates.unique_actives_aggregates().by_event_code()) {
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
          EXPECT_GE(day_index, EarliestAllowedDayIndex(aggregates.aggregation_config()));
          if (day_index < EarliestAllowedDayIndex(aggregates.aggregation_config())) {
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
      // Check that this ReportAggregationKey is in the LocalAggregateStore, and
      // that the aggregates are of the expected type.
      auto report_aggregates = local_aggregate_store.by_report_key().find(logged_key);
      EXPECT_NE(report_aggregates, local_aggregate_store.by_report_key().end());
      if (report_aggregates == local_aggregate_store.by_report_key().end()) {
        return false;
      }
      if (report_aggregates->second.type_case() != ReportAggregates::kUniqueActivesAggregates) {
        return false;
      }
      // Compute the earliest day index that should appear among the aggregates
      // for this report.
      auto earliest_allowed =
          EarliestAllowedDayIndex(report_aggregates->second.aggregation_config());
      for (const auto& logged_event_pair : logged_event_map) {
        const auto& logged_event_code = logged_event_pair.first;
        const auto& logged_days = logged_event_pair.second;
        // Check whether this event code is in the LocalAggregateStore
        // under this ReportAggregationKey. If not, check that all day indices
        // for this event code are smaller than the day index of the earliest
        // allowed aggregate.
        auto event_code_aggregates =
            report_aggregates->second.unique_actives_aggregates().by_event_code().find(
                logged_event_code);
        if (event_code_aggregates ==
            report_aggregates->second.unique_actives_aggregates().by_event_code().end()) {
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
          auto day_aggregate = event_code_aggregates->second.by_day_index().find(logged_day_index);
          if (logged_day_index >= earliest_allowed) {
            EXPECT_NE(day_aggregate, event_code_aggregates->second.by_day_index().end());
            if (day_aggregate == event_code_aggregates->second.by_day_index().end()) {
              return false;
            }
            EXPECT_TRUE(day_aggregate->second.activity_daily_aggregate().activity_indicator());
            if (!day_aggregate->second.activity_daily_aggregate().activity_indicator()) {
              return false;
            }
          }
        }
      }
    }
    return true;
  }

  bool CheckPerDeviceNumericAggregates(const LoggedValues& logged_values,
                                       uint32_t /*current_day_index*/) {
    auto local_aggregate_store = event_aggregator_->CopyLocalAggregateStore();
    // Check that the LocalAggregateStore contains no more PerDeviceNumeric
    // aggregates than |logged_values| and |day_last_garbage_collected_| should
    // imply.
    for (const auto& report_pair : local_aggregate_store.by_report_key()) {
      const auto& aggregates = report_pair.second;
      if (aggregates.type_case() != ReportAggregates::kNumericAggregates) {
        continue;
      }
      const auto& report_key = report_pair.first;
      // Check whether this ReportAggregationKey is in |logged_values|. If not,
      // expect that its by_component map is empty.
      auto report_values = logged_values.find(report_key);
      if (report_values == logged_values.end()) {
        EXPECT_TRUE(aggregates.numeric_aggregates().by_component().empty());
        if (!aggregates.numeric_aggregates().by_component().empty()) {
          return false;
        }
        break;
      }
      auto expected_components = report_values->second;
      for (const auto& component_pair : aggregates.numeric_aggregates().by_component()) {
        // Check that this component is in |logged_values| under this
        // ReportAggregationKey.
        auto component = component_pair.first;
        auto component_values = expected_components.find(component);
        EXPECT_NE(component_values, expected_components.end());
        if (component_values == expected_components.end()) {
          return false;
        }
        const auto& expected_events = component_values->second;
        for (const auto& event_pair : component_pair.second.by_event_code()) {
          // Check that this event code is in |logged_values| under this
          // ReportAggregationKey and component.
          const auto& event_code = event_pair.first;
          auto event_values = expected_events.find(event_code);
          EXPECT_NE(event_values, expected_events.end());
          if (event_values == expected_events.end()) {
            return false;
          }
          const auto& expected_days = event_values->second;
          for (const auto& day_pair : event_pair.second.by_day_index()) {
            // Check that this day index is in |logged_values| under this
            // ReportAggregationKey, component, and event code.
            const auto& day_index = day_pair.first;
            auto day_value = expected_days.find(day_index);
            EXPECT_NE(day_value, expected_days.end());
            if (day_value == expected_days.end()) {
              return false;
            }
            // Check that the day index is no earlier than is implied by the
            // dates of store creation and garbage collection.
            EXPECT_GE(day_index, EarliestAllowedDayIndex(aggregates.aggregation_config()));
            if (day_index < EarliestAllowedDayIndex(aggregates.aggregation_config())) {
              return false;
            }
          }
        }
      }
    }

    // Check that the LocalAggregateStore contains aggregates for all values in
    // |logged_values|, as long as they are recent enough to have survived any
    // garbage collection.
    for (const auto& logged_pair : logged_values) {
      const auto& logged_key = logged_pair.first;
      const auto& logged_component_map = logged_pair.second;
      // Check that this ReportAggregationKey is in the LocalAggregateStore, and
      // that the aggregates are of the expected type.
      auto report_aggregates = local_aggregate_store.by_report_key().find(logged_key);
      EXPECT_NE(report_aggregates, local_aggregate_store.by_report_key().end());
      if (report_aggregates == local_aggregate_store.by_report_key().end()) {
        return false;
      }
      if (report_aggregates->second.type_case() != ReportAggregates::kNumericAggregates) {
        return false;
      }
      const auto& aggregation_type =
          report_aggregates->second.aggregation_config().report().aggregation_type();
      // Compute the earliest day index that should appear among the aggregates
      // for this report.
      auto earliest_allowed =
          EarliestAllowedDayIndex(report_aggregates->second.aggregation_config());
      for (const auto& logged_component_pair : logged_component_map) {
        const auto& logged_component = logged_component_pair.first;
        const auto& logged_event_code_map = logged_component_pair.second;
        // Check whether this component is in the LocalAggregateStore under this
        // ReportAggregationKey. If not, check that all day indices for all
        // entries in |logged_values| under this component are smaller than the
        // day index of the earliest allowed aggregate.
        bool component_found = false;
        auto component_aggregates =
            report_aggregates->second.numeric_aggregates().by_component().find(logged_component);
        if (component_aggregates !=
            report_aggregates->second.numeric_aggregates().by_component().end()) {
          component_found = true;
        }
        for (const auto& logged_event_pair : logged_event_code_map) {
          const auto& logged_event_code = logged_event_pair.first;
          const auto& logged_day_map = logged_event_pair.second;
          // Check whether this event code is in the LocalAggregateStore under
          // this ReportAggregationKey. If not, check that all day indices in
          // |logged_values| under this component are smaller than the day index
          // of the earliest allowed aggregate.
          bool event_code_found = false;
          if (component_found) {
            auto event_code_aggregates =
                component_aggregates->second.by_event_code().find(logged_event_code);
            if (event_code_aggregates != component_aggregates->second.by_event_code().end()) {
              event_code_found = true;
            }
            if (event_code_found) {
              // Check that all of the day indices in |logged_values| under this
              // ReportAggregationKey, component, and event code are in the
              // LocalAggregateStore, as long as they are recent enough to have
              // survived any garbage collection. Check that each aggregate has
              // the expected value.
              for (const auto& logged_day_pair : logged_day_map) {
                auto logged_day_index = logged_day_pair.first;
                auto logged_values = logged_day_pair.second;
                auto day_aggregate =
                    event_code_aggregates->second.by_day_index().find(logged_day_index);
                if (logged_day_index >= earliest_allowed) {
                  EXPECT_NE(day_aggregate, event_code_aggregates->second.by_day_index().end());
                  if (day_aggregate == event_code_aggregates->second.by_day_index().end()) {
                    return false;
                  }
                  int64_t aggregate_from_logged_values = 0;
                  for (size_t index = 0; index < logged_values.size(); index++) {
                    switch (aggregation_type) {
                      case ReportDefinition::SUM:
                        aggregate_from_logged_values += logged_values[index];
                        break;
                      case ReportDefinition::MAX:
                        aggregate_from_logged_values =
                            std::max(aggregate_from_logged_values, logged_values[index]);
                        break;
                      case ReportDefinition::MIN:
                        if (index == 0) {
                          aggregate_from_logged_values = logged_values[0];
                        }
                        aggregate_from_logged_values =
                            std::min(aggregate_from_logged_values, logged_values[index]);
                        break;
                      default:
                        return false;
                    }
                  }
                  EXPECT_EQ(day_aggregate->second.numeric_daily_aggregate().value(),
                            aggregate_from_logged_values);
                  if (day_aggregate->second.numeric_daily_aggregate().value() !=
                      aggregate_from_logged_values) {
                    return false;
                  }
                }
              }
            }
          }
          if (!component_found | !event_code_found) {
            for (const auto& logged_day_pair : logged_day_map) {
              auto logged_day_index = logged_day_pair.first;
              EXPECT_LT(logged_day_index, earliest_allowed);
              if (logged_day_index >= earliest_allowed) {
                return false;
              }
            }
            break;
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
    EXPECT_GE(day_store_created_, backfill_days)
        << "The day index of store creation must be larger than the number "
           "of backfill days.";
    if (day_last_garbage_collected_ == 0u) {
      return day_store_created_ - backfill_days;
    }

    // Otherwise, it is the later of:
    // (a) The day index on which the store was created minus the number
    // of backfill days.
    // (b) The day index for which the store was last garbage-collected
    // minus the number of backfill days, minus the largest window size in
    // the report associated to |config|, plus 1.
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
      return day_store_created_ - backfill_days;
    }
    return (day_store_created_ < (day_last_garbage_collected_ - max_window_size + 1))
               ? (day_last_garbage_collected_ - backfill_days - max_window_size + 1)
               : day_store_created_ - backfill_days;
  }

  std::unique_ptr<EventAggregator> event_aggregator_;
  std::unique_ptr<MockConsistentProtoStore> local_aggregate_proto_store_;
  std::unique_ptr<MockConsistentProtoStore> obs_history_proto_store_;
  std::unique_ptr<ObservationWriter> observation_writer_;
  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;
  std::unique_ptr<TestUpdateRecipient> update_recipient_;
  std::unique_ptr<FakeObservationStore> observation_store_;
  std::unique_ptr<IncrementingClock> test_clock_;
  // The day index on which the LocalAggregateStore was last
  // garbage-collected. A value of 0 indicates that the store has never been
  // garbage-collected.
  uint32_t day_last_garbage_collected_ = 0u;
  // The day index on which the LocalAggregateStore was created.
  uint32_t day_store_created_ = 0u;

 private:
  std::unique_ptr<SystemDataInterface> system_data_;
};  // namespace logger

// Creates an EventAggregator and provides it with a ProjectContext generated
// from a registry.
class EventAggregatorTestWithProjectContext : public EventAggregatorTest {
 protected:
  explicit EventAggregatorTestWithProjectContext(const std::string& registry_var_name) {
    project_context_ = GetTestProject(registry_var_name);
  }

  void SetUp() override {
    EventAggregatorTest::SetUp();
    event_aggregator_->UpdateAggregationConfigs(*project_context_);
  }

  // Logs an OccurrenceEvent for the MetricReportId of a locally
  // aggregated report of the ProjectContext. Overrides the method
  // EventAggregatorTest::LogUniqueActivesEvent.
  Status LogUniqueActivesEvent(const MetricReportId& metric_report_id, uint32_t day_index,
                               uint32_t event_code, LoggedActivity* logged_activity = nullptr) {
    return EventAggregatorTest::LogUniqueActivesEvent(*project_context_, metric_report_id,
                                                      day_index, event_code, logged_activity);
  }

  // Logs a CountEvent for the MetricReportId of a locally
  // aggregated report of the ProjectContext. Overrides the method
  // EventAggregatorTest::LogPerDeviceCountEvent.
  Status LogPerDeviceCountEvent(const MetricReportId& metric_report_id, uint32_t day_index,
                                const std::string& component, uint32_t event_code, int64_t count,
                                LoggedValues* logged_values = nullptr) {
    return EventAggregatorTest::LogPerDeviceCountEvent(*project_context_, metric_report_id,
                                                       day_index, component, event_code, count,
                                                       logged_values);
  }

  // Logs an ElapsedTimeEvent for the MetricReportId of a locally
  // aggregated report of the ProjectContext. Overrides the method
  // EventAggregatorTest::LogPerDeviceElapsedTimeEvent.
  Status LogPerDeviceElapsedTimeEvent(const MetricReportId& metric_report_id, uint32_t day_index,
                                      const std::string& component, uint32_t event_code,
                                      int64_t micros, LoggedValues* logged_values = nullptr) {
    return EventAggregatorTest::LogPerDeviceElapsedTimeEvent(*project_context_, metric_report_id,
                                                             day_index, component, event_code,
                                                             micros, logged_values);
  }

  // Logs a FrameRateEvent for the MetricReportId of a locally
  // aggregated report of the ProjectContext. Overrides the method
  // EventAggregatorTest::LogPerDeviceFrameRateEvent.
  Status LogPerDeviceFrameRateEvent(const MetricReportId& metric_report_id, uint32_t day_index,
                                    const std::string& component, uint32_t event_code, float fps,
                                    LoggedValues* logged_values = nullptr) {
    return EventAggregatorTest::LogPerDeviceFrameRateEvent(
        *project_context_, metric_report_id, day_index, component, event_code, fps, logged_values);
  }

  // Logs a MemoryUsageEvent for the MetricReportId of a locally
  // aggregated report of the ProjectContext. Overrides the method
  // EventAggregatorTest::LogPerDeviceMemoryUsageEvent.
  Status LogPerDeviceMemoryUsageEvent(const MetricReportId& metric_report_id, uint32_t day_index,
                                      const std::string& component,
                                      const std::vector<uint32_t>& event_codes, int64_t bytes,
                                      LoggedValues* logged_values = nullptr) {
    return EventAggregatorTest::LogPerDeviceMemoryUsageEvent(*project_context_, metric_report_id,
                                                             day_index, component, event_codes,
                                                             bytes, logged_values);
  }

 private:
  // A ProjectContext wrapping the MetricDefinitions passed to the
  // constructor in |metric_string|.
  std::unique_ptr<ProjectContext> project_context_;
};

// Creates an EventAggregator and provides it with a ProjectContext generated
// from test_registries/unique_actives_test_registry.yaml. All metrics in this
// registry are of type EVENT_OCCURRED and have a UNIQUE_N_DAY_ACTIVES report.
class UniqueActivesEventAggregatorTest : public EventAggregatorTestWithProjectContext {
 protected:
  UniqueActivesEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(testing::unique_actives::kCobaltRegistryBase64) {}
};

// Creates an EventAggregator and provides it with a ProjectContext generated
// from test_registries/unique_actives_noise_free_test_registry.yaml. All
// metrics in this registry are of type EVENT_OCCURRED and have a
// UNIQUE_N_DAY_ACTIVES report with local_privacy_noise_level NONE.
class UniqueActivesNoiseFreeEventAggregatorTest : public EventAggregatorTestWithProjectContext {
 protected:
  UniqueActivesNoiseFreeEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(
            testing::unique_actives_noise_free::kCobaltRegistryBase64) {}
};

// Creates an EventAggregator and provides it with a ProjectContext generated
// from test_registries/per_device_numeric_stats_test_registry.yaml. All metrics
// in this registry are of type EVENT_COUNT and have a PER_DEVICE_NUMERIC_STATS
// report.
class PerDeviceNumericEventAggregatorTest : public EventAggregatorTestWithProjectContext {
 protected:
  PerDeviceNumericEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(
            testing::per_device_numeric_stats::kCobaltRegistryBase64) {}
};

// Creates an EventAggregator and provides it with a ProjectContext generated
// from test_registries/mixed_time_zone_test_registry.yaml. This registry
// contains multiple MetricDefinitions with different time zone policies.
class NoiseFreeMixedTimeZoneEventAggregatorTest : public EventAggregatorTestWithProjectContext {
 protected:
  NoiseFreeMixedTimeZoneEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(testing::mixed_time_zone::kCobaltRegistryBase64) {}
};

class EventAggregatorWorkerTest : public EventAggregatorTest {
 protected:
  void SetUp() override { EventAggregatorTest::SetUp(); }

  void ShutDownWorkerThread() { event_aggregator_->ShutDown(); }

  bool in_shutdown_state() { return (shutdown_flag_set() && !worker_joinable()); }

  bool in_run_state() { return (!shutdown_flag_set() && worker_joinable()); }

  bool shutdown_flag_set() {
    return event_aggregator_->protected_shutdown_flag_.const_lock()->shut_down;
  }

  bool worker_joinable() { return event_aggregator_->worker_thread_.joinable(); }
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
// EventAggregator::UpdateAggregationConfigs is called with a ProjectContext
// containing at least one report for each locally aggregated report type.
TEST_F(EventAggregatorTest, UpdateAggregationConfigs) {
  // Check that the LocalAggregateStore is empty.
  EXPECT_EQ(0u, CopyLocalAggregateStore().by_report_key().size());
  // Provide the unique_actives test registry to the EventAggregator.
  auto unique_actives_project_context =
      GetTestProject(testing::unique_actives::kCobaltRegistryBase64);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*unique_actives_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in the unique_actives
  // test registry.
  EXPECT_EQ(testing::unique_actives::kExpectedAggregationParams.metric_report_ids.size(),
            CopyLocalAggregateStore().by_report_key().size());
  // Check that the LocalAggregateStore contains the expected
  // ReportAggregationKey and AggregationConfig for each locally aggregated
  // report in the unique_actives registry.
  for (const auto& metric_report_id :
       testing::unique_actives::kExpectedAggregationParams.metric_report_ids) {
    std::string key;
    SerializeToBase64(MakeAggregationKey(*unique_actives_project_context, metric_report_id), &key);
    auto config = MakeAggregationConfig(*unique_actives_project_context, metric_report_id);
    LocalAggregateStore local_aggregate_store = CopyLocalAggregateStore();
    auto report_aggregates = local_aggregate_store.by_report_key().find(key);
    EXPECT_NE(local_aggregate_store.by_report_key().end(), report_aggregates);
    EXPECT_EQ(SerializeAsStringDeterministic(config),
              SerializeAsStringDeterministic(report_aggregates->second.aggregation_config()));
  }
}

// Tests two assumptions about the behavior of
// EventAggregator::UpdateAggregationConfigs when two projects with the same
// customer ID and project ID provide configurations to the EventAggregator.
// These assumptions are:
// (1) If the second project provides a report with a
// ReportAggregationKey which was not provided by the first project, then
// the EventAggregator accepts the new report.
// (2) If a report provided by the second project has a ReportAggregationKey
// which was already provided by the first project, then the EventAggregator
// rejects the new report, even if its ReportDefinition differs from that of
// existing report with the same ReportAggregationKey.
TEST_F(EventAggregatorTest, UpdateAggregationConfigsWithSameKey) {
  // Provide the unique_actives test registry to the EventAggregator.
  auto unique_actives_project_context =
      GetTestProject(testing::unique_actives::kCobaltRegistryBase64);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*unique_actives_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in the unique_actives
  // test registry.
  EXPECT_EQ(testing::unique_actives::kExpectedAggregationParams.metric_report_ids.size(),
            CopyLocalAggregateStore().by_report_key().size());
  // Provide the unique_actives_noise_free test registry to the EventAggregator.
  auto unique_actives_noise_free_project_context =
      GetTestProject(testing::unique_actives_noise_free::kCobaltRegistryBase64);
  EXPECT_EQ(
      kOK, event_aggregator_->UpdateAggregationConfigs(*unique_actives_noise_free_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of distinct MetricReportIds of locally
  // aggregated reports in the union of the unique_actives and
  // unique_actives_noise_free registries.
  auto local_aggregate_store = CopyLocalAggregateStore();
  EXPECT_EQ(4u, local_aggregate_store.by_report_key().size());
  // The MetricReportId |kFeaturesActiveMetricReportId| appears in both
  // registries. The associated ReportAggregationKeys are identical, but the
  // FeaturesActive_UniqueDevices reports in the two registries have different
  // sets of window sizes, so their AggregationConfigs are different.
  //
  // Check that the AggregationConfig stored in the LocalAggregateStore
  // under the key associated to |kFeaturesActiveMetricReportId| is the
  // first AggregationConfig that was provided for that key; i.e., is
  // derived from the unique_actives test registry.
  std::string key;
  EXPECT_TRUE(
      SerializeToBase64(MakeAggregationKey(*unique_actives_project_context,
                                           testing::unique_actives::kFeaturesActiveMetricReportId),
                        &key));
  auto unique_actives_config = MakeAggregationConfig(
      *unique_actives_project_context, testing::unique_actives::kFeaturesActiveMetricReportId);
  auto report_aggregates = local_aggregate_store.by_report_key().find(key);
  EXPECT_NE(local_aggregate_store.by_report_key().end(), report_aggregates);
  EXPECT_EQ(SerializeAsStringDeterministic(unique_actives_config),
            SerializeAsStringDeterministic(report_aggregates->second.aggregation_config()));
  auto noise_free_config =
      MakeAggregationConfig(*unique_actives_noise_free_project_context,
                            testing::unique_actives_noise_free::kFeaturesActiveMetricReportId);
  EXPECT_NE(SerializeAsStringDeterministic(noise_free_config),
            SerializeAsStringDeterministic(report_aggregates->second.aggregation_config()));
}

// Tests that EventAggregator::Log*Event returns |kInvalidArguments| when
// passed a report ID which is not associated to a key of the
// LocalAggregateStore, or when passed an EventRecord containing an Event
// proto message which is not of the appropriate event type.
TEST_F(EventAggregatorTest, LogBadEvents) {
  // Provide the unique_actives test registry to the EventAggregator.
  auto unique_actives_project_context =
      GetTestProject(testing::unique_actives::kCobaltRegistryBase64);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*unique_actives_project_context));
  // Attempt to log a UniqueActivesEvent for
  // |kEventsOccurredMetricReportId|, which is not in the unique_actives
  // registry. Check that the result is |kInvalidArguments|.
  auto noise_free_project_context =
      GetTestProject(testing::unique_actives_noise_free::kCobaltRegistryBase64);
  EventRecord bad_event_record;
  bad_event_record.metric = noise_free_project_context->GetMetric(
      testing::unique_actives_noise_free::kEventsOccurredMetricId);
  bad_event_record.event->set_day_index(CurrentDayIndex());
  bad_event_record.event->mutable_occurrence_event()->set_event_code(0u);
  EXPECT_EQ(kInvalidArguments,
            event_aggregator_->LogUniqueActivesEvent(
                testing::unique_actives_noise_free::kEventsOccurredUniqueDevicesReportId,
                &bad_event_record));
  // Attempt to call LogUniqueActivesEvent() with a valid metric and report
  // ID, but with an EventRecord wrapping an Event which is not an
  // OccurrenceEvent. Check that the result is |kInvalidArguments|.
  bad_event_record.metric =
      unique_actives_project_context->GetMetric(testing::unique_actives::kFeaturesActiveMetricId);
  bad_event_record.event->mutable_count_event();
  EXPECT_EQ(kInvalidArguments,
            event_aggregator_->LogUniqueActivesEvent(
                testing::unique_actives::kFeaturesActiveUniqueDevicesReportId, &bad_event_record));
  // Attempt to call LogPerDeviceCountEvent() with a valid metric and report
  // ID, but with an EventRecord wrapping an Event which is not a
  // CountEvent. Check that the result is |kInvalidArguments|.
  bad_event_record.metric = noise_free_project_context->GetMetric(
      testing::per_device_numeric_stats::kConnectionFailuresMetricReportId.first);
  bad_event_record.event->mutable_occurrence_event();
  EXPECT_EQ(kInvalidArguments,
            event_aggregator_->LogCountEvent(
                testing::per_device_numeric_stats::kConnectionFailuresPerDeviceCountReportId,
                &bad_event_record));
}

// Tests that EventAggregator::GenerateObservations() returns a positive
// status and that the expected number of Observations is generated when no
// Events have been logged to the EventAggregator.
TEST_F(EventAggregatorTest, GenerateObservationsNoEvents) {
  // Provide the all_report_types test registry to the EventAggregator.
  auto project_context = GetTestProject(testing::all_report_types::kCobaltRegistryBase64);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*project_context));
  // Generate locally aggregated Observations for the current day index.
  EXPECT_EQ(kOK, GenerateObservations(CurrentDayIndex()));
  std::vector<Observation2> observations(0);
  EXPECT_TRUE(FetchAggregatedObservations(&observations,
                                          testing::all_report_types::kExpectedAggregationParams,
                                          observation_store_.get(), update_recipient_.get()));
}

// Tests that EventAggregator::GenerateObservations() only generates
// Observations the first time it is called for a given day index.
TEST_F(EventAggregatorTest, GenerateObservationsTwice) {
  // Provide the all_report_types test registry to the EventAggregator.
  auto project_context = GetTestProject(testing::all_report_types::kCobaltRegistryBase64);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*project_context));
  // Check that Observations are generated when GenerateObservations is called
  // for the current day index for the first time.
  auto current_day_index = CurrentDayIndex();
  EXPECT_EQ(kOK, GenerateObservations(current_day_index));
  std::vector<Observation2> observations(0);
  EXPECT_TRUE(FetchAggregatedObservations(&observations,
                                          testing::all_report_types::kExpectedAggregationParams,
                                          observation_store_.get(), update_recipient_.get()));
  // Check that no Observations are generated when GenerateObservations is
  // called for the currentday index for the second time.
  ResetObservationStore();
  EXPECT_EQ(kOK, GenerateObservations(current_day_index));
  EXPECT_EQ(0u, observation_store_->messages_received.size());
}

// When the LocalAggregateStore contains one ReportAggregates proto and that
// proto is empty, GenerateObservations should return success but generate no
// observations.
TEST_F(EventAggregatorTest, GenerateObservationsFromBadStore) {
  auto bad_store = std::make_unique<LocalAggregateStore>();
  (*bad_store->mutable_by_report_key())["some_key"] = ReportAggregates();
  local_aggregate_proto_store_->set_stored_proto(std::move(bad_store));
  // Read the bad store in to the EventAggregator.
  ResetEventAggregator();
  EXPECT_EQ(kOK, GenerateObservations(CurrentDayIndex()));
  EXPECT_EQ(0u, observation_store_->messages_received.size());
}

// When the LocalAggregateStore contains one empty ReportAggregates proto and
// some valid ReportAggregates, GenerateObservations should produce observations
// for the valid ReportAggregates.
TEST_F(EventAggregatorTest, GenerateObservationsFromBadStoreMultiReport) {
  auto bad_store = std::make_unique<LocalAggregateStore>();
  (*bad_store->mutable_by_report_key())["some_key"] = ReportAggregates();
  local_aggregate_proto_store_->set_stored_proto(std::move(bad_store));
  // Read the bad store in to the EventAggregator.
  ResetEventAggregator();
  // Provide the all_report_types test registry to the EventAggregator.
  auto project_context = GetTestProject(testing::all_report_types::kCobaltRegistryBase64);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*project_context));
  EXPECT_EQ(kOK, GenerateObservations(CurrentDayIndex()));
  std::vector<Observation2> observations(0);
  EXPECT_TRUE(FetchAggregatedObservations(&observations,
                                          testing::all_report_types::kExpectedAggregationParams,
                                          observation_store_.get(), update_recipient_.get()));
}

// When the LocalAggregateStore contains one ReportAggregates proto and that
// proto is empty, GarbageCollect should return success.
TEST_F(EventAggregatorTest, GarbageCollectBadStore) {
  auto bad_store = std::make_unique<LocalAggregateStore>();
  (*bad_store->mutable_by_report_key())["some_key"] = ReportAggregates();
  local_aggregate_proto_store_->set_stored_proto(std::move(bad_store));
  // Read the bad store in to the EventAggregator.
  ResetEventAggregator();
  EXPECT_EQ(kOK, GarbageCollect(CurrentDayIndex()));
}

// Tests that the LocalAggregateStore is updated as expected when
// EventAggregator::LogUniqueActivesEvent() is called with valid arguments;
// i.e., with a report ID associated to an existing key of the
// LocalAggregateStore, and with an EventRecord which wraps an
// OccurrenceEvent.
//
// Logs some valid events each day for 35 days, checking the contents of the
// LocalAggregateStore each day.
TEST_F(UniqueActivesEventAggregatorTest, LogEvents) {
  LoggedActivity logged_activity;
  uint32_t num_days = 35;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    // Log an event for the FeaturesActive_UniqueDevices report with event code
    // 0. Check the contents of the LocalAggregateStore.
    auto day_index = CurrentDayIndex();
    EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kFeaturesActiveMetricReportId,
                                         day_index, 0u, &logged_activity));
    EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, day_index));
    // Log another event for the same report, event code, and day index.
    // Check the contents of the LocalAggregateStore.
    EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kFeaturesActiveMetricReportId,
                                         day_index, 0u, &logged_activity));
    EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, day_index));
    // Log several more events for various valid reports and event codes.
    // Check the contents of the LocalAggregateStore.
    EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kDeviceBootsMetricReportId,
                                         day_index, 0u, &logged_activity));
    EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kFeaturesActiveMetricReportId,
                                         day_index, 4u, &logged_activity));
    EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kNetworkActivityMetricReportId,
                                         day_index, 1u, &logged_activity));
    EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, day_index));
    AdvanceClock(kDay);
  }
}

// Tests GarbageCollect() for UniqueActivesReportAggregates.
//
// For each value of N in the range [0, 34], logs some UniqueActivesEvents
// each day for N consecutive days and then garbage-collects the
// LocalAggregateStore. After garbage collection, verifies the contents of
// the LocalAggregateStore.
TEST_F(UniqueActivesEventAggregatorTest, GarbageCollect) {
  uint32_t max_days_before_gc = 35;
  for (uint32_t days_before_gc = 0; days_before_gc < max_days_before_gc; days_before_gc++) {
    SetUp();
    day_last_garbage_collected_ = 0u;
    LoggedActivity logged_activity;
    for (uint32_t offset = 0; offset < days_before_gc; offset++) {
      auto day_index = CurrentDayIndex();
      for (const auto& metric_report_id :
           testing::unique_actives::kExpectedAggregationParams.metric_report_ids) {
        // Log 2 events with event code 0.
        EXPECT_EQ(kOK, LogUniqueActivesEvent(metric_report_id, day_index, 0u, &logged_activity));
        EXPECT_EQ(kOK, LogUniqueActivesEvent(metric_report_id, day_index, 0u, &logged_activity));
        if (offset < 3) {
          // Log 1 event with event code 1.
          EXPECT_EQ(kOK, LogUniqueActivesEvent(metric_report_id, day_index, 1u, &logged_activity));
        }
      }
      AdvanceClock(kDay);
    }
    auto end_day_index = CurrentDayIndex();
    EXPECT_EQ(kOK, GarbageCollect(end_day_index));
    day_last_garbage_collected_ = end_day_index;
    EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, end_day_index));
    TearDown();
  }
}

// Tests that EventAggregator::GenerateObservations() returns a positive
// status and that the expected number of Observations is generated after
// some UniqueActivesEvents have been logged, without any garbage
// collection.
//
// For 35 days, logs 2 events each day for the NetworkActivity_UniqueDevices
// report and 2 events for the FeaturesActive_UniqueDevices report, all
// with event code 0.
//
// Each day, calls GenerateObservations() with the day index of the previous
// day. Checks that a positive status is returned and that the
// FakeObservationStore has received the expected number of new observations
// for each locally aggregated report ID in the unique_actives registry.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservations) {
  int num_days = 35;
  std::vector<Observation2> observations(0);
  for (int offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    observations.clear();
    ResetObservationStore();
    EXPECT_EQ(kOK, GenerateObservations(day_index - 1));
    EXPECT_TRUE(FetchAggregatedObservations(&observations,
                                            testing::unique_actives::kExpectedAggregationParams,
                                            observation_store_.get(), update_recipient_.get()));
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kNetworkActivityMetricReportId,
                                           day_index, 0u));
      EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kFeaturesActiveMetricReportId,
                                           day_index, 0u));
    }
    AdvanceClock(kDay);
  }
  observations.clear();
  ResetObservationStore();
  EXPECT_EQ(kOK, GenerateObservations(CurrentDayIndex() - 1));
  EXPECT_TRUE(FetchAggregatedObservations(&observations,
                                          testing::unique_actives::kExpectedAggregationParams,
                                          observation_store_.get(), update_recipient_.get()));
}

// Tests that GenerateObservations() returns a positive status and that the
// expected number of Observations is generated each day when Events are
// logged for UNIQUE_N_DAY_ACTIVES reports over multiple days, and when the
// LocalAggregateStore is garbage-collected each day.
//
// For 35 days, logs 2 events each day for the NetworkActivity_UniqueDevices
// report and 2 events for the FeaturesActive_UniqueDevices report, all
// with event code 0.
//
// Each day following the first day, calls GenerateObservations() and then
// GarbageCollect() with the day index of the current day. Checks that
// positive statuses are returned and that the FakeObservationStore has
// received the expected number of new observations for each locally
// aggregated report ID in the unique_actives registry.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservationsWithGc) {
  int num_days = 35;
  std::vector<Observation2> observations(0);
  for (int offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    observations.clear();
    ResetObservationStore();
    EXPECT_EQ(kOK, GenerateObservations(day_index - 1));
    EXPECT_TRUE(FetchAggregatedObservations(&observations,
                                            testing::unique_actives::kExpectedAggregationParams,
                                            observation_store_.get(), update_recipient_.get()));
    EXPECT_EQ(kOK, GarbageCollect(day_index));
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kNetworkActivityMetricReportId,
                                           day_index, 0u));
      EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kFeaturesActiveMetricReportId,
                                           day_index, 0u));
    }
    AdvanceClock(kDay);
  }
  observations.clear();
  ResetObservationStore();
  auto day_index = CurrentDayIndex();
  EXPECT_EQ(kOK, GenerateObservations(day_index - 1));
  EXPECT_TRUE(FetchAggregatedObservations(&observations,
                                          testing::unique_actives::kExpectedAggregationParams,
                                          observation_store_.get(), update_recipient_.get()));
  EXPECT_EQ(kOK, GarbageCollect(day_index));
}

// Tests that GenerateObservations() returns a positive status and that the
// expected number of Observations is generated when events are logged over
// multiple days and some of those days' Observations are backfilled, without
// any garbage collection of the LocalAggregateStore.
//
// Sets the |backfill_days_| field of the EventAggregator to 3.
//
// Logging pattern:
// For 35 days, logs 2 events each day for the
// NetworkActivity_UniqueDevices report and 2 events for the
// FeaturesActive_UniqueDevices report, all with event code 0.
//
// Observation generation pattern:
// Calls GenerateObservations() on the 1st through 5th and the 7th out of
// every 10 days, for 35 days.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on
// the first day of every 10 (the day index for which GenerateObservations()
// was called, plus 3 days of backfill), that 1 day's worth of Observations
// are generated on the 2nd through 5th day of every 10, that 2 days'
// worth of Observations are generated on the 7th day of every 10 (the
// day index for which GenerateObservations() was called, plus 1 day of
// backfill), and that no Observations are generated on the remaining days.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservationsWithBackfill) {
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log 2 events each day for 35 days. Call GenerateObservations() on the
  // first 5 day indices, and the 7th, out of every 10.
  for (int offset = 0; offset < 35; offset++) {
    auto day_index = CurrentDayIndex();
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kNetworkActivityMetricReportId,
                                           day_index, 0u));
      EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kFeaturesActiveMetricReportId,
                                           day_index, 0u));
    }
    observation_store_->ResetObservationCounter();
    if (offset % 10 < 5 || offset % 10 == 6) {
      EXPECT_EQ(kOK, GenerateObservations(day_index));
    }
    auto num_new_obs = observation_store_->num_observations_added();
    EXPECT_GE(num_new_obs, 0u);
    // Check that the expected daily number of Observations was generated.
    switch (offset % 10) {
      case 0:
        EXPECT_EQ(
            testing::unique_actives::kExpectedAggregationParams.daily_num_obs * (backfill_days + 1),
            num_new_obs);
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        EXPECT_EQ(testing::unique_actives::kExpectedAggregationParams.daily_num_obs, num_new_obs);
        break;
      case 6:
        EXPECT_EQ(testing::unique_actives::kExpectedAggregationParams.daily_num_obs * 2,
                  num_new_obs);
        break;
      default:
        EXPECT_EQ(0u, num_new_obs);
    }
    AdvanceClock(kDay);
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
// For 35 days, logs 2 events each day for the
// NetworkActivity_UniqueDevices report and 2 events for the
// FeaturesActive_Unique_Devices report, all with event code 0.
//
// Observation generation pattern:
// Calls GenerateObservations() on the 1st through 5th and the 7th out of
// every 10 days, for 35 days. Garbage-collects the LocalAggregateStore after
// each call.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on
// the first day of every 10 (the day index for which GenerateObservations()
// was called, plus 3 days of backfill), that 1 day's worth of Observations
// are generated on the 2nd through 5th day of every 10, that 2 days'
// worth of Observations are generated on the 7th day of every 10 (the
// day index for which GenerateObservations() was called, plus 1 day of
// backfill), and that no Observations are generated on the remaining days.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservationsWithBackfillAndGc) {
  int num_days = 35;
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log 2 events each day for 35 days. Call GenerateObservations() on the
  // first 5 day indices, and the 7th, out of every 10.
  for (int offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kNetworkActivityMetricReportId,
                                           day_index, 0u));
      EXPECT_EQ(kOK, LogUniqueActivesEvent(testing::unique_actives::kFeaturesActiveMetricReportId,
                                           day_index, 0u));
    }
    observation_store_->ResetObservationCounter();
    if (offset % 10 < 5 || offset % 10 == 6) {
      EXPECT_EQ(kOK, GenerateObservations(day_index));
      EXPECT_EQ(kOK, GarbageCollect(day_index));
    }
    auto num_new_obs = observation_store_->num_observations_added();
    EXPECT_GE(num_new_obs, 0u);
    // Check that the expected daily number of Observations was generated.
    // This expected number is some multiple of the daily_num_obs field of
    // |kUniqueActivesExpectedParams|, depending on the number of days which
    // should have been backfilled when GenerateObservations() was called.
    switch (offset % 10) {
      case 0:
        EXPECT_EQ(
            testing::unique_actives::kExpectedAggregationParams.daily_num_obs * (backfill_days + 1),
            num_new_obs);
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        EXPECT_EQ(testing::unique_actives::kExpectedAggregationParams.daily_num_obs, num_new_obs);
        break;
      case 6:
        EXPECT_EQ(testing::unique_actives::kExpectedAggregationParams.daily_num_obs * 2,
                  num_new_obs);
        break;
      default:
        EXPECT_EQ(0u, num_new_obs);
    }
    AdvanceClock(kDay);
  }
}

// Checks that UniqueActivesObservations with the expected values (i.e.,
// non-active for all UNIQUE_N_DAY_ACTIVES reports, for all window sizes and
// event codes) are generated when no Events have been logged to the
// EventAggregator.
TEST_F(UniqueActivesNoiseFreeEventAggregatorTest, CheckObservationValuesNoEvents) {
  auto current_day_index = CurrentDayIndex();
  EXPECT_EQ(kOK, GenerateObservations(current_day_index));
  auto expected_obs = MakeNullExpectedUniqueActivesObservations(
      testing::unique_actives_noise_free::kExpectedAggregationParams, current_day_index);
  EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                             update_recipient_.get()));
}

// Checks that UniqueActivesObservations with the expected values are
// generated when GenerateObservations() is called for a single day index
// after logging some events for UNIQUE_N_DAY_ACTIVES reports for that day
// index, without any garbage collection or backfill.
//
// Logging pattern:
// Logs 2 occurrences of event code 0 for the FeaturesActives_UniqueDevices
// report, and 1 occurrence of event code 1 for the
// EventsOccurred_UniqueDevices report, all on the same day.
//
// Observation generation pattern:
// Calls GenerateObservations() after logging all events.
//
// Expected numbers of Observations:
// The expected number of Observations is the daily_num_obs field of
// |testing::unique_actives_noise_free::kExpectedAggregationParams|.
//
// Expected Observation values:
// All Observations should be labeled with the day index on which the events
// were logged.
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
TEST_F(UniqueActivesNoiseFreeEventAggregatorTest, CheckObservationValuesSingleDay) {
  auto day_index = CurrentDayIndex();
  // Log several events on |day_index|.
  EXPECT_EQ(kOK,
            LogUniqueActivesEvent(testing::unique_actives_noise_free::kFeaturesActiveMetricReportId,
                                  day_index, 0u));
  EXPECT_EQ(kOK,
            LogUniqueActivesEvent(testing::unique_actives_noise_free::kFeaturesActiveMetricReportId,
                                  day_index, 0u));
  EXPECT_EQ(kOK,
            LogUniqueActivesEvent(testing::unique_actives_noise_free::kEventsOccurredMetricReportId,
                                  day_index, 1u));
  // Generate locally aggregated Observations for |day_index|.
  EXPECT_EQ(kOK, GenerateObservations(day_index));

  // Form the expected observations.
  auto expected_obs = MakeNullExpectedUniqueActivesObservations(
      testing::unique_actives_noise_free::kExpectedAggregationParams, day_index);
  expected_obs[{testing::unique_actives_noise_free::kFeaturesActiveMetricReportId, day_index}] = {
      {1, {true, false, false, false, false}},
      {7, {true, false, false, false, false}},
      {30, {true, false, false, false, false}}};
  expected_obs[{testing::unique_actives_noise_free::kEventsOccurredMetricReportId, day_index}] = {
      {1, {false, true, false, false, false}}, {7, {false, true, false, false, false}}};

  // Check the contents of the FakeObservationStore.
  EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                             update_recipient_.get()));
}

// Checks that UniqueActivesObservations with the expected values are
// generated when some events have been logged for a UNIQUE_N_DAY_ACTIVES
// report over multiple days and GenerateObservations() is called each
// day, without garbage collection or backfill.
//
// Logging pattern:
// Logs events for the EventsOccurred_UniqueDevices report (whose parent
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
// equal to the daily_num_obs field of
// |testing::unique_actives_noise_free::kExpectedAggregationParams|.
//
// Expected Observation values:
// The EventsOccurred_UniqueDevices report has window sizes 1 and 7, and
// the expected activity indicators of Observations for that report on the
// i-th day are:
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
TEST_F(UniqueActivesNoiseFreeEventAggregatorTest, CheckObservationValuesMultiDay) {
  auto start_day_index = CurrentDayIndex();
  // Form expected Obsevations for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedUniqueActivesObservations> expected_obs(num_days);
  const auto& expected_id = testing::unique_actives_noise_free::kEventsOccurredMetricReportId;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_obs[offset] = MakeNullExpectedUniqueActivesObservations(
        testing::unique_actives_noise_free::kExpectedAggregationParams, start_day_index + offset);
  }
  expected_obs[0][{expected_id, start_day_index}] = {{1, {false, true, true, true, true}},
                                                     {7, {false, true, true, true, true}}};
  expected_obs[1][{expected_id, start_day_index + 1}] = {{1, {false, false, false, false, false}},
                                                         {7, {false, true, true, true, true}}};
  expected_obs[2][{expected_id, start_day_index + 2}] = {{1, {false, false, false, false, false}},
                                                         {7, {false, true, true, true, true}}};
  expected_obs[3][{expected_id, start_day_index + 3}] = {{1, {false, true, false, false, false}},
                                                         {7, {false, true, true, true, true}}};
  expected_obs[4][{expected_id, start_day_index + 4}] = {{1, {false, false, false, false, false}},
                                                         {7, {false, true, true, true, true}}};
  expected_obs[5][{expected_id, start_day_index + 5}] = {{1, {false, false, false, false, false}},
                                                         {7, {false, true, true, true, true}}};
  expected_obs[6][{expected_id, start_day_index + 6}] = {{1, {false, true, true, false, false}},
                                                         {7, {false, true, true, true, true}}};
  expected_obs[7][{expected_id, start_day_index + 7}] = {{1, {false, false, false, false, false}},
                                                         {7, {false, true, true, false, false}}};
  expected_obs[8][{expected_id, start_day_index + 8}] = {{1, {false, false, false, false, false}},
                                                         {7, {false, true, true, false, false}}};
  expected_obs[9][{expected_id, start_day_index + 9}] = {{1, {false, true, false, true, false}},
                                                         {7, {false, true, true, true, false}}};

  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    for (uint32_t event_code = 1;
         event_code <
         testing::unique_actives_noise_free::kExpectedAggregationParams.num_event_codes.at(
             expected_id);
         event_code++) {
      if (offset % (3 * event_code) == 0) {
        EXPECT_EQ(kOK, LogUniqueActivesEvent(expected_id, day_index, event_code));
      }
    }
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Generate locally aggregated Observations.
    EXPECT_EQ(kOK, GenerateObservations(day_index));
    // Check the generated Observations against the expectation.
    EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs[offset], observation_store_.get(),
                                               update_recipient_.get()));
    AdvanceClock(kDay);
  }
}

// Checks that UniqueActivesObservations with the expected values are
// generated when some events have been logged for a UNIQUE_N_DAY_ACTIVES
// report over multiple days and GenerateObservations() is called each
// day, and when the LocalAggregateStore is garbage-collected after each call
// to GenerateObservations().
//
// Logging pattern:
// Logs events for the EventsOccurred_UniqueDevices report (whose parent
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
// equal to the daily_num_obs field of
// |testing::unique_actives_noise_free::kExpectedAggregationParams|.
//
// Expected Observation values:
// The EventsOccurred_UniqueDevices report has window sizes 1 and 7, and
// the expected activity indicators of Observations for that report on the
// i-th day are:
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
TEST_F(UniqueActivesNoiseFreeEventAggregatorTest,
       CheckObservationValuesMultiDayWithGarbageCollection) {
  auto start_day_index = CurrentDayIndex();
  // Form expected Observations for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedUniqueActivesObservations> expected_obs(num_days);
  const auto& expected_id = testing::unique_actives_noise_free::kEventsOccurredMetricReportId;

  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_obs[offset] = MakeNullExpectedUniqueActivesObservations(
        testing::unique_actives_noise_free::kExpectedAggregationParams, start_day_index + offset);
  }
  expected_obs[0][{expected_id, start_day_index}] = {{1, {false, true, true, true, true}},
                                                     {7, {false, true, true, true, true}}};
  expected_obs[1][{expected_id, start_day_index + 1}] = {{1, {false, false, false, false, false}},
                                                         {7, {false, true, true, true, true}}};
  expected_obs[2][{expected_id, start_day_index + 2}] = {{1, {false, false, false, false, false}},
                                                         {7, {false, true, true, true, true}}};
  expected_obs[3][{expected_id, start_day_index + 3}] = {{1, {false, true, false, false, false}},
                                                         {7, {false, true, true, true, true}}};
  expected_obs[4][{expected_id, start_day_index + 4}] = {{1, {false, false, false, false, false}},
                                                         {7, {false, true, true, true, true}}};
  expected_obs[5][{expected_id, start_day_index + 5}] = {{1, {false, false, false, false, false}},
                                                         {7, {false, true, true, true, true}}};
  expected_obs[6][{expected_id, start_day_index + 6}] = {{1, {false, true, true, false, false}},
                                                         {7, {false, true, true, true, true}}};
  expected_obs[7][{expected_id, start_day_index + 7}] = {{1, {false, false, false, false, false}},
                                                         {7, {false, true, true, false, false}}};
  expected_obs[8][{expected_id, start_day_index + 8}] = {{1, {false, false, false, false, false}},
                                                         {7, {false, true, true, false, false}}};
  expected_obs[9][{expected_id, start_day_index + 9}] = {{1, {false, true, false, true, false}},
                                                         {7, {false, true, true, true, false}}};

  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    for (uint32_t event_code = 1;
         event_code <
         testing::unique_actives_noise_free::kExpectedAggregationParams.num_event_codes.at(
             expected_id);
         event_code++) {
      if (offset % (3 * event_code) == 0) {
        EXPECT_EQ(kOK, LogUniqueActivesEvent(expected_id, day_index, event_code));
      }
    }
    // Advance |test_clock_| by 1 day.
    AdvanceClock(kDay);
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Generate locally aggregated Observations and garbage-collect the
    // LocalAggregateStore, both for the previous day as measured by
    // |test_clock_|. Back up the LocalAggregateStore and
    // AggregatedObservationHistoryStore.
    DoScheduledTasksNow();
    // Check the generated Observations against the expectation.
    EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs[offset], observation_store_.get(),
                                               update_recipient_.get()));
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
// |start_day_index| to |start_day_index + 8| according to the following
// pattern:
//
// * For i = 0 to i = 4, log an event with event code i on day
// |start_day_index + i| and |start_day_index + 2*i|.
//
// Observation generation pattern:
// The test calls GenerateObservations() on day |start_day_index + i| for i =
// 0 through i = 5 and for i = 8, skipping the days |start_day_index + 6| and
// |start_day_index + 7|.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on the
// first day (the day index for which GenerateObservations() was called, plus
// 3 days of backfill), that 1 day's worth of Observations is generated on the
// 2nd through 6th day, that 3 days' worth of Observations are generated on
// the 9th day (the day index for which GenerateObservations() was called,
// plus 2 days of backfill), and that no Observations are generated on the
// remaining days.
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
TEST_F(UniqueActivesNoiseFreeEventAggregatorTest, CheckObservationValuesWithBackfill) {
  auto start_day_index = CurrentDayIndex();
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  const auto& expected_id = testing::unique_actives_noise_free::kEventsOccurredMetricReportId;
  const auto& expected_params = testing::unique_actives_noise_free::kExpectedAggregationParams;
  // Log events for 9 days. Call GenerateObservations() on the first 6 day
  // indices, and the 9th.
  for (uint32_t offset = 0; offset < 9; offset++) {
    auto day_index = CurrentDayIndex();
    ResetObservationStore();
    for (uint32_t event_code = 0; event_code < expected_params.num_event_codes.at(expected_id);
         event_code++) {
      if (event_code == offset || (2 * event_code) == offset) {
        EXPECT_EQ(kOK, LogUniqueActivesEvent(expected_id, day_index, event_code));
      }
    }
    if (offset < 6 || offset == 8) {
      EXPECT_EQ(kOK, GenerateObservations(day_index));
    }
    // Make the set of Observations which are expected to be generated on
    // |start_day_index + offset| and check it against the contents of the
    // FakeObservationStore.
    ExpectedUniqueActivesObservations expected_obs;
    switch (offset) {
      case 0: {
        for (uint32_t day_index = start_day_index - backfill_days; day_index <= start_day_index;
             day_index++) {
          for (const auto& pair :
               MakeNullExpectedUniqueActivesObservations(expected_params, day_index)) {
            expected_obs.insert(pair);
          }
        }
        expected_obs[{expected_id, start_day_index}] = {{1, {true, false, false, false, false}},
                                                        {7, {true, false, false, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      case 1: {
        expected_obs =
            MakeNullExpectedUniqueActivesObservations(expected_params, start_day_index + 1);
        expected_obs[{expected_id, start_day_index + 1}] = {{1, {false, true, false, false, false}},
                                                            {7, {true, true, false, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      case 2: {
        expected_obs =
            MakeNullExpectedUniqueActivesObservations(expected_params, start_day_index + 2);
        expected_obs[{expected_id, start_day_index + 2}] = {{1, {false, true, true, false, false}},
                                                            {7, {true, true, true, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      case 3: {
        expected_obs =
            MakeNullExpectedUniqueActivesObservations(expected_params, start_day_index + 3);
        expected_obs[{expected_id, start_day_index + 3}] = {{1, {false, false, false, true, false}},
                                                            {7, {true, true, true, true, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      case 4: {
        expected_obs =
            MakeNullExpectedUniqueActivesObservations(expected_params, start_day_index + 4);
        expected_obs[{expected_id, start_day_index + 4}] = {{1, {false, false, true, false, true}},
                                                            {7, {true, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      case 5: {
        expected_obs =
            MakeNullExpectedUniqueActivesObservations(expected_params, start_day_index + 5);
        expected_obs[{expected_id, start_day_index + 5}] = {
            {1, {false, false, false, false, false}}, {7, {true, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      case 8: {
        for (uint32_t day_index = start_day_index + 6; day_index <= start_day_index + 8;
             day_index++) {
          for (const auto& pair :
               MakeNullExpectedUniqueActivesObservations(expected_params, day_index)) {
            expected_obs.insert(pair);
          }
        }
        expected_obs[{expected_id, start_day_index + 6}] = {{1, {false, false, false, true, false}},
                                                            {7, {true, true, true, true, true}}};
        expected_obs[{expected_id, start_day_index + 7}] = {
            {1, {false, false, false, false, false}}, {7, {false, true, true, true, true}}};
        expected_obs[{expected_id, start_day_index + 8}] = {{1, {false, false, false, false, true}},
                                                            {7, {false, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      default:
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
    }
    AdvanceClock(kDay);
  }
}

// Tests that the expected UniqueActivesObservations are generated when events
// are logged over multiple days and when Observations are backfilled for some
// days during that period, and when the LocalAggregateStore is
// garbage-collected after each call to GenerateObservations().
//
// The test sets the number of backfill days to 3.
//
// Logging pattern:
// Events for the EventsOccurred_UniqueDevices report are logged over the days
// |start_day_index| to |start_day_index + 8| according to the following
// pattern:
//
// * For i = 0 to i = 4, log an event with event code i on day
// |start_day_index + i| and |start_day_index + 2*i|.
//
// Observation generation pattern:
// The test calls GenerateObservations() on day |start_day_index + i| for i =
// 0 through i = 5 and for i = 8, skipping the days |start_day_index + 6| and
// |start_day_index + 7|.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on the
// first day (the day index for which GenerateObservations() was called, plus
// 3 days of backfill), that 1 day's worth of Observations is generated on the
// 2nd through 6th day, that 3 days' worth of Observations are generated on
// the 9th day (the day index for which GenerateObservations() was called,
// plus 2 days of backfill), and that no Observations are generated on the
// remaining days.
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
TEST_F(UniqueActivesNoiseFreeEventAggregatorTest, CheckObservationValuesWithBackfillAndGc) {
  auto start_day_index = CurrentDayIndex();
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);

  const auto& expected_id = testing::unique_actives_noise_free::kEventsOccurredMetricReportId;
  const auto& expected_params = testing::unique_actives_noise_free::kExpectedAggregationParams;

  // Log events for 9 days. Call GenerateObservations() on the first 6 day
  // indices, and the 9th.
  for (uint32_t offset = 0; offset < 8; offset++) {
    auto day_index = CurrentDayIndex();
    ResetObservationStore();
    for (uint32_t event_code = 0; event_code < expected_params.num_event_codes.at(expected_id);
         event_code++) {
      if (event_code == offset || (2 * event_code) == offset) {
        EXPECT_EQ(kOK, LogUniqueActivesEvent(expected_id, day_index, event_code));
      }
    }
    // Advance |test_clock_| by 1 day.
    AdvanceClock(kDay);
    if (offset < 6 || offset == 9) {
      // Generate Observations and garbage-collect, both for the previous day
      // index according to |test_clock_|. Back up the LocalAggregateStore and
      // the AggregatedObservationHistoryStore.
      DoScheduledTasksNow();
    }
    // Make the set of Observations which are expected to be generated on
    // |start_day_index + offset| and check it against the contents of the
    // FakeObservationStore.
    ExpectedUniqueActivesObservations expected_obs;
    switch (offset) {
      case 0: {
        for (uint32_t day_index = start_day_index - backfill_days; day_index <= start_day_index;
             day_index++) {
          for (const auto& pair :
               MakeNullExpectedUniqueActivesObservations(expected_params, day_index)) {
            expected_obs.insert(pair);
          }
        }
        expected_obs[{expected_id, start_day_index}] = {{1, {true, false, false, false, false}},
                                                        {7, {true, false, false, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      case 1: {
        expected_obs =
            MakeNullExpectedUniqueActivesObservations(expected_params, start_day_index + 1);
        expected_obs[{expected_id, start_day_index + 1}] = {{1, {false, true, false, false, false}},
                                                            {7, {true, true, false, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      case 2: {
        expected_obs =
            MakeNullExpectedUniqueActivesObservations(expected_params, start_day_index + 2);
        expected_obs[{expected_id, start_day_index + 2}] = {{1, {false, true, true, false, false}},
                                                            {7, {true, true, true, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      case 3: {
        expected_obs =
            MakeNullExpectedUniqueActivesObservations(expected_params, start_day_index + 3);
        expected_obs[{expected_id, start_day_index + 3}] = {{1, {false, false, false, true, false}},
                                                            {7, {true, true, true, true, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      case 4: {
        expected_obs =
            MakeNullExpectedUniqueActivesObservations(expected_params, start_day_index + 4);
        expected_obs[{expected_id, start_day_index + 4}] = {{1, {false, false, true, false, true}},
                                                            {7, {true, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      case 5: {
        expected_obs =
            MakeNullExpectedUniqueActivesObservations(expected_params, start_day_index + 5);
        expected_obs[{expected_id, start_day_index + 5}] = {
            {1, {false, false, false, false, false}}, {7, {true, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      case 8: {
        for (uint32_t day_index = start_day_index + 6; day_index <= start_day_index + 8;
             day_index++) {
          for (const auto& pair :
               MakeNullExpectedUniqueActivesObservations(expected_params, day_index)) {
            expected_obs.insert(pair);
          }
        }
        expected_obs[{expected_id, start_day_index + 6}] = {{1, {false, false, false, true, false}},
                                                            {7, {true, true, true, true, true}}};
        expected_obs[{expected_id, start_day_index + 7}] = {
            {1, {false, false, false, false, false}}, {7, {false, true, true, true, true}}};
        expected_obs[{expected_id, start_day_index + 8}] = {{1, {false, false, false, false, true}},
                                                            {7, {false, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
        break;
      }
      default:
        EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                                   update_recipient_.get()));
    }
  }
}

// Tests that the LocalAggregateStore is updated as expected when
// EventAggregator::LogPerDeviceCountEvent() is called with valid arguments;
// i.e., with a report ID associated to an existing key of the
// LocalAggregateStore, and with an EventRecord which wraps a CountEvent.
//
// Logs some valid events each day for 35 days, checking the contents of the
// LocalAggregateStore each day.
TEST_F(PerDeviceNumericEventAggregatorTest, LogEvents) {
  LoggedValues logged_values;

  std::vector<MetricReportId> count_metric_report_ids = {
      testing::per_device_numeric_stats::kSettingsChangedMetricReportId,
      testing::per_device_numeric_stats::kConnectionFailuresMetricReportId};
  std::vector<MetricReportId> elapsed_time_metric_report_ids = {
      testing::per_device_numeric_stats::kStreamingTimeTotalMetricReportId,
      testing::per_device_numeric_stats::kStreamingTimeMinMetricReportId,
      testing::per_device_numeric_stats::kStreamingTimeMaxMetricReportId};
  MetricReportId frame_rate_metric_report_id =
      testing::per_device_numeric_stats::kLoginModuleFrameRateMinMetricReportId;
  MetricReportId memory_usage_metric_report_id =
      testing::per_device_numeric_stats::kLedgerMemoryUsageMaxMetricReportId;

  uint32_t num_days = 35;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    for (const auto& id : count_metric_report_ids) {
      for (const auto& component : {"component_A", "component_B", "component_C"}) {
        // Log 2 events with event code 0, for each component A, B, C.
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(id, day_index, component, 0u, 2, &logged_values));
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(id, day_index, component, 0u, 3, &logged_values));
      }
      if (offset < 3) {
        // Log 1 event for component D and event code 1.
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(id, day_index, "component_D", 1u, 4, &logged_values));
      }
    }
    for (const auto& id : elapsed_time_metric_report_ids) {
      for (const auto& component : {"component_A", "component_B", "component_C"}) {
        // Log 2 events with event code 0, for each component A, B, C.
        EXPECT_EQ(kOK,
                  LogPerDeviceElapsedTimeEvent(id, day_index, component, 0u, 2, &logged_values));
        EXPECT_EQ(kOK,
                  LogPerDeviceElapsedTimeEvent(id, day_index, component, 0u, 3, &logged_values));
      }
      if (offset < 3) {
        // Log 1 event for component D and event code 1.
        EXPECT_EQ(
            kOK, LogPerDeviceElapsedTimeEvent(id, day_index, "component_D", 1u, 4, &logged_values));
      }
    }
    for (const auto& component : {"component_A", "component_B"}) {
      // Log some events for a FRAME_RATE metric with a PerDeviceNumericStats
      // report.
      EXPECT_EQ(kOK, LogPerDeviceFrameRateEvent(frame_rate_metric_report_id, day_index, component,
                                                0u, 2.25, &logged_values));
      EXPECT_EQ(kOK, LogPerDeviceFrameRateEvent(frame_rate_metric_report_id, day_index, component,
                                                0u, 1.75, &logged_values));
      // Log some events for a MEMORY_USAGE metric with a
      // PerDeviceNumericStats report.
      EXPECT_EQ(kOK,
                LogPerDeviceMemoryUsageEvent(memory_usage_metric_report_id, day_index, component,
                                             std::vector<uint32_t>{0u, 0u}, 300, &logged_values));
      EXPECT_EQ(kOK,
                LogPerDeviceMemoryUsageEvent(memory_usage_metric_report_id, day_index, component,
                                             std::vector<uint32_t>{1u, 0u}, 300, &logged_values));
    }
    EXPECT_TRUE(CheckPerDeviceNumericAggregates(logged_values, day_index));
    AdvanceClock(kDay);
  }
}

// Tests GarbageCollect() for PerDeviceNumericReportAggregates.
//
// For each value of N in the range [0, 34], logs some events for
// PerDeviceNumeric reports each day for N consecutive days, and then
// garbage-collects the LocalAggregateStore. After garbage collection, verifies
// the contents of the LocalAggregateStore.
TEST_F(PerDeviceNumericEventAggregatorTest, GarbageCollect) {
  uint32_t max_days_before_gc = 35;
  for (uint32_t days_before_gc = 0; days_before_gc < max_days_before_gc; days_before_gc++) {
    SetUp();
    day_last_garbage_collected_ = 0u;
    LoggedValues logged_values;
    std::vector<MetricReportId> count_metric_report_ids = {
        testing::per_device_numeric_stats::kSettingsChangedMetricReportId,
        testing::per_device_numeric_stats::kConnectionFailuresMetricReportId};
    std::vector<MetricReportId> elapsed_time_metric_report_ids = {
        testing::per_device_numeric_stats::kStreamingTimeTotalMetricReportId,
        testing::per_device_numeric_stats::kStreamingTimeMinMetricReportId,
        testing::per_device_numeric_stats::kStreamingTimeMaxMetricReportId};
    MetricReportId frame_rate_metric_report_id =
        testing::per_device_numeric_stats::kLoginModuleFrameRateMinMetricReportId;
    MetricReportId memory_usage_metric_report_id =
        testing::per_device_numeric_stats::kLedgerMemoryUsageMaxMetricReportId;
    for (uint32_t offset = 0; offset < days_before_gc; offset++) {
      auto day_index = CurrentDayIndex();
      for (const auto& id : count_metric_report_ids) {
        for (const auto& component : {"component_A", "component_B", "component_C"}) {
          // Log 2 events with event code 0, for each component A, B, C.
          EXPECT_EQ(kOK, LogPerDeviceCountEvent(id, day_index, component, 0u, 2, &logged_values));
          EXPECT_EQ(kOK, LogPerDeviceCountEvent(id, day_index, component, 0u, 3, &logged_values));
        }
        if (offset < 3) {
          // Log 1 event for component D and event code 1.
          EXPECT_EQ(kOK,
                    LogPerDeviceCountEvent(id, day_index, "component_D", 1u, 4, &logged_values));
        }
      }
      for (const auto& id : elapsed_time_metric_report_ids) {
        for (const auto& component : {"component_A", "component_B", "component_C"}) {
          // Log 2 events with event code 0, for each component A, B, C.
          EXPECT_EQ(kOK,
                    LogPerDeviceElapsedTimeEvent(id, day_index, component, 0u, 2, &logged_values));
          EXPECT_EQ(kOK,
                    LogPerDeviceElapsedTimeEvent(id, day_index, component, 0u, 3, &logged_values));
        }
        if (offset < 3) {
          // Log 1 event for component D and event code 1.
          EXPECT_EQ(kOK, LogPerDeviceElapsedTimeEvent(id, day_index, "component_D", 1u, 4,
                                                      &logged_values));
        }
      }
      for (const auto& component : {"component_A", "component_B"}) {
        EXPECT_EQ(kOK, LogPerDeviceFrameRateEvent(frame_rate_metric_report_id, day_index, component,
                                                  0u, 2.25, &logged_values));
        EXPECT_EQ(kOK, LogPerDeviceFrameRateEvent(frame_rate_metric_report_id, day_index, component,
                                                  0u, 1.75, &logged_values));
        EXPECT_EQ(kOK,
                  LogPerDeviceMemoryUsageEvent(memory_usage_metric_report_id, day_index, component,
                                               std::vector<uint32_t>{0u, 0u}, 300, &logged_values));
        EXPECT_EQ(kOK,
                  LogPerDeviceMemoryUsageEvent(memory_usage_metric_report_id, day_index, component,
                                               std::vector<uint32_t>{1u, 0u}, 300, &logged_values));
      }
      AdvanceClock(kDay);
    }
    auto end_day_index = CurrentDayIndex();
    EXPECT_EQ(kOK, GarbageCollect(end_day_index));
    day_last_garbage_collected_ = end_day_index;
    EXPECT_TRUE(CheckPerDeviceNumericAggregates(logged_values, end_day_index));
    TearDown();
  }
}

// Tests that EventAggregator::GenerateObservations() returns a positive
// status and that the expected number of Observations is generated after
// some CountEvents have been logged for PerDeviceNumericStats reports, without
// any garbage collection.
//
// For 35 days, logs a positive number of events each day for the
// ConnectionFailures_PerDeviceNumericStats report with "component_A" and for
// the SettingsChanged_PerDeviceNumericStats report with "component_B", all with
// event code 0.
//
// Each day, calls GenerateObservations() with the day index of the previous
// day. Checks that a positive status is returned and that the
// FakeObservationStore has received the expected number of new observations
// for each locally aggregated report ID in the per_device_numeric_stats test
// registry.
TEST_F(PerDeviceNumericEventAggregatorTest, GenerateObservations) {
  int num_days = 1;
  std::vector<Observation2> observations(0);
  ExpectedAggregationParams expected_params =
      testing::per_device_numeric_stats::kExpectedAggregationParams;
  for (int offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    observations.clear();
    ResetObservationStore();
    EXPECT_EQ(kOK, GenerateObservations(day_index - 1));
    EXPECT_TRUE(FetchAggregatedObservations(&observations, expected_params,
                                            observation_store_.get(), update_recipient_.get()));
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(
                         testing::per_device_numeric_stats::kConnectionFailuresMetricReportId,
                         day_index, "component_A", 0u, 1));
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(
                         testing::per_device_numeric_stats::kSettingsChangedMetricReportId,
                         day_index, "component_B", 0u, 5));
    }
    // If this is the first time we're logging events, update the expected
    // numbers of generated Observations to account for the logged events.
    // For each report, for each window size, expect 1 Observation more than if
    // no events had been logged.
    if (offset == 0) {
      expected_params.daily_num_obs += 3;
      expected_params.num_obs_per_report
          [testing::per_device_numeric_stats::kConnectionFailuresMetricReportId] += 1;
      expected_params
          .num_obs_per_report[testing::per_device_numeric_stats::kSettingsChangedMetricReportId] +=
          2;
    }
    AdvanceClock(kDay);
  }
  observations.clear();
  ResetObservationStore();
  EXPECT_EQ(kOK, GenerateObservations(CurrentDayIndex() - 1));
  EXPECT_TRUE(FetchAggregatedObservations(&observations, expected_params, observation_store_.get(),
                                          update_recipient_.get()));
}

// Tests that EventAggregator::GenerateObservations() returns a positive
// status and that the expected number of Observations is generated after
// some CountEvents have been logged for PerDeviceNumeric reports over multiple
// days, and when the LocalAggregateStore is garbage-collected each day.
//
// For 35 days, logs a positive number of events each day for the
// ConnectionFailures_PerDeviceNumeric report with "component_A" and for
// the SettingsChanged_PerDeviceNumeric report with "component_B", all with
// event code 0.
//
// Each day, calls GenerateObservations() with the day index of the previous
// day. Checks that a positive status is returned and that the
// FakeObservationStore has received the expected number of new observations
// for each locally aggregated report ID in the per_device_numeric_stats test
// registry.
TEST_F(PerDeviceNumericEventAggregatorTest, GenerateObservationsWithGc) {
  int num_days = 35;
  std::vector<Observation2> observations(0);
  ExpectedAggregationParams expected_params =
      testing::per_device_numeric_stats::kExpectedAggregationParams;
  for (int offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    observations.clear();
    ResetObservationStore();
    EXPECT_EQ(kOK, GenerateObservations(day_index - 1));
    EXPECT_TRUE(FetchAggregatedObservations(&observations, expected_params,
                                            observation_store_.get(), update_recipient_.get()));
    EXPECT_EQ(kOK, GarbageCollect(day_index));
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(
                         testing::per_device_numeric_stats::kConnectionFailuresMetricReportId,
                         day_index, "component_A", 0u, 1));
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(
                         testing::per_device_numeric_stats::kSettingsChangedMetricReportId,
                         day_index, "component_B", 0u, 5));
    }
    // If this is the first time we're logging events, update the expected
    // numbers of generated Observations to account for the logged events.
    // For each report, for each window size, expect 1 Observation more than if
    // no events had been logged.
    if (offset == 0) {
      expected_params.daily_num_obs += 3;
      expected_params.num_obs_per_report
          [testing::per_device_numeric_stats::kConnectionFailuresMetricReportId] += 1;
      expected_params
          .num_obs_per_report[testing::per_device_numeric_stats::kSettingsChangedMetricReportId] +=
          2;
    }
    AdvanceClock(kDay);
  }
  observations.clear();
  ResetObservationStore();
  auto day_index = CurrentDayIndex();
  EXPECT_EQ(kOK, GenerateObservations(day_index - 1));
  EXPECT_TRUE(FetchAggregatedObservations(&observations, expected_params, observation_store_.get(),
                                          update_recipient_.get()));
  EXPECT_EQ(kOK, GarbageCollect(day_index));
}

// Tests that GenerateObservations() returns a positive status and that the
// expected number of Observations is generated when events are logged over
// multiple days and some of those days' Observations are backfilled, without
// any garbage collection of the LocalAggregateStore.
//
// Sets the |backfill_days_| field of the EventAggregator to 3.
//
// Logging pattern:
// For 35 days, logs 2 events each day for the
// ConnectionFailures_PerDeviceCount report and 2 events for the
// SettingsChanged_PerDeviceCount report, all with event code 0.
//
// Observation generation pattern:
// Calls GenerateObservations() on the 1st through 5th and the 7th out of
// every 10 days, for 35 days.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on
// the first day of every 10 (the day index for which GenerateObservations()
// was called, plus 3 days of backfill), that 1 day's worth of Observations
// are generated on the 2nd through 5th day of every 10, that 2 days'
// worth of Observations are generated on the 7th day of every 10 (the
// day index for which GenerateObservations() was called, plus 1 day of
// backfill), and that no Observations are generated on the remaining days.
TEST_F(PerDeviceNumericEventAggregatorTest, GenerateObservationsWithBackfill) {
  const auto& expected_params = testing::per_device_numeric_stats::kExpectedAggregationParams;
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log 2 events each day for 35 days. Call GenerateObservations() on the
  // first 5 day indices, and the 7th, out of every 10.
  for (int offset = 0; offset < 35; offset++) {
    auto day_index = CurrentDayIndex();
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(
                         testing::per_device_numeric_stats::kConnectionFailuresMetricReportId,
                         day_index, "component_A", 0u, 1));
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(
                         testing::per_device_numeric_stats::kSettingsChangedMetricReportId,
                         day_index, "component_B", 0u, 5));
    }
    auto num_obs_before = observation_store_->messages_received.size();
    if (offset % 10 < 5 || offset % 10 == 6) {
      EXPECT_EQ(kOK, GenerateObservations(day_index));
    }
    auto num_obs_after = observation_store_->messages_received.size();
    EXPECT_GE(num_obs_after, num_obs_before);
    // Check that the expected daily number of Observations was generated.
    switch (offset % 10) {
      case 0:
        // If this is the first day of logging, expect 3 Observations for each
        // day in the backfill period and 6 Observations for the current day.
        if (offset == 0) {
          EXPECT_EQ(
              (expected_params.daily_num_obs * backfill_days) + expected_params.daily_num_obs + 3,
              num_obs_after - num_obs_before);
        } else {
          // If this is another day whose offset is a multiple of 10, expect 6
          // Observations for each day in the backfill period as well as the
          // current day.
          EXPECT_EQ((expected_params.daily_num_obs + 3) * (backfill_days + 1),
                    num_obs_after - num_obs_before);
        }
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        // Expect 6 Observations for this day.
        EXPECT_EQ(expected_params.daily_num_obs + 3, num_obs_after - num_obs_before);
        break;
      case 6:
        // Expect 6 Observations for each of today and yesterday.
        EXPECT_EQ((expected_params.daily_num_obs + 3) * 2, num_obs_after - num_obs_before);
        break;
      default:
        EXPECT_EQ(num_obs_after, num_obs_before);
    }
    AdvanceClock(kDay);
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
// For 35 days, logs 2 events each day for the
// ConnectionFailures_PerDeviceNumeric report with "component_A" and 2 events
// for the SettingsChanged_PerDeviceNumeric report with "component_B", all with
// event code 0.
//
// Observation generation pattern:
// Calls GenerateObservations() on the 1st through 5th and the 7th out of
// every 10 days, for 35 days. Garbage-collects the LocalAggregateStore after
// each call.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on
// the first day of every 10 (the day index for which GenerateObservations()
// was called, plus 3 days of backfill), that 1 day's worth of Observations
// are generated on the 2nd through 5th day of every 10, that 2 days'
// worth of Observations are generated on the 7th day of every 10 (the
// day index for which GenerateObservations() was called, plus 1 day of
// backfill), and that no Observations are generated on the remaining days.
TEST_F(PerDeviceNumericEventAggregatorTest, GenerateObservationsWithBackfillAndGc) {
  int num_days = 35;
  const auto& expected_params = testing::per_device_numeric_stats::kExpectedAggregationParams;
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log 2 events each day for 35 days. Call GenerateObservations() on the
  // first 5 day indices, and the 7th, out of every 10.
  for (int offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(
                         testing::per_device_numeric_stats::kConnectionFailuresMetricReportId,
                         day_index, "component_A", 0u, 1));
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(
                         testing::per_device_numeric_stats::kSettingsChangedMetricReportId,
                         day_index, "component_B", 0u, 5));
    }
    auto num_obs_before = observation_store_->messages_received.size();
    if (offset % 10 < 5 || offset % 10 == 6) {
      EXPECT_EQ(kOK, GenerateObservations(day_index));
      EXPECT_EQ(kOK, GarbageCollect(day_index));
    }
    auto num_obs_after = observation_store_->messages_received.size();
    EXPECT_GE(num_obs_after, num_obs_before);
    // Check that the expected daily number of Observations was generated.
    switch (offset % 10) {
      case 0:
        // If this is the first day of logging, expect 3 Observations for each
        // day in the backfill period and 6 Observations for the current day.
        if (offset == 0) {
          EXPECT_EQ(
              (expected_params.daily_num_obs * backfill_days) + expected_params.daily_num_obs + 3,
              num_obs_after - num_obs_before);
        } else {
          // If this is another day whose offset is a multiple of 10, expect 6
          // Observations for each day in the backfill period as well as the
          // current day.
          EXPECT_EQ((expected_params.daily_num_obs + 3) * (backfill_days + 1),
                    num_obs_after - num_obs_before);
        }
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        // Expect 6 Observations for this day.
        EXPECT_EQ(expected_params.daily_num_obs + 3, num_obs_after - num_obs_before);
        break;
      case 6:
        // Expect 6 Observations for each of today and yesterday.
        EXPECT_EQ((expected_params.daily_num_obs + 3) * 2, num_obs_after - num_obs_before);
        break;
      default:
        EXPECT_EQ(num_obs_after, num_obs_before);
    }
    AdvanceClock(kDay);
  }
}

// Generate Observations without logging any events, and check that the
// resulting Observations are as expected: 1 ReportParticipationObservation for
// each PER_DEVICE_NUMERIC_STATS report in the config, and no
// PerDeviceNumericObservations.
TEST_F(PerDeviceNumericEventAggregatorTest, CheckObservationValuesNoEvents) {
  const auto current_day_index = CurrentDayIndex();
  EXPECT_EQ(kOK, GenerateObservations(current_day_index));
  const auto& expected_report_participation_obs = MakeExpectedReportParticipationObservations(
      testing::per_device_numeric_stats::kExpectedAggregationParams, current_day_index);
  EXPECT_TRUE(CheckPerDeviceNumericObservations({}, expected_report_participation_obs,
                                                observation_store_.get(), update_recipient_.get()));
}

// Check that the expected PerDeviceNumericObservations and
// ReportParticipationObservations are generated when GenerateObservations() is
// called after logging some CountEvents and ElapsedTimeEvents for
// PER_DEVICE_NUMERIC_STATS reports over a single day index.
TEST_F(PerDeviceNumericEventAggregatorTest, CheckObservationValuesSingleDay) {
  const auto day_index = CurrentDayIndex();
  // Log several events on |day_index|.
  EXPECT_EQ(kOK, LogPerDeviceCountEvent(
                     testing::per_device_numeric_stats::kConnectionFailuresMetricReportId,
                     day_index, "component_A", 0u, 5));
  EXPECT_EQ(kOK, LogPerDeviceCountEvent(
                     testing::per_device_numeric_stats::kConnectionFailuresMetricReportId,
                     day_index, "component_B", 0u, 5));
  EXPECT_EQ(kOK, LogPerDeviceCountEvent(
                     testing::per_device_numeric_stats::kConnectionFailuresMetricReportId,
                     day_index, "component_A", 0u, 5));
  EXPECT_EQ(kOK, LogPerDeviceCountEvent(
                     testing::per_device_numeric_stats::kConnectionFailuresMetricReportId,
                     day_index, "component_A", 1u, 5));
  EXPECT_EQ(
      kOK, LogPerDeviceCountEvent(testing::per_device_numeric_stats::kSettingsChangedMetricReportId,
                                  day_index, "component_C", 0u, 5));
  EXPECT_EQ(
      kOK, LogPerDeviceCountEvent(testing::per_device_numeric_stats::kSettingsChangedMetricReportId,
                                  day_index, "component_C", 0u, 5));

  std::vector<MetricReportId> streaming_time_ids = {
      testing::per_device_numeric_stats::kStreamingTimeTotalMetricReportId,
      testing::per_device_numeric_stats::kStreamingTimeMinMetricReportId,
      testing::per_device_numeric_stats::kStreamingTimeMaxMetricReportId};
  for (const auto& id : streaming_time_ids) {
    EXPECT_EQ(kOK, LogPerDeviceElapsedTimeEvent(id, day_index, "component_D", 0u, 15));
    EXPECT_EQ(kOK, LogPerDeviceElapsedTimeEvent(id, day_index, "component_D", 1u, 5));
    EXPECT_EQ(kOK, LogPerDeviceElapsedTimeEvent(id, day_index, "component_D", 0u, 10));
  }
  // Generate locally aggregated Observations for |day_index|.
  EXPECT_EQ(kOK, GenerateObservations(day_index));

  // Form the expected Observations.
  auto expected_report_participation_obs = MakeExpectedReportParticipationObservations(
      testing::per_device_numeric_stats::kExpectedAggregationParams, day_index);
  ExpectedPerDeviceNumericObservations expected_per_device_numeric_obs;
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kConnectionFailuresMetricReportId, day_index}][1] = {
      {"component_A", 0u, 10}, {"component_A", 1u, 5}, {"component_B", 0u, 5}};
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kSettingsChangedMetricReportId, day_index}][7] = {
      {"component_C", 0u, 10}};
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kSettingsChangedMetricReportId, day_index}][30] = {
      {"component_C", 0u, 10}};
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kStreamingTimeTotalMetricReportId, day_index}][1] = {
      {"component_D", 0u, 25}, {"component_D", 1u, 5}};
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kStreamingTimeTotalMetricReportId, day_index}][7] = {
      {"component_D", 0u, 25}, {"component_D", 1u, 5}};
  // The 7-day minimum value for the StreamingTime metric is 0 for all event
  // codes and components, so we don't expect a PerDeviceNumericObservation with
  // a 7-day window for the StreamingTime_PerDeviceMin report.
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kStreamingTimeMinMetricReportId, day_index}][1] = {
      {"component_D", 0u, 10}, {"component_D", 1u, 5}};
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kStreamingTimeMinMetricReportId, day_index}][7] = {
      {"component_D", 0u, 10}, {"component_D", 1u, 5}};
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kStreamingTimeMaxMetricReportId, day_index}][1] = {
      {"component_D", 0u, 15}, {"component_D", 1u, 5}};
  expected_per_device_numeric_obs[{
      testing::per_device_numeric_stats::kStreamingTimeMaxMetricReportId, day_index}][7] = {
      {"component_D", 0u, 15}, {"component_D", 1u, 5}};

  EXPECT_TRUE(CheckPerDeviceNumericObservations(expected_per_device_numeric_obs,
                                                expected_report_participation_obs,
                                                observation_store_.get(), update_recipient_.get()));
}

// Checks that PerDeviceNumericObservations with the expected values are
// generated when some events have been logged for an EVENT_COUNT metric with
// a PER_DEVICE_NUMERIC_STATS report over multiple days and
// GenerateObservations() is called each day, without garbage collection or
// backfill.
//
// Logged events for the SettingsChanged_PerDeviceCount report on the i-th
// day:
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
// Expected PerDeviceNumericObservations for the
// SettingsChanged_PerDeviceNumeric report on the i-th day:
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
// In addition, expect 1 ReportParticipationObservation each day for each of
// the reports in the registry.
TEST_F(PerDeviceNumericEventAggregatorTest, CheckObservationValuesMultiDay) {
  auto start_day_index = CurrentDayIndex();
  const auto& expected_id = testing::per_device_numeric_stats::kSettingsChangedMetricReportId;
  const auto& expected_params = testing::per_device_numeric_stats::kExpectedAggregationParams;
  // Form expected Observations for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedPerDeviceNumericObservations> expected_per_device_numeric_obs(num_days);
  std::vector<ExpectedReportParticipationObservations> expected_report_participation_obs(num_days);
  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_report_participation_obs[offset] =
        MakeExpectedReportParticipationObservations(expected_params, start_day_index + offset);
  }
  expected_per_device_numeric_obs[0] = {};
  expected_per_device_numeric_obs[1][{expected_id, start_day_index + 1}] = {{7, {{"A", 1u, 3}}},
                                                                            {30, {{"A", 1u, 3}}}};
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

  for (uint32_t offset = 0; offset < 1; offset++) {
    auto day_index = CurrentDayIndex();
    for (uint32_t event_code = 1; event_code < 3; event_code++) {
      if (offset > 0 && offset % event_code == 0) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(expected_id, day_index, "A", event_code, 3));
      }
      if (offset > 0 && offset % (2 * event_code) == 0) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(expected_id, day_index, "B", event_code, 2));
      }
    }
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Generate locally aggregated Observations.
    EXPECT_EQ(kOK, GenerateObservations(day_index));
    EXPECT_TRUE(CheckPerDeviceNumericObservations(
        expected_per_device_numeric_obs[offset], expected_report_participation_obs[offset],
        observation_store_.get(), update_recipient_.get()))
        << "offset = " << offset;
    AdvanceClock(kDay);
  }
}

// Repeat the CheckObservationValuesMultiDay test, this time calling
// GarbageCollect() after each call to GenerateObservations.
//
// The logging pattern and set of Observations for each day index is the same
// as in PerDeviceNumericEventAggregatorTest::CheckObservationValuesMultiDay.
// See that test for documentation.
TEST_F(PerDeviceNumericEventAggregatorTest, CheckObservationValuesMultiDayWithGarbageCollection) {
  auto start_day_index = CurrentDayIndex();
  const auto& expected_id = testing::per_device_numeric_stats::kSettingsChangedMetricReportId;
  const auto& expected_params = testing::per_device_numeric_stats::kExpectedAggregationParams;
  // Form expected Observations for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedPerDeviceNumericObservations> expected_per_device_numeric_obs(num_days);
  std::vector<ExpectedReportParticipationObservations> expected_report_participation_obs(num_days);
  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_report_participation_obs[offset] =
        MakeExpectedReportParticipationObservations(expected_params, start_day_index + offset);
  }
  expected_per_device_numeric_obs[0] = {};
  expected_per_device_numeric_obs[1][{expected_id, start_day_index + 1}] = {{7, {{"A", 1u, 3}}},
                                                                            {30, {{"A", 1u, 3}}}};
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

  for (uint32_t offset = 0; offset < 10; offset++) {
    auto day_index = CurrentDayIndex();
    for (uint32_t event_code = 1; event_code < 3; event_code++) {
      if (offset > 0 && offset % event_code == 0) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(expected_id, day_index, "A", event_code, 3));
      }
      if (offset > 0 && offset % (2 * event_code) == 0) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(expected_id, day_index, "B", event_code, 2));
      }
    }
    // Advance |test_clock_| by 1 day.
    AdvanceClock(kDay);
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Generate locally aggregated Observations and garbage-collect the
    // LocalAggregateStore, both for the previous day as measured by
    // |test_clock_|. Back up the LocalAggregateStore and
    // AggregatedObservationHistoryStore.
    DoScheduledTasksNow();
    EXPECT_TRUE(CheckPerDeviceNumericObservations(
        expected_per_device_numeric_obs[offset], expected_report_participation_obs[offset],
        observation_store_.get(), update_recipient_.get()));
  }
}

// Tests that the expected PerDeviceNumericObservations are generated when
// events are logged over multiple days for an EVENT_COUNT
// metric with a PER_DEVICE_NUMERIC_STATS report, when Observations are
// backfilled for some days during that period, without any garbage-collection
// of the LocalAggregateStore.
//
// The logging pattern and set of Observations for each day index is the same
// as in PerDeviceNumericEventAggregatorTest::CheckObservationValuesMultiDay.
// See that test for documentation.
TEST_F(PerDeviceNumericEventAggregatorTest, CheckObservationValuesWithBackfill) {
  auto start_day_index = CurrentDayIndex();
  const auto& expected_id = testing::per_device_numeric_stats::kSettingsChangedMetricReportId;
  const auto& expected_params = testing::per_device_numeric_stats::kExpectedAggregationParams;
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log events for 9 days. Call GenerateObservations() on the first 6 day
  // indices, and the 9th.
  uint32_t num_days = 9;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    ResetObservationStore();
    for (uint32_t event_code = 1; event_code < 3; event_code++) {
      if (offset > 0 && (offset % event_code == 0)) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(expected_id, day_index, "A", event_code, 3));
      }
      if (offset > 0 && offset % (2 * event_code) == 0) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(expected_id, day_index, "B", event_code, 2));
      }
    }
    if (offset < 6 || offset == 8) {
      EXPECT_EQ(kOK, GenerateObservations(day_index));
    }
    // Make the set of Observations which are expected to be generated on
    // |start_day_index + offset| and check it against the contents of the
    // FakeObservationStore.
    ExpectedPerDeviceNumericObservations expected_per_device_numeric_obs;
    ExpectedReportParticipationObservations expected_report_participation_obs;
    switch (offset) {
      case 0: {
        for (uint32_t day_index = start_day_index - backfill_days; day_index <= start_day_index;
             day_index++) {
          for (const auto& pair :
               MakeExpectedReportParticipationObservations(expected_params, day_index)) {
            expected_report_participation_obs.insert(pair);
          }
        }
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 1: {
        expected_per_device_numeric_obs[{expected_id, day_index}] = {{7, {{"A", 1u, 3}}},
                                                                     {30, {{"A", 1u, 3}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 2: {
        expected_per_device_numeric_obs[{expected_id, day_index}] = {
            {7, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {30, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 3: {
        expected_per_device_numeric_obs[{expected_id, day_index}] = {
            {7, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {30, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 4: {
        expected_per_device_numeric_obs[{expected_id, day_index}] = {
            {7, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 5: {
        expected_per_device_numeric_obs[{expected_id, day_index}] = {
            {7, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 8: {
        expected_per_device_numeric_obs[{expected_id, start_day_index + 6}] = {
            {7, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{expected_id, start_day_index + 7}] = {
            {7, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{expected_id, start_day_index + 8}] = {
            {7, {{"A", 1u, 21}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}},
            {30, {{"A", 1u, 24}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}}};
        for (uint32_t day_index = start_day_index + 6; day_index <= start_day_index + 8;
             day_index++) {
          for (const auto& pair :
               MakeExpectedReportParticipationObservations(expected_params, day_index)) {
            expected_report_participation_obs.insert(pair);
          }
        }
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      default:
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
    }
    AdvanceClock(kDay);
  }
}

// Tests that the expected Observations are generated for
// PerDeviceNumericStats reports when events are logged for over multiple days
// for an EVENT_COUNT metric with a PER_DEVICE_NUMERIC_STATS report, when
// Observations are backfilled for some days during that period, and when the
// LocalAggregatedStore is garbage-collected after each call to
// GenerateObservations().
//
// The logging pattern and set of Observations for each day index is the same
// as in PerDeviceNumericEventAggregatorTest::CheckObservationValuesMultiDay.
// See that test for documentation.
TEST_F(PerDeviceNumericEventAggregatorTest, EventCountCheckObservationValuesWithBackfillAndGc) {
  auto start_day_index = CurrentDayIndex();
  const auto& expected_id = testing::per_device_numeric_stats::kSettingsChangedMetricReportId;
  const auto& expected_params = testing::per_device_numeric_stats::kExpectedAggregationParams;
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log events for 9 days. Call GenerateObservations() on the first 6 day
  // indices, and the 9th.
  uint32_t num_days = 9;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    ResetObservationStore();
    for (uint32_t event_code = 1; event_code < 3; event_code++) {
      if (offset > 0 && (offset % event_code == 0)) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(expected_id, day_index, "A", event_code, 3));
      }
      if (offset > 0 && offset % (2 * event_code) == 0) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(expected_id, day_index, "B", event_code, 2));
      }
    }
    // Advance |test_clock_| by 1 day.
    AdvanceClock(kDay);
    if (offset < 6 || offset == 8) {
      // Generate Observations and garbage-collect, both for the previous day
      // index according to |test_clock_|. Back up the LocalAggregateStore and
      // the AggregatedObservationHistoryStore.
      DoScheduledTasksNow();
    }
    // Make the set of Observations which are expected to be generated on
    // |start_day_index + offset| and check it against the contents of the
    // FakeObservationStore.
    ExpectedPerDeviceNumericObservations expected_per_device_numeric_obs;
    ExpectedReportParticipationObservations expected_report_participation_obs;
    switch (offset) {
      case 0: {
        for (uint32_t day_index = start_day_index - backfill_days; day_index <= start_day_index;
             day_index++) {
          for (const auto& pair :
               MakeExpectedReportParticipationObservations(expected_params, day_index)) {
            expected_report_participation_obs.insert(pair);
          }
        }
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 1: {
        expected_per_device_numeric_obs[{expected_id, day_index}] = {{7, {{"A", 1u, 3}}},
                                                                     {30, {{"A", 1u, 3}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 2: {
        expected_per_device_numeric_obs[{expected_id, day_index}] = {
            {7, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {30, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 3: {
        expected_per_device_numeric_obs[{expected_id, day_index}] = {
            {7, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {30, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 4: {
        expected_per_device_numeric_obs[{expected_id, day_index}] = {
            {7, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 5: {
        expected_per_device_numeric_obs[{expected_id, day_index}] = {
            {7, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 8: {
        expected_per_device_numeric_obs[{expected_id, start_day_index + 6}] = {
            {7, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{expected_id, start_day_index + 7}] = {
            {7, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{expected_id, start_day_index + 8}] = {
            {7, {{"A", 1u, 21}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}},
            {30, {{"A", 1u, 24}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}}};
        for (uint32_t day_index = start_day_index + 6; day_index <= start_day_index + 8;
             day_index++) {
          for (const auto& pair :
               MakeExpectedReportParticipationObservations(expected_params, day_index)) {
            expected_report_participation_obs.insert(pair);
          }
        }
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      default:
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
    }
  }
}

// Tests that the expected Observations are generated for
// PerDeviceNumericStats reports when events are logged for over multiple days
// for an ELAPSED_TIME metric with PER_DEVICE_NUMERIC_STATS reports with
// multiple aggregation types, when Observations are backfilled for some days
// during that period, and when the LocalAggregatedStore is garbage-collected
// after each call to GenerateObservations().
//
// Logged events for the StreamingTime_PerDevice{Total, Min, Max} reports on the
// i-th day:
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
TEST_F(PerDeviceNumericEventAggregatorTest, ElapsedTimeCheckObservationValuesWithBackfillAndGc) {
  auto start_day_index = CurrentDayIndex();
  const auto& total_report_id =
      testing::per_device_numeric_stats::kStreamingTimeTotalMetricReportId;
  const auto& min_report_id = testing::per_device_numeric_stats::kStreamingTimeMinMetricReportId;
  const auto& max_report_id = testing::per_device_numeric_stats::kStreamingTimeMaxMetricReportId;
  std::vector<MetricReportId> streaming_time_ids = {total_report_id, min_report_id, max_report_id};
  const auto& expected_params = testing::per_device_numeric_stats::kExpectedAggregationParams;
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log events for 9 days. Call GenerateObservations() on the first 6 day
  // indices, and the 9th.
  uint32_t num_days = 9;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    ResetObservationStore();
    for (uint32_t event_code = 1; event_code < 3; event_code++) {
      for (const auto& report_id : streaming_time_ids) {
        if (offset > 0 && (offset % event_code == 0)) {
          EXPECT_EQ(kOK, LogPerDeviceElapsedTimeEvent(report_id, day_index, "A", event_code, 3));
        }
        if (offset > 0 && offset % (2 * event_code) == 0) {
          EXPECT_EQ(kOK, LogPerDeviceElapsedTimeEvent(report_id, day_index, "B", event_code, 2));
        }
      }
    }

    // Advance |test_clock_| by 1 day.
    AdvanceClock(kDay);
    if (offset < 6 || offset == 8) {
      // Generate Observations and garbage-collect, both for the previous day
      // index according to |test_clock_|. Back up the LocalAggregateStore and
      // the AggregatedObservationHistoryStore.
      DoScheduledTasksNow();
    }
    // Make the set of Observations which are expected to be generated on
    // |start_day_index + offset| and check it against the contents of the
    // FakeObservationStore.
    ExpectedPerDeviceNumericObservations expected_per_device_numeric_obs;
    ExpectedReportParticipationObservations expected_report_participation_obs;
    switch (offset) {
      case 0: {
        for (uint32_t day_index = start_day_index - backfill_days; day_index <= start_day_index;
             day_index++) {
          for (const auto& pair :
               MakeExpectedReportParticipationObservations(expected_params, day_index)) {
            expected_report_participation_obs.insert(pair);
          }
        }
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 1: {
        expected_per_device_numeric_obs[{total_report_id, day_index}] = {{1, {{"A", 1u, 3}}},
                                                                         {7, {{"A", 1u, 3}}}};
        expected_per_device_numeric_obs[{min_report_id, day_index}] = {{1, {{"A", 1u, 3}}},
                                                                       {7, {{"A", 1u, 3}}}};
        expected_per_device_numeric_obs[{max_report_id, day_index}] = {{1, {{"A", 1u, 3}}},
                                                                       {7, {{"A", 1u, 3}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()))
            << "day 1";
        break;
      }
      case 2: {
        expected_per_device_numeric_obs[{total_report_id, day_index}] = {
            {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {7, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_per_device_numeric_obs[{min_report_id, day_index}] = {
            {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_per_device_numeric_obs[{max_report_id, day_index}] = {
            {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 3: {
        expected_per_device_numeric_obs[{total_report_id, day_index}] = {
            {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_per_device_numeric_obs[{min_report_id, day_index}] = {
            {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_per_device_numeric_obs[{max_report_id, day_index}] = {
            {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 4: {
        expected_per_device_numeric_obs[{total_report_id, day_index}] = {
            {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
            {7, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{min_report_id, day_index}] = {
            {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
            {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{max_report_id, day_index}] = {
            {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
            {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 5: {
        expected_per_device_numeric_obs[{total_report_id, day_index}] = {
            {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{min_report_id, day_index}] = {
            {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{max_report_id, day_index}] = {
            {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(expected_params, day_index);
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 8: {
        expected_per_device_numeric_obs[{total_report_id, start_day_index + 6}] = {
            {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {7, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{total_report_id, start_day_index + 7}] = {
            {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{total_report_id, start_day_index + 8}] = {
            {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
            {7, {{"A", 1u, 21}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}}};

        expected_per_device_numeric_obs[{min_report_id, start_day_index + 6}] = {
            {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{min_report_id, start_day_index + 7}] = {
            {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{min_report_id, start_day_index + 8}] = {
            {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
            {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};

        expected_per_device_numeric_obs[{max_report_id, start_day_index + 6}] = {
            {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{max_report_id, start_day_index + 7}] = {
            {1, {{"A", 1u, 3}}}, {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};
        expected_per_device_numeric_obs[{max_report_id, start_day_index + 8}] = {
            {1, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}},
            {7, {{"A", 1u, 3}, {"A", 2u, 3}, {"B", 1u, 2}, {"B", 2u, 2}}}};

        for (uint32_t day_index = start_day_index + 6; day_index <= start_day_index + 8;
             day_index++) {
          for (const auto& pair :
               MakeExpectedReportParticipationObservations(expected_params, day_index)) {
            expected_report_participation_obs.insert(pair);
          }
        }
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      default:
        EXPECT_TRUE(CheckPerDeviceNumericObservations(
            expected_per_device_numeric_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
    }
  }
}

// Tests GenerateObservations() and GarbageCollect() in the case where the
// LocalAggregateStore contains aggregates for metrics with both UTC and LOCAL
// time zone policies, and where the day index in local time may be less than
// the day index in UTC.
TEST_F(NoiseFreeMixedTimeZoneEventAggregatorTest, LocalBeforeUTC) {
  std::vector<ExpectedUniqueActivesObservations> expected_obs(3);
  // Begin at a time when the current day index is the same in both UTC and
  // local time. Log 1 event for event code 0 for each of the 2 reports, then
  // generate Observations and garbage-collect for the previous day index in
  // each of UTC and local time.
  auto start_day_index = CurrentDayIndex();
  LogUniqueActivesEvent(testing::mixed_time_zone::kDeviceBootsMetricReportId, start_day_index, 0u);
  LogUniqueActivesEvent(testing::mixed_time_zone::kFeaturesActiveMetricReportId, start_day_index,
                        0u);
  GenerateObservations(start_day_index - 1, start_day_index - 1);
  GarbageCollect(start_day_index - 1, start_day_index - 1);
  // Form the expected contents of the FakeObservationStore.
  // Since no events were logged on the previous day and no Observations have
  // been generated for that day yet, expect Observations of non-activity for
  // all event codes, for both reports.
  expected_obs[0] = MakeNullExpectedUniqueActivesObservations(
      testing::mixed_time_zone::kExpectedAggregationParams, start_day_index - 1);
  EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs[0], observation_store_.get(),
                                             update_recipient_.get()));
  ResetObservationStore();
  // Advance the day index in UTC, but not in local time, and log 1 event for
  // event code 1 for each of the 2 reports. Generate Observations and
  // garbage-collect for the previous day in each of UTC and local time.
  LogUniqueActivesEvent(testing::mixed_time_zone::kDeviceBootsMetricReportId, start_day_index, 1u);
  LogUniqueActivesEvent(testing::mixed_time_zone::kFeaturesActiveMetricReportId,
                        start_day_index + 1, 1u);
  GenerateObservations(start_day_index, start_day_index - 1);
  GarbageCollect(start_day_index, start_day_index - 1);
  // Form the expected contents of the FakeObservationStore. Since
  // Observations have already been generated for the
  // DeviceBoots_UniqueDevices report for |start_day_index - 1|, expect no
  // Observations for that report.
  expected_obs[1][{testing::mixed_time_zone::kFeaturesActiveMetricReportId, start_day_index}] = {
      {1, {true, false, false}}};
  EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs[1], observation_store_.get(),
                                             update_recipient_.get()));
  ResetObservationStore();
  // Advance the day index in local time so that it is equal to the day index
  // in UTC. Log 1 event for event code 2 for each of the 2 reports, then
  // generate Observations and garbage-collect for the previous day in each of
  // UTC and local time.
  LogUniqueActivesEvent(testing::mixed_time_zone::kDeviceBootsMetricReportId, start_day_index + 1,
                        2u);
  LogUniqueActivesEvent(testing::mixed_time_zone::kFeaturesActiveMetricReportId,
                        start_day_index + 1, 2u);
  GenerateObservations(start_day_index, start_day_index);
  GarbageCollect(start_day_index, start_day_index);
  // Form the expected contents of the FakeObservationStore. Since
  // Observations have already been generated for the
  // FeaturesActive_UniqueDevices report for day |start_day_index|, expect no
  // Observations for that report.
  expected_obs[2][{testing::mixed_time_zone::kDeviceBootsMetricReportId, start_day_index}] = {
      {1, {true, true, false}}};
  EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs[2], observation_store_.get(),
                                             update_recipient_.get()));
}

// Tests GenerateObservations() and GarbageCollect() in the case where the
// LocalAggregateStore contains aggregates for metrics with both UTC and LOCAL
// time zone policies, and where the day index in UTC may be less than
// the day index in local time.
TEST_F(NoiseFreeMixedTimeZoneEventAggregatorTest, LocalAfterUTC) {
  std::vector<ExpectedUniqueActivesObservations> expected_obs(3);
  // Begin at a time when the current day index is the same in both UTC and
  // local time. Log 1 event for event code 0 for each of the 2 reports, then
  // generate Observations and garbage-collect for the previous day index in
  // each of UTC and local time.
  auto start_day_index = CurrentDayIndex();
  LogUniqueActivesEvent(testing::mixed_time_zone::kDeviceBootsMetricReportId, start_day_index, 0u);
  LogUniqueActivesEvent(testing::mixed_time_zone::kFeaturesActiveMetricReportId, start_day_index,
                        0u);
  GenerateObservations(start_day_index - 1, start_day_index - 1);
  GarbageCollect(start_day_index - 1, start_day_index - 1);
  // Form the expected contents of the FakeObservationStore.
  // Since no events were logged on the previous day and no Observations have
  // been generated for that day yet, expect Observations of non-activity for
  // all event codes, for both reports.
  expected_obs[0] = MakeNullExpectedUniqueActivesObservations(
      testing::mixed_time_zone::kExpectedAggregationParams, start_day_index - 1);
  EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs[0], observation_store_.get(),
                                             update_recipient_.get()));
  ResetObservationStore();
  // Advance the day index in local time, but not in UTC, and log 1 event for
  // event code 1 for each of the 2 reports. Generate Observations and
  // garbage-collect for the previous day in each of UTC and local time.
  LogUniqueActivesEvent(testing::mixed_time_zone::kDeviceBootsMetricReportId, start_day_index + 1,
                        1u);
  LogUniqueActivesEvent(testing::mixed_time_zone::kFeaturesActiveMetricReportId, start_day_index,
                        1u);
  GenerateObservations(start_day_index - 1, start_day_index);
  GarbageCollect(start_day_index - 1, start_day_index);
  // Form the expected contents of the FakeObservationStore. Since
  // Observations have already been generated for the
  // FeaturesActive_UniqueDevices report for |start_day_index - 1|, expect no
  // Observations for that report.
  expected_obs[1][{testing::mixed_time_zone::kDeviceBootsMetricReportId, start_day_index}] = {
      {1, {true, false, false}}};
  EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs[1], observation_store_.get(),
                                             update_recipient_.get()));
  ResetObservationStore();
  // Advance the day index in UTC so that it is equal to the day index in
  // local time. Log 1 event for event code 2 for each of the 2 reports, then
  // generate Observations and garbage-collect for the previous day in each of
  // UTC and local time.
  LogUniqueActivesEvent(testing::mixed_time_zone::kDeviceBootsMetricReportId, start_day_index + 1,
                        2u);
  LogUniqueActivesEvent(testing::mixed_time_zone::kFeaturesActiveMetricReportId,
                        start_day_index + 1, 2u);
  GenerateObservations(start_day_index, start_day_index);
  GarbageCollect(start_day_index, start_day_index);
  // Form the expected contents of the FakeObservationStore. Since
  // Observations have already been generated for the
  // DeviceBoots_UniqueDevices report for day |start_day_index|, expect no
  // Observations for that report.
  expected_obs[2][{testing::mixed_time_zone::kFeaturesActiveMetricReportId, start_day_index}] = {
      {1, {true, true, false}}};
  EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs[2], observation_store_.get(),
                                             update_recipient_.get()));
}

// Starts the worker thread, and destructs the EventAggregator without
// explicitly shutting down the worker thread. Checks that the shutdown flag
// and worker thread are in the expected states before and after the thread is
// started.
TEST_F(EventAggregatorWorkerTest, StartWorkerThread) {
  EXPECT_TRUE(in_shutdown_state());
  event_aggregator_->Start(
      std::make_unique<IncrementingClock>(std::chrono::system_clock::duration(0)));
  EXPECT_TRUE(in_run_state());
}

// Starts the worker thread, shuts down the worker thread, and destructs the
// EventAggregator. Checks that the shutdown flag and worker thread are in the
// expected states.
TEST_F(EventAggregatorWorkerTest, StartAndShutDownWorkerThread) {
  EXPECT_TRUE(in_shutdown_state());
  event_aggregator_->Start(
      std::make_unique<IncrementingClock>(std::chrono::system_clock::duration(0)));
  EXPECT_TRUE(in_run_state());
  ShutDownWorkerThread();
  EXPECT_TRUE(in_shutdown_state());
}

// Starts the worker thread and immediately shuts it down. Checks that the
// LocalAggregateStore was backed up during shutdown.
TEST_F(EventAggregatorWorkerTest, BackUpBeforeShutdown) {
  event_aggregator_->Start(
      std::make_unique<IncrementingClock>(std::chrono::system_clock::duration(0)));
  ShutDownWorkerThread();
  EXPECT_EQ(1, local_aggregate_proto_store_->write_count_);
}

// Starts the worker thread and calls
// EventAggregator::UpdateAggregationConfigs() on the main thread.
TEST_F(EventAggregatorWorkerTest, UpdateAggregationConfigs) {
  event_aggregator_->Start(
      std::make_unique<IncrementingClock>(std::chrono::system_clock::duration(0)));
  // Provide the EventAggregator with the all_report_types registry.
  auto project_context = GetTestProject(testing::all_report_types::kCobaltRegistryBase64);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in the
  // all_report_types registry.
  EXPECT_EQ(testing::all_report_types::kExpectedAggregationParams.metric_report_ids.size(),
            CopyLocalAggregateStore().by_report_key().size());
}

// Starts the worker thread, provides a ProjectContext, logs some events, and
// shuts down the worker thread. Checks that the LocalAggregateStore was
// backed up at least once during the lifetime of the worker thread.
TEST_F(EventAggregatorWorkerTest, LogEvents) {
  auto day_index = CurrentDayIndex();
  event_aggregator_->Start(
      std::make_unique<IncrementingClock>(std::chrono::system_clock::duration(0)));
  // Provide the EventAggregator with the all_report_types registry.
  auto project_context = GetTestProject(testing::all_report_types::kCobaltRegistryBase64);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*project_context));
  // Log some events.
  LoggedActivity logged_activity;
  EXPECT_EQ(kOK, LogUniqueActivesEvent(*project_context,
                                       testing::all_report_types::kDeviceBootsMetricReportId,
                                       day_index, 0u, &logged_activity));
  EXPECT_EQ(kOK, LogUniqueActivesEvent(*project_context,
                                       testing::all_report_types::kFeaturesActiveMetricReportId,
                                       day_index, 4u, &logged_activity));
  EXPECT_EQ(kOK, LogUniqueActivesEvent(*project_context,
                                       testing::all_report_types::kEventsOccurredMetricReportId,
                                       day_index, 1u, &logged_activity));
  EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, day_index));
  ShutDownWorkerThread();
  EXPECT_GE(local_aggregate_proto_store_->write_count_, 1);
}

}  // namespace logger
}  // namespace cobalt

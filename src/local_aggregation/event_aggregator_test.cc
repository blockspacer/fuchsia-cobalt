// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/local_aggregation/event_aggregator.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include "src/lib/util/clock.h"
#include "src/lib/util/datetime_util.h"
#include "src/lib/util/proto_util.h"
#include "src/lib/util/testing/test_with_files.h"
#include "src/local_aggregation/aggregation_utils.h"
#include "src/local_aggregation/event_aggregator_mgr.h"
#include "src/local_aggregation/test_utils/test_event_aggregator_mgr.h"
#include "src/logger/logger_test_utils.h"
#include "src/logger/testing_constants.h"
#include "src/pb/event.pb.h"
#include "src/registry/packed_event_codes.h"
#include "src/registry/project_configs.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::local_aggregation {

using config::PackEventCodes;
using logger::Encoder;
using logger::EventRecord;
using logger::kInvalidArguments;
using logger::kOK;
using logger::MetricReportId;
using logger::ObservationWriter;
using logger::ProjectContext;
using logger::Status;
using logger::testing::ExpectedUniqueActivesObservations;
using logger::testing::FakeObservationStore;
using logger::testing::GetTestProject;
using logger::testing::MakeAggregationConfig;
using logger::testing::MakeAggregationKey;
using logger::testing::TestUpdateRecipient;
using system_data::ClientSecret;
using system_data::SystemDataInterface;
using util::EncryptedMessageMaker;
using util::IncrementingSteadyClock;
using util::IncrementingSystemClock;
using util::SerializeToBase64;
using util::TimeToDayIndex;

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
class EventAggregatorTest : public util::testing::TestWithFiles {
 protected:
  void SetUp() override {
    MakeTestFolder();
    observation_store_ = std::make_unique<FakeObservationStore>();
    update_recipient_ = std::make_unique<TestUpdateRecipient>();
    observation_encrypter_ = EncryptedMessageMaker::MakeUnencrypted();
    observation_writer_ = std::make_unique<ObservationWriter>(
        observation_store_.get(), update_recipient_.get(), observation_encrypter_.get());
    encoder_ = std::make_unique<Encoder>(system_data::ClientSecret::GenerateNewSecret(),
                                         system_data_.get());
    ResetEventAggregator();
  }

  void ResetEventAggregator() {
    CobaltConfig cfg = {.client_secret = system_data::ClientSecret::GenerateNewSecret()};

    cfg.local_aggregation_backfill_days = 0;
    cfg.local_aggregate_proto_store_path = aggregate_store_path();
    cfg.obs_history_proto_store_path = obs_history_path();

    event_aggregator_mgr_ = std::make_unique<TestEventAggregatorManager>(cfg, fs(), encoder_.get(),
                                                                         observation_writer_.get());
    // Pass this clock to the EventAggregator::Start method, if it is called.
    test_clock_ = std::make_unique<IncrementingSystemClock>(std::chrono::system_clock::duration(0));
    // Initilize it to 10 years after the beginning of time.
    test_clock_->set_time(std::chrono::system_clock::time_point(std::chrono::seconds(10 * kYear)));
    // Use this to advance the clock in the tests.
    unowned_test_clock_ = test_clock_.get();
    day_store_created_ = CurrentDayIndex();
    test_steady_clock_ = new IncrementingSteadyClock(std::chrono::system_clock::duration(0));
    event_aggregator_mgr_->SetSteadyClock(test_steady_clock_);
  }

  // Destruct the EventAggregator (thus calling EventAggregator::ShutDown())
  // before destructing the objects which the EventAggregator points to but does
  // not own.
  void TearDown() override { event_aggregator_mgr_.reset(); }

  // Advances |test_clock_| by |num_seconds| seconds.
  void AdvanceClock(int num_seconds) {
    unowned_test_clock_->increment_by(std::chrono::seconds(num_seconds));
    test_steady_clock_->increment_by(std::chrono::seconds(num_seconds));
  }

  // Returns the day index of the current day according to |test_clock_|, in
  // |time_zone|, without incrementing the clock.
  uint32_t CurrentDayIndex(MetricDefinition::TimeZonePolicy time_zone = MetricDefinition::UTC) {
    return TimeToDayIndex(std::chrono::system_clock::to_time_t(unowned_test_clock_->peek_now()),
                          time_zone);
  }

  size_t GetBackfillDays() { return event_aggregator_mgr_->aggregate_store_->backfill_days_; }

  LocalAggregateStore CopyLocalAggregateStore() {
    return event_aggregator_mgr_->aggregate_store_->CopyLocalAggregateStore();
  }

  void TriggerAndWaitForDoScheduledTasks() {
    {
      // Acquire the lock to manually trigger the scheduled tasks.
      auto locked = event_aggregator_mgr_->protected_worker_thread_controller_.lock();
      locked->immediate_run_trigger = true;
      locked->wakeup_notifier.notify_all();
    }
    while (true) {
      // Reacquire the lock to make sure that the scheduled tasks have completed.
      auto locked = event_aggregator_mgr_->protected_worker_thread_controller_.lock();
      if (!locked->immediate_run_trigger) {
        break;
      }
      std::this_thread::yield();
    }
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
  // and an event code, logs an EventOccurredEvent to the EventAggregator for
  // that report, day index, and event code. If a non-null LoggedActivity map is
  // provided, updates the map with information about the logged Event.
  Status AddUniqueActivesEvent(std::shared_ptr<const ProjectContext> project_context,
                               const MetricReportId& metric_report_id, uint32_t day_index,
                               uint32_t event_code, LoggedActivity* logged_activity = nullptr) {
    EventRecord event_record(std::move(project_context), metric_report_id.first);
    event_record.event()->set_day_index(day_index);
    event_record.event()->mutable_event_occurred_event()->set_event_code(event_code);
    auto status = event_aggregator_mgr_->GetEventAggregator()->AddUniqueActivesEvent(
        metric_report_id.second, event_record);
    if (logged_activity == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(MakeAggregationKey(*event_record.project_context(), metric_report_id),
                           &key)) {
      return kInvalidArguments;
    }
    (*logged_activity)[key][event_code].insert(day_index);
    return status;
  }

  // Given a ProjectContext |project_context| and the MetricReportId of an
  // EVENT_COUNT metric with a PER_DEVICE_NUMERIC_STATS report in
  // |project_context|, as well as a day index, a component string, and an event
  // code, logs a EventCountEvent to the EventAggregator for that report, day
  // index, component, and event code. If a non-null LoggedValues map is
  // provided, updates the map with information about the logged Event.
  Status AddPerDeviceEventCountEvent(std::shared_ptr<const ProjectContext> project_context,
                                     const MetricReportId& metric_report_id, uint32_t day_index,
                                     const std::string& component, uint32_t event_code,
                                     int64_t count, LoggedValues* logged_values = nullptr) {
    EventRecord event_record(std::move(project_context), metric_report_id.first);
    event_record.event()->set_day_index(day_index);
    auto event_count_event = event_record.event()->mutable_event_count_event();
    event_count_event->set_component(component);
    event_count_event->add_event_code(event_code);
    event_count_event->set_count(count);
    auto status = event_aggregator_mgr_->GetEventAggregator()->AddEventCountEvent(
        metric_report_id.second, event_record);
    if (logged_values == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(MakeAggregationKey(*event_record.project_context(), metric_report_id),
                           &key)) {
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
  Status AddPerDeviceElapsedTimeEvent(std::shared_ptr<const ProjectContext> project_context,
                                      const MetricReportId& metric_report_id, uint32_t day_index,
                                      const std::string& component, uint32_t event_code,
                                      int64_t micros, LoggedValues* logged_values = nullptr) {
    EventRecord event_record(std::move(project_context), metric_report_id.first);
    event_record.event()->set_day_index(day_index);
    auto elapsed_time_event = event_record.event()->mutable_elapsed_time_event();
    elapsed_time_event->set_component(component);
    elapsed_time_event->add_event_code(event_code);
    elapsed_time_event->set_elapsed_micros(micros);
    auto status = event_aggregator_mgr_->GetEventAggregator()->AddElapsedTimeEvent(
        metric_report_id.second, event_record);
    if (logged_values == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(MakeAggregationKey(*event_record.project_context(), metric_report_id),
                           &key)) {
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
  Status AddPerDeviceFrameRateEvent(std::shared_ptr<const ProjectContext> project_context,
                                    const MetricReportId& metric_report_id, uint32_t day_index,
                                    const std::string& component, uint32_t event_code, float fps,
                                    LoggedValues* logged_values = nullptr) {
    EventRecord event_record(std::move(project_context), metric_report_id.first);
    event_record.event()->set_day_index(day_index);
    auto frame_rate_event = event_record.event()->mutable_frame_rate_event();
    frame_rate_event->set_component(component);
    frame_rate_event->add_event_code(event_code);
    int64_t frames_per_1000_seconds = std::round(fps * 1000.0);
    frame_rate_event->set_frames_per_1000_seconds(frames_per_1000_seconds);
    auto status = event_aggregator_mgr_->GetEventAggregator()->AddFrameRateEvent(
        metric_report_id.second, event_record);
    if (logged_values == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(MakeAggregationKey(*event_record.project_context(), metric_report_id),
                           &key)) {
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
  Status AddPerDeviceMemoryUsageEvent(std::shared_ptr<const ProjectContext> project_context,
                                      const MetricReportId& metric_report_id, uint32_t day_index,
                                      const std::string& component,
                                      const std::vector<uint32_t>& event_codes, int64_t bytes,
                                      LoggedValues* logged_values = nullptr) {
    EventRecord event_record(std::move(project_context), metric_report_id.first);
    event_record.event()->set_day_index(day_index);
    auto memory_usage_event = event_record.event()->mutable_memory_usage_event();
    memory_usage_event->set_component(component);
    for (auto event_code : event_codes) {
      memory_usage_event->add_event_code(event_code);
    }
    memory_usage_event->set_bytes(bytes);
    auto status = event_aggregator_mgr_->GetEventAggregator()->AddMemoryUsageEvent(
        metric_report_id.second, event_record);
    if (logged_values == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(MakeAggregationKey(*event_record.project_context(), metric_report_id),
                           &key)) {
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
    auto local_aggregate_store = event_aggregator_mgr_->aggregate_store_->CopyLocalAggregateStore();
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
    auto local_aggregate_store = event_aggregator_mgr_->aggregate_store_->CopyLocalAggregateStore();
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
    uint32_t max_aggregation_days = 1;
    for (const auto& window : config.aggregation_window()) {
      if (window.units_case() == OnDeviceAggregationWindow::kDays &&
          window.days() > max_aggregation_days) {
        max_aggregation_days = window.days();
      }
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

    if (day_last_garbage_collected_ - backfill_days < (max_aggregation_days + 1)) {
      return day_store_created_ - backfill_days;
    }
    return (day_store_created_ < (day_last_garbage_collected_ - max_aggregation_days + 1))
               ? (day_last_garbage_collected_ - backfill_days - max_aggregation_days + 1)
               : day_store_created_ - backfill_days;
  }

  std::unique_ptr<TestEventAggregatorManager> event_aggregator_mgr_;
  std::unique_ptr<ObservationWriter> observation_writer_;
  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;
  std::unique_ptr<TestUpdateRecipient> update_recipient_;
  std::unique_ptr<FakeObservationStore> observation_store_;
  std::unique_ptr<IncrementingSystemClock> test_clock_;
  IncrementingSystemClock* unowned_test_clock_;
  IncrementingSteadyClock* test_steady_clock_;
  // The day index on which the LocalAggregateStore was last
  // garbage-collected. A value of 0 indicates that the store has never been
  // garbage-collected.
  uint32_t day_last_garbage_collected_ = 0u;
  // The day index on which the LocalAggregateStore was created.
  uint32_t day_store_created_ = 0u;

 private:
  std::unique_ptr<system_data::SystemDataInterface> system_data_;
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
    event_aggregator_mgr_->GetEventAggregator()->UpdateAggregationConfigs(*project_context_);
  }

  // Adds an EventOccurredEvent to the local aggregations for the MetricReportId of a locally
  // aggregated report of the ProjectContext. Overrides the method
  // EventAggregatorTest::AddUniqueActivesEvent.
  Status AddUniqueActivesEvent(const MetricReportId& metric_report_id, uint32_t day_index,
                               uint32_t event_code, LoggedActivity* logged_activity = nullptr) {
    return EventAggregatorTest::AddUniqueActivesEvent(project_context_, metric_report_id, day_index,
                                                      event_code, logged_activity);
  }

  // Adds a EventCountEvent to the local aggregations for the MetricReportId of a locally aggregated
  // report of the ProjectContext. Overrides the method
  // EventAggregatorTest::AddPerDeviceEventCountEvent.
  Status AddPerDeviceEventCountEvent(const MetricReportId& metric_report_id, uint32_t day_index,
                                     const std::string& component, uint32_t event_code,
                                     int64_t count, LoggedValues* logged_values = nullptr) {
    return EventAggregatorTest::AddPerDeviceEventCountEvent(
        project_context_, metric_report_id, day_index, component, event_code, count, logged_values);
  }

  // Adds an ElapsedTimeEvent to the local aggregations for the MetricReportId of a locally
  // aggregated report of the ProjectContext. Overrides the method
  // EventAggregatorTest::AddPerDeviceElapsedTimeEvent.
  Status AddPerDeviceElapsedTimeEvent(const MetricReportId& metric_report_id, uint32_t day_index,
                                      const std::string& component, uint32_t event_code,
                                      int64_t micros, LoggedValues* logged_values = nullptr) {
    return EventAggregatorTest::AddPerDeviceElapsedTimeEvent(project_context_, metric_report_id,
                                                             day_index, component, event_code,
                                                             micros, logged_values);
  }

  // Adds a FrameRateEvent to the local aggregations for the MetricReportId of a locally aggregated
  // report of the ProjectContext. Overrides the method
  // EventAggregatorTest::AddPerDeviceFrameRateEvent.
  Status AddPerDeviceFrameRateEvent(const MetricReportId& metric_report_id, uint32_t day_index,
                                    const std::string& component, uint32_t event_code, float fps,
                                    LoggedValues* logged_values = nullptr) {
    return EventAggregatorTest::AddPerDeviceFrameRateEvent(
        project_context_, metric_report_id, day_index, component, event_code, fps, logged_values);
  }

  // Adds a MemoryUsageEvent to the local aggregations for the MetricReportId of a locally
  // aggregated report of the ProjectContext. Overrides the method
  // EventAggregatorTest::AddPerDeviceMemoryUsageEvent.
  Status AddPerDeviceMemoryUsageEvent(const MetricReportId& metric_report_id, uint32_t day_index,
                                      const std::string& component,
                                      const std::vector<uint32_t>& event_codes, int64_t bytes,
                                      LoggedValues* logged_values = nullptr) {
    return EventAggregatorTest::AddPerDeviceMemoryUsageEvent(project_context_, metric_report_id,
                                                             day_index, component, event_codes,
                                                             bytes, logged_values);
  }

 private:
  // A ProjectContext wrapping the MetricDefinitions passed to the
  // constructor in |metric_string|.
  std::shared_ptr<ProjectContext> project_context_;
};

// Creates an EventAggregator and provides it with a ProjectContext generated
// from test_registries/unique_actives_test_registry.yaml. All metrics in this
// registry are of type EVENT_OCCURRED and have a UNIQUE_N_DAY_ACTIVES report.
class UniqueActivesEventAggregatorTest : public EventAggregatorTestWithProjectContext {
 protected:
  UniqueActivesEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(
            logger::testing::unique_actives::kCobaltRegistryBase64) {}
};

// Creates an EventAggregator and provides it with a ProjectContext generated
// from test_registries/unique_actives_noise_free_test_registry.yaml. All
// metrics in this registry are of type EVENT_OCCURRED and have a
// UNIQUE_N_DAY_ACTIVES report with local_privacy_noise_level NONE.
class UniqueActivesNoiseFreeEventAggregatorTest : public EventAggregatorTestWithProjectContext {
 protected:
  UniqueActivesNoiseFreeEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(
            logger::testing::unique_actives_noise_free::kCobaltRegistryBase64) {}
};

// Creates an EventAggregator and provides it with a ProjectContext generated
// from test_registries/per_device_numeric_stats_test_registry.yaml. All metrics
// in this registry are of type EVENT_COUNT and have a PER_DEVICE_NUMERIC_STATS
// report.
class PerDeviceNumericEventAggregatorTest : public EventAggregatorTestWithProjectContext {
 protected:
  PerDeviceNumericEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(
            logger::testing::per_device_numeric_stats::kCobaltRegistryBase64) {}
};

class PerDeviceHistogramEventAggregatorTest : public EventAggregatorTestWithProjectContext {
 protected:
  PerDeviceHistogramEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(
            logger::testing::per_device_histogram::kCobaltRegistryBase64) {}
};

class EventAggregatorWorkerTest : public EventAggregatorTest {
 protected:
  void SetUp() override { EventAggregatorTest::SetUp(); }

  void ShutDownWorkerThread() { event_aggregator_mgr_->ShutDown(); }

  bool in_shutdown_state() { return (shutdown_flag_set() && !worker_joinable()); }

  bool in_run_state() { return (!shutdown_flag_set() && worker_joinable()); }

  bool shutdown_flag_set() {
    return event_aggregator_mgr_->protected_worker_thread_controller_.const_lock()->shut_down;
  }

  bool worker_joinable() { return event_aggregator_mgr_->worker_thread_.joinable(); }
};

// Tests that an empty LocalAggregateStore is updated with
// ReportAggregationKeys and AggregationConfigs as expected when
// EventAggregator::UpdateAggregationConfigs is called with a ProjectContext
// containing at least one report for each locally aggregated report type.
TEST_F(EventAggregatorTest, UpdateAggregationConfigs) {
  // Check that the LocalAggregateStore is empty.
  EXPECT_EQ(0u, CopyLocalAggregateStore().by_report_key().size());
  // Provide the unique_actives test registry to the EventAggregator.
  auto unique_actives_project_context =
      GetTestProject(logger::testing::unique_actives::kCobaltRegistryBase64);
  EXPECT_EQ(kOK, event_aggregator_mgr_->GetEventAggregator()->UpdateAggregationConfigs(
                     *unique_actives_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in the unique_actives
  // test registry.
  EXPECT_EQ(logger::testing::unique_actives::kExpectedAggregationParams.metric_report_ids.size(),
            CopyLocalAggregateStore().by_report_key().size());
  // Check that the LocalAggregateStore contains the expected
  // ReportAggregationKey and AggregationConfig for each locally aggregated
  // report in the unique_actives registry.
  for (const auto& metric_report_id :
       logger::testing::unique_actives::kExpectedAggregationParams.metric_report_ids) {
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
      GetTestProject(logger::testing::unique_actives::kCobaltRegistryBase64);
  EXPECT_EQ(kOK, event_aggregator_mgr_->GetEventAggregator()->UpdateAggregationConfigs(
                     *unique_actives_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in the unique_actives
  // test registry.
  EXPECT_EQ(logger::testing::unique_actives::kExpectedAggregationParams.metric_report_ids.size(),
            CopyLocalAggregateStore().by_report_key().size());
  // Provide the unique_actives_noise_free test registry to the EventAggregator.
  auto unique_actives_noise_free_project_context =
      GetTestProject(logger::testing::unique_actives_noise_free::kCobaltRegistryBase64);
  EXPECT_EQ(kOK, event_aggregator_mgr_->GetEventAggregator()->UpdateAggregationConfigs(
                     *unique_actives_noise_free_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of distinct MetricReportIds of locally
  // aggregated reports in the union of the unique_actives and
  // unique_actives_noise_free registries.
  auto local_aggregate_store = CopyLocalAggregateStore();
  EXPECT_EQ(5u, local_aggregate_store.by_report_key().size());
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
  EXPECT_TRUE(SerializeToBase64(
      MakeAggregationKey(*unique_actives_project_context,
                         logger::testing::unique_actives::kFeaturesActiveMetricReportId),
      &key));
  auto unique_actives_config =
      MakeAggregationConfig(*unique_actives_project_context,
                            logger::testing::unique_actives::kFeaturesActiveMetricReportId);
  auto report_aggregates = local_aggregate_store.by_report_key().find(key);
  EXPECT_NE(local_aggregate_store.by_report_key().end(), report_aggregates);
  EXPECT_EQ(SerializeAsStringDeterministic(unique_actives_config),
            SerializeAsStringDeterministic(report_aggregates->second.aggregation_config()));
  auto noise_free_config = MakeAggregationConfig(
      *unique_actives_noise_free_project_context,
      logger::testing::unique_actives_noise_free::kFeaturesActiveMetricReportId);
  EXPECT_NE(SerializeAsStringDeterministic(noise_free_config),
            SerializeAsStringDeterministic(report_aggregates->second.aggregation_config()));
}

// Tests that EventAggregator::Log*Event returns |kInvalidArguments| when
// passed a report ID which is not associated to a key of the
// LocalAggregateStore, or when passed an EventRecord containing an Event
// proto message which is not of the appropriate event type.
TEST_F(EventAggregatorTest, LogBadEvents) {
  // Provide the unique_actives test registry to the EventAggregator.
  std::shared_ptr<ProjectContext> unique_actives_project_context =
      GetTestProject(logger::testing::unique_actives::kCobaltRegistryBase64);
  EXPECT_EQ(kOK, event_aggregator_mgr_->GetEventAggregator()->UpdateAggregationConfigs(
                     *unique_actives_project_context));
  // Attempt to log a UniqueActivesEvent for
  // |kEventsOccurredMetricReportId|, which is not in the unique_actives
  // registry. Check that the result is |kInvalidArguments|.
  std::shared_ptr<ProjectContext> noise_free_project_context =
      GetTestProject(logger::testing::unique_actives_noise_free::kCobaltRegistryBase64);
  EventRecord bad_event_record(noise_free_project_context,
                               logger::testing::unique_actives_noise_free::kEventsOccurredMetricId);
  bad_event_record.event()->set_day_index(CurrentDayIndex());
  bad_event_record.event()->mutable_event_occurred_event()->set_event_code(0u);
  EXPECT_EQ(kInvalidArguments, event_aggregator_mgr_->GetEventAggregator()->AddUniqueActivesEvent(
                                   logger::testing::unique_actives_noise_free::
                                       kEventsOccurredEventsOccurredUniqueDevicesReportId,
                                   bad_event_record));
  // Attempt to call AddUniqueActivesEvent() with a valid metric and report
  // ID, but with an EventRecord wrapping an Event which is not an
  // EventOccurredEvent. Check that the result is |kInvalidArguments|.
  EventRecord bad_event_record2(unique_actives_project_context,
                                logger::testing::unique_actives::kFeaturesActiveMetricId);
  bad_event_record2.event()->mutable_event_count_event();
  EXPECT_EQ(kInvalidArguments,
            event_aggregator_mgr_->GetEventAggregator()->AddUniqueActivesEvent(
                logger::testing::unique_actives::kFeaturesActiveFeaturesActiveUniqueDevicesReportId,
                bad_event_record2));
  // Attempt to call AddPerDeviceEventCountEvent() with a valid metric and report
  // ID, but with an EventRecord wrapping an Event which is not a
  // EventCountEvent. Check that the result is |kInvalidArguments|.
  EventRecord bad_event_record3(
      noise_free_project_context,
      logger::testing::per_device_numeric_stats::kConnectionFailuresMetricReportId.first);
  bad_event_record3.event()->mutable_event_occurred_event();
  EXPECT_EQ(kInvalidArguments, event_aggregator_mgr_->GetEventAggregator()->AddEventCountEvent(
                                   logger::testing::per_device_numeric_stats::
                                       kConnectionFailuresConnectionFailuresPerDeviceCountReportId,
                                   bad_event_record3));
}

// Tests that the LocalAggregateStore is updated as expected when
// EventAggregator::AddUniqueActivesEvent() is called with valid arguments;
// i.e., with a report ID associated to an existing key of the
// LocalAggregateStore, and with an EventRecord which wraps an
// EventOccurredEvent.
//
// Logs some valid events each day for 35 days, checking the contents of the
// LocalAggregateStore each day.
TEST_F(UniqueActivesEventAggregatorTest, LogEvents) {
  LoggedActivity logged_activity;
  uint32_t num_days = 35;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    // Add an event to the local aggregations for the FeaturesActive_UniqueDevices report with event
    // code 0. Check the contents of the LocalAggregateStore.
    auto day_index = CurrentDayIndex();
    EXPECT_EQ(kOK,
              AddUniqueActivesEvent(logger::testing::unique_actives::kFeaturesActiveMetricReportId,
                                    day_index, 0u, &logged_activity));
    EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, day_index));
    // Add another event to the local aggregations for the same report, event code, and day index.
    // Check the contents of the LocalAggregateStore.
    EXPECT_EQ(kOK,
              AddUniqueActivesEvent(logger::testing::unique_actives::kFeaturesActiveMetricReportId,
                                    day_index, 0u, &logged_activity));
    EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, day_index));
    // Add several more events to the local aggregations for various valid reports and event codes.
    // Check the contents of the LocalAggregateStore.
    EXPECT_EQ(kOK,
              AddUniqueActivesEvent(logger::testing::unique_actives::kDeviceBootsMetricReportId,
                                    day_index, 0u, &logged_activity));
    EXPECT_EQ(kOK,
              AddUniqueActivesEvent(logger::testing::unique_actives::kFeaturesActiveMetricReportId,
                                    day_index, 4u, &logged_activity));
    EXPECT_EQ(kOK, AddUniqueActivesEvent(
                       logger::testing::unique_actives::kNetworkActivityWindowSizeMetricReportId,
                       day_index, 1u, &logged_activity));
    EXPECT_EQ(kOK,
              AddUniqueActivesEvent(
                  logger::testing::unique_actives::kNetworkActivityAggregationWindowMetricReportId,
                  day_index, 1u, &logged_activity));
    EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, day_index));
    AdvanceClock(kDay);
  }
}

// Tests that the LocalAggregateStore is updated as expected when
// EventAggregator::AddPerDeviceEventCountEvent() is called with valid arguments;
// i.e., with a report ID associated to an existing key of the
// LocalAggregateStore, and with an EventRecord which wraps a EventCountEvent.
//
// Logs some valid events each day for 35 days, checking the contents of the
// LocalAggregateStore each day.
TEST_F(PerDeviceNumericEventAggregatorTest, LogEvents) {
  LoggedValues logged_values;

  std::vector<MetricReportId> count_metric_report_ids = {
      logger::testing::per_device_numeric_stats::kSettingsChangedWindowSizeMetricReportId,
      logger::testing::per_device_numeric_stats::kSettingsChangedAggregationWindowMetricReportId,
      logger::testing::per_device_numeric_stats::kConnectionFailuresMetricReportId};
  std::vector<MetricReportId> elapsed_time_metric_report_ids = {
      logger::testing::per_device_numeric_stats::kStreamingTimeTotalMetricReportId,
      logger::testing::per_device_numeric_stats::kStreamingTimeMinMetricReportId,
      logger::testing::per_device_numeric_stats::kStreamingTimeMaxMetricReportId};
  MetricReportId frame_rate_metric_report_id =
      logger::testing::per_device_numeric_stats::kLoginModuleFrameRateMinMetricReportId;
  MetricReportId memory_usage_metric_report_id =
      logger::testing::per_device_numeric_stats::kLedgerMemoryUsageMaxMetricReportId;

  uint32_t num_days = 35;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    for (const auto& id : count_metric_report_ids) {
      for (const auto& component : {"component_A", "component_B", "component_C"}) {
        // Adds 2 events to the local aggregations with event code 0, for each component A, B, C.
        EXPECT_EQ(kOK,
                  AddPerDeviceEventCountEvent(id, day_index, component, 0u, 2, &logged_values));
        EXPECT_EQ(kOK,
                  AddPerDeviceEventCountEvent(id, day_index, component, 0u, 3, &logged_values));
      }
      if (offset < 3) {
        // Adds 1 event to the local aggregations for component D and event code 1.
        EXPECT_EQ(kOK,
                  AddPerDeviceEventCountEvent(id, day_index, "component_D", 1u, 4, &logged_values));
      }
    }
    for (const auto& id : elapsed_time_metric_report_ids) {
      for (const auto& component : {"component_A", "component_B", "component_C"}) {
        // Adds 2 events to the local aggregations with event code 0, for each component A, B, C.
        EXPECT_EQ(kOK,
                  AddPerDeviceElapsedTimeEvent(id, day_index, component, 0u, 2, &logged_values));
        EXPECT_EQ(kOK,
                  AddPerDeviceElapsedTimeEvent(id, day_index, component, 0u, 3, &logged_values));
      }
      if (offset < 3) {
        // Adds 1 event to the local aggregations for component D and event code 1.
        EXPECT_EQ(
            kOK, AddPerDeviceElapsedTimeEvent(id, day_index, "component_D", 1u, 4, &logged_values));
      }
    }
    for (const auto& component : {"component_A", "component_B"}) {
      // Adds some events to the local aggregations for a FRAME_RATE metric with a
      // PerDeviceNumericStats report.
      EXPECT_EQ(kOK, AddPerDeviceFrameRateEvent(frame_rate_metric_report_id, day_index, component,
                                                0u, 2.25, &logged_values));
      EXPECT_EQ(kOK, AddPerDeviceFrameRateEvent(frame_rate_metric_report_id, day_index, component,
                                                0u, 1.75, &logged_values));
      // Adds some events to the local aggregations for a MEMORY_USAGE metric with a
      // PerDeviceNumericStats report.
      EXPECT_EQ(kOK,
                AddPerDeviceMemoryUsageEvent(memory_usage_metric_report_id, day_index, component,
                                             std::vector<uint32_t>{0u, 0u}, 300, &logged_values));
      EXPECT_EQ(kOK,
                AddPerDeviceMemoryUsageEvent(memory_usage_metric_report_id, day_index, component,
                                             std::vector<uint32_t>{1u, 0u}, 300, &logged_values));
    }
    EXPECT_TRUE(CheckPerDeviceNumericAggregates(logged_values, day_index));
    AdvanceClock(kDay);
  }
}

// Tests that the LocalAggregateStore is updated as expected when
// EventAggregator::AddPerDeviceEventCountEvent() is called with valid arguments;
// i.e., with a report ID associated to an existing key of the
// LocalAggregateStore, and with an EventRecord which wraps a EventCountEvent.
//
// Logs some valid events each day for 35 days, checking the contents of the
// LocalAggregateStore each day.
TEST_F(PerDeviceHistogramEventAggregatorTest, LogEvents) {
  LoggedValues logged_values;

  auto event_count_id = logger::testing::per_device_histogram::kSettingsChangedMetricReportId;
  auto elapsed_time_id = logger::testing::per_device_histogram::kStreamingTimeTotalMetricReportId;
  auto frame_rate_id =
      logger::testing::per_device_histogram::kLoginModuleFrameRateMinMetricReportId;
  auto memory_usage_id = logger::testing::per_device_histogram::kLedgerMemoryUsageMaxMetricReportId;

  uint32_t num_days = 35;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    for (const auto& component : {"component_A", "component_B", "component_C"}) {
      // Adds 2 events to the local aggregations with event code 0, for each component A, B, C.
      EXPECT_EQ(kOK, AddPerDeviceEventCountEvent(event_count_id, day_index, component, 0u, 2,
                                                 &logged_values));
      EXPECT_EQ(kOK, AddPerDeviceEventCountEvent(event_count_id, day_index, component, 0u, 3,
                                                 &logged_values));
    }
    if (offset < 3) {
      // Adds 1 event to the local aggregations for component D and event code 1.
      EXPECT_EQ(kOK, AddPerDeviceEventCountEvent(event_count_id, day_index, "component_D", 1u, 4,
                                                 &logged_values));
    }

    for (const auto& component : {"component_A", "component_B", "component_C"}) {
      // Adds 2 events to the local aggregations with event code 0, for each component A, B, C.
      EXPECT_EQ(kOK, AddPerDeviceElapsedTimeEvent(elapsed_time_id, day_index, component, 0u, 2,
                                                  &logged_values));
      EXPECT_EQ(kOK, AddPerDeviceElapsedTimeEvent(elapsed_time_id, day_index, component, 0u, 3,
                                                  &logged_values));
    }
    if (offset < 3) {
      // Adds 1 event to the local aggregations for component D and event code 1.
      EXPECT_EQ(kOK, AddPerDeviceElapsedTimeEvent(elapsed_time_id, day_index, "component_D", 1u, 4,
                                                  &logged_values));
    }

    for (const auto& component : {"component_A", "component_B"}) {
      // Adds some events to the local aggregations for a FRAME_RATE metric with a
      // PerDeviceHistogram report.
      EXPECT_EQ(kOK, AddPerDeviceFrameRateEvent(frame_rate_id, day_index, component, 0u, 2.25,
                                                &logged_values));
      EXPECT_EQ(kOK, AddPerDeviceFrameRateEvent(frame_rate_id, day_index, component, 0u, 1.75,
                                                &logged_values));
      // Adds some events to the local aggregations for a MEMORY_USAGE metric with a
      // PerDeviceHistogram report.
      EXPECT_EQ(kOK,
                AddPerDeviceMemoryUsageEvent(memory_usage_id, day_index, component,
                                             std::vector<uint32_t>{0u, 0u}, 300, &logged_values));
      EXPECT_EQ(kOK,
                AddPerDeviceMemoryUsageEvent(memory_usage_id, day_index, component,
                                             std::vector<uint32_t>{1u, 0u}, 300, &logged_values));
    }
    EXPECT_TRUE(CheckPerDeviceNumericAggregates(logged_values, day_index));
    AdvanceClock(kDay);
  }
}

}  // namespace cobalt::local_aggregation

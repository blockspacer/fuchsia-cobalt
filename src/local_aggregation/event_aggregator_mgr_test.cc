// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/local_aggregation/event_aggregator_mgr.h"

#include "src/lib/util/clock.h"
#include "src/lib/util/datetime_util.h"
#include "src/lib/util/proto_util.h"
#include "src/local_aggregation/test_utils/test_event_aggregator_mgr.h"
#include "src/logger/testing_constants.h"
#include "src/pb/event.pb.h"
#include "src/registry/packed_event_codes.h"
#include "src/registry/project_configs.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::local_aggregation {

using logger::Encoder;
using logger::EventRecord;
using logger::kOK;
using logger::MetricReportId;
using logger::ObservationWriter;
using logger::ProjectContext;
using logger::Status;
using logger::testing::ExpectedUniqueActivesObservations;
using logger::testing::FakeObservationStore;
using logger::testing::GetTestProject;
using logger::testing::MakeAggregationKey;
using logger::testing::MakeNullExpectedUniqueActivesObservations;
using logger::testing::MockConsistentProtoStore;
using logger::testing::TestUpdateRecipient;
using logger::testing::all_report_types::kCobaltRegistryBase64;
using logger::testing::all_report_types::kDeviceBootsMetricReportId;
using logger::testing::all_report_types::kEventsOccurredMetricReportId;
using logger::testing::all_report_types::kFeaturesActiveMetricReportId;
using system_data::ClientSecret;
using system_data::SystemDataInterface;
using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;
using util::EncryptedMessageMaker;
using util::IncrementingSteadyClock;
using util::IncrementingSystemClock;
using util::SerializeToBase64;
using util::TimeToDayIndex;

// Number of seconds in a day
constexpr int kSecondsInOneDay = 60 * 60 * 24;
// Number of seconds in an ideal year
constexpr int kYear = kSecondsInOneDay * 365;

// Filenames for constructors of ConsistentProtoStores
constexpr char kAggregateStoreFilename[] = "local_aggregate_store_backup";
constexpr char kObsHistoryFilename[] = "obs_history_backup";

class EventAggregatorManagerTest : public ::testing::Test {
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
  }

  std::unique_ptr<IncrementingSystemClock> GetTestSystemClock() {
    auto test_clock =
        std::make_unique<IncrementingSystemClock>(std::chrono::system_clock::duration(0));
    test_clock->set_time(std::chrono::system_clock::time_point(std::chrono::seconds(10 * kYear)));
    test_system_clock_ptr_ = test_clock.get();
    return test_clock;
  }

  IncrementingSteadyClock* GetTestSteadyClock() {
    test_steady_clock_ptr_ = new IncrementingSteadyClock(std::chrono::system_clock::duration(0));
    return test_steady_clock_ptr_;
  }

  std::unique_ptr<TestEventAggregatorManager> GetEventAggregatorManager(
      IncrementingSteadyClock* steady_clock) {
    auto event_aggregator_mgr_ = std::make_unique<TestEventAggregatorManager>(
        encoder_.get(), observation_writer_.get(), local_aggregate_proto_store_.get(),
        obs_history_proto_store_.get());
    event_aggregator_mgr_->SetSteadyClock(steady_clock);
    return event_aggregator_mgr_;
  }

  void ShutDown(EventAggregatorManager* event_aggregator_mgr) { event_aggregator_mgr->ShutDown(); }

  bool IsInShutdownState(EventAggregatorManager* event_aggregator_mgr) {
    return (IsShutdownFlagSet(event_aggregator_mgr) && !IsWorkerJoinable(event_aggregator_mgr));
  }

  bool IsInRunState(EventAggregatorManager* event_aggregator_mgr) {
    return (!IsShutdownFlagSet(event_aggregator_mgr) && IsWorkerJoinable(event_aggregator_mgr));
  }

  bool IsShutdownFlagSet(EventAggregatorManager* event_aggregator_mgr) {
    return event_aggregator_mgr->protected_worker_thread_controller_.const_lock()->shut_down;
  }

  bool IsWorkerJoinable(EventAggregatorManager* event_aggregator_mgr) {
    return event_aggregator_mgr->worker_thread_.joinable();
  }

  // Returns the day index of the current day according to |test_clock_|, in
  // |time_zone|, without incrementing the clock.
  uint32_t CurrentDayIndex(MetricDefinition::TimeZonePolicy time_zone = MetricDefinition::UTC) {
    return TimeToDayIndex(std::chrono::system_clock::to_time_t(test_system_clock_ptr_->peek_now()),
                          time_zone);
  }

  bool BackUpHappened() { return local_aggregate_proto_store_->write_count_ >= 1; }

  uint32_t NumberOfKVPairsInStore(EventAggregatorManager* event_aggregator_mgr) {
    return event_aggregator_mgr->aggregate_store_->CopyLocalAggregateStore().by_report_key().size();
  }

  // Given a ProjectContext |project_context| and the MetricReportId of a UNIQUE_N_DAY_ACTIVES
  // report in |project_context|, as well as a day index and an event code, adds an OccurrenceEvent
  // to the EventAggregator's AggregateStore for that report, day index, and event code. If a
  // non-null LoggedActivity map is provided, updates the map with information about the logged
  // Event.
  Status AddUniqueActivesEvent(EventAggregatorManager* event_aggregator_mgr,
                               std::shared_ptr<const ProjectContext> project_context,
                               const MetricReportId& metric_report_id, uint32_t day_index,
                               uint32_t event_code) {
    EventRecord event_record(std::move(project_context), metric_report_id.first);
    event_record.event()->set_day_index(day_index);
    event_record.event()->mutable_occurrence_event()->set_event_code(event_code);
    return event_aggregator_mgr->GetEventAggregator()->AddUniqueActivesEvent(
        metric_report_id.second, event_record);
  }

  uint32_t GetNumberOfUniqueActivesAggregates(EventAggregatorManager* event_aggregator_mgr) {
    auto local_aggregate_store = event_aggregator_mgr->aggregate_store_->CopyLocalAggregateStore();
    uint32_t num_aggregates = 0;
    for (const auto& [report_key, aggregates] : local_aggregate_store.by_report_key()) {
      if (aggregates.type_case() != ReportAggregates::kUniqueActivesAggregates) {
        continue;
      }

      for (const auto& [event_code, event_code_aggregates] :
           aggregates.unique_actives_aggregates().by_event_code()) {
        num_aggregates += event_code_aggregates.by_day_index_size();
      }
    }
    return num_aggregates;
  }

  AssertionResult ContainsValidUniqueActivesAggregate(
      EventAggregatorManager* event_aggregator_mgr,
      const std::shared_ptr<const ProjectContext>& project_context,
      const MetricReportId& metric_report_id, uint32_t day_index, uint32_t event_code) {
    auto local_aggregate_store = event_aggregator_mgr->aggregate_store_->CopyLocalAggregateStore();
    std::string key;
    if (!SerializeToBase64(MakeAggregationKey(*project_context, metric_report_id), &key)) {
      return AssertionFailure() << "Could not serialize key with metric id  "
                                << metric_report_id.first << " and report id "
                                << metric_report_id.second;
    }

    auto report_aggregates = local_aggregate_store.by_report_key().find(key);
    if (report_aggregates == local_aggregate_store.by_report_key().end()) {
      return AssertionFailure() << "No report aggregates for key with metric id "
                                << metric_report_id.first << " and report id "
                                << metric_report_id.second;
    }

    if (report_aggregates->second.type_case() != ReportAggregates::kUniqueActivesAggregates) {
      return AssertionFailure() << "Report aggregates for key with metric id "
                                << metric_report_id.first << " and report id "
                                << metric_report_id.second << " are not of type unique actives.";
    }

    auto event_code_aggregates =
        report_aggregates->second.unique_actives_aggregates().by_event_code().find(event_code);
    if (event_code_aggregates ==
        report_aggregates->second.unique_actives_aggregates().by_event_code().end()) {
      return AssertionFailure() << "No report aggregates for key with metric id "
                                << metric_report_id.first << " and report id "
                                << metric_report_id.second << " with event code " << event_code;
    }

    auto day_aggregate = event_code_aggregates->second.by_day_index().find(day_index);
    if (day_aggregate == event_code_aggregates->second.by_day_index().end()) {
      return AssertionFailure() << "No report aggregates for key with metric id "
                                << metric_report_id.first << " and report id "
                                << metric_report_id.second << " and event code " << event_code
                                << " with day index " << day_index;
    }

    return AssertionSuccess();
  }

  void TriggerAndWaitForDoScheduledTasks(EventAggregatorManager* event_aggregator_mgr) {
    {
      // Acquire the lock to manually trigger the scheduled tasks.
      auto locked = event_aggregator_mgr->protected_worker_thread_controller_.lock();
      locked->immediate_run_trigger = true;
      locked->shutdown_notifier.notify_all();
    }
    while (true) {
      // Reacquire the lock to make sure that the scheduled tasks have completed.
      auto locked = event_aggregator_mgr->protected_worker_thread_controller_.lock();
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

  void AdvanceClock(int num_seconds) {
    test_steady_clock_ptr_->increment_by(std::chrono::seconds(num_seconds));
    test_system_clock_ptr_->increment_by(std::chrono::seconds(num_seconds));
  }

 protected:
  IncrementingSystemClock* test_system_clock_ptr_;  // used for determing CurrentDayIndex
  IncrementingSteadyClock* test_steady_clock_ptr_;  // used for scheduling events.

  std::unique_ptr<MockConsistentProtoStore> local_aggregate_proto_store_;
  std::unique_ptr<MockConsistentProtoStore> obs_history_proto_store_;
  std::unique_ptr<ObservationWriter> observation_writer_;
  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;
  std::unique_ptr<TestUpdateRecipient> update_recipient_;
  std::unique_ptr<FakeObservationStore> observation_store_;
  std::unique_ptr<SystemDataInterface> system_data_;
};

TEST_F(EventAggregatorManagerTest, StartWorkerThread) {
  auto event_aggregator_mgr = GetEventAggregatorManager(GetTestSteadyClock());
  EXPECT_TRUE(IsInShutdownState(event_aggregator_mgr.get()));

  event_aggregator_mgr->Start(GetTestSystemClock());
  EXPECT_TRUE(IsInRunState(event_aggregator_mgr.get()));
}

TEST_F(EventAggregatorManagerTest, StartAndShutDownWorkerThread) {
  auto event_aggregator_mgr = GetEventAggregatorManager(GetTestSteadyClock());

  EXPECT_TRUE(IsInShutdownState(event_aggregator_mgr.get()));

  event_aggregator_mgr->Start(GetTestSystemClock());
  EXPECT_TRUE(IsInRunState(event_aggregator_mgr.get()));

  ShutDown(event_aggregator_mgr.get());
  EXPECT_TRUE(IsInShutdownState(event_aggregator_mgr.get()));
}

TEST_F(EventAggregatorManagerTest, BackUpBeforeShutdown) {
  auto event_aggregator_mgr = GetEventAggregatorManager(GetTestSteadyClock());
  event_aggregator_mgr->Start(GetTestSystemClock());

  ShutDown(event_aggregator_mgr.get());
  EXPECT_TRUE(BackUpHappened());
}

// Starts the worker thread and calls
// EventAggregator::UpdateAggregationConfigs() on the main thread.
TEST_F(EventAggregatorManagerTest, UpdateAggregationConfigs) {
  auto event_aggregator_mgr = GetEventAggregatorManager(GetTestSteadyClock());
  event_aggregator_mgr->Start(GetTestSystemClock());

  // Provide the EventAggregator with the all_report_types registry.
  auto project_context = GetTestProject(kCobaltRegistryBase64);
  EXPECT_EQ(kOK,
            event_aggregator_mgr->GetEventAggregator()->UpdateAggregationConfigs(*project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in the
  // all_report_types registry.
  EXPECT_EQ(logger::testing::all_report_types::kExpectedAggregationParams.metric_report_ids.size(),
            NumberOfKVPairsInStore(event_aggregator_mgr.get()));
}

TEST_F(EventAggregatorManagerTest, LogEvents) {
  auto event_aggregator_mgr = GetEventAggregatorManager(GetTestSteadyClock());
  event_aggregator_mgr->Start(GetTestSystemClock());

  auto day_index = CurrentDayIndex();
  // Provide the EventAggregator with the all_report_types registry.
  std::shared_ptr<ProjectContext> project_context = GetTestProject(kCobaltRegistryBase64);
  EXPECT_EQ(kOK,
            event_aggregator_mgr->GetEventAggregator()->UpdateAggregationConfigs(*project_context));

  EXPECT_EQ(kOK, AddUniqueActivesEvent(event_aggregator_mgr.get(), project_context,
                                       kDeviceBootsMetricReportId, day_index, /*event_code*/ 0u));
  EXPECT_EQ(kOK,
            AddUniqueActivesEvent(event_aggregator_mgr.get(), project_context,
                                  kFeaturesActiveMetricReportId, day_index, /*event_code*/ 4u));
  EXPECT_EQ(kOK,
            AddUniqueActivesEvent(event_aggregator_mgr.get(), project_context,
                                  kEventsOccurredMetricReportId, day_index, /*event_code*/ 1u));

  EXPECT_EQ(3, GetNumberOfUniqueActivesAggregates(event_aggregator_mgr.get()));
  EXPECT_TRUE(ContainsValidUniqueActivesAggregate(event_aggregator_mgr.get(), project_context,
                                                  kDeviceBootsMetricReportId, day_index,
                                                  /*event_code*/ 0u));
  EXPECT_TRUE(ContainsValidUniqueActivesAggregate(event_aggregator_mgr.get(), project_context,
                                                  kFeaturesActiveMetricReportId, day_index,
                                                  /*event_code*/ 4u));
  EXPECT_TRUE(ContainsValidUniqueActivesAggregate(event_aggregator_mgr.get(), project_context,
                                                  kEventsOccurredMetricReportId, day_index,
                                                  /*event_code*/ 1u));

  ShutDown(event_aggregator_mgr.get());
  EXPECT_TRUE(BackUpHappened());
}

// Checks that UniqueActivesObservations with the expected values are
// generated by the the scheduled Observation generation when some events
// have been logged for a UNIQUE_N_DAY_ACTIVES.
// (based on UniqueActivesNoiseFreeEventAggregatorTest::CheckObservationValuesMultiDay)
//
// Logging pattern:
// Logs events for the EventsOccurred_UniqueDevices report (whose parent
// metric has max_event_code = 4) for event codes 1 through 4.
//
// Expected number of Observations:
// The call to GenerateObservations should generate a number of Observations
// equal to the daily_num_obs field of
// |logger::testing::unique_actives_noise_free::kExpectedAggregationParams|.
//
// Expected Observation values:
// The EventsOccurred_UniqueDevices report has window sizes 1 and 7, and
// the expected activity indicators of Observations for that report are:
//
// (window size)            active for event codes
// ------------------------------------------------------
// (1)                           1, 2, 3, 4
// (7)                           1, 2, 3, 4
TEST_F(EventAggregatorManagerTest, Run) {
  auto event_aggregator_mgr = GetEventAggregatorManager(GetTestSteadyClock());
  event_aggregator_mgr->Start(GetTestSystemClock());

  auto day_index = CurrentDayIndex();
  std::shared_ptr<const ProjectContext> project_context =
      GetTestProject(logger::testing::unique_actives_noise_free::kCobaltRegistryBase64);

  // Form expected Observations for the 1 day of logging.
  auto expected_obs = MakeNullExpectedUniqueActivesObservations(
      logger::testing::unique_actives_noise_free::kExpectedAggregationParams, day_index);
  const auto& expected_id =
      logger::testing::unique_actives_noise_free::kEventsOccurredMetricReportId;
  expected_obs[{expected_id, day_index}] = {{1, {false, true, true, true, true}},
                                            {7, {false, true, true, true, true}}};

  TriggerAndWaitForDoScheduledTasks(event_aggregator_mgr.get());

  EXPECT_EQ(kOK,
            event_aggregator_mgr->GetEventAggregator()->UpdateAggregationConfigs(*project_context));
  EXPECT_EQ(kOK, AddUniqueActivesEvent(event_aggregator_mgr.get(), project_context, expected_id,
                                       day_index, /*event_code*/ 1));
  EXPECT_EQ(kOK, AddUniqueActivesEvent(event_aggregator_mgr.get(), project_context, expected_id,
                                       day_index, /*event_code*/ 2));
  EXPECT_EQ(kOK, AddUniqueActivesEvent(event_aggregator_mgr.get(), project_context, expected_id,
                                       day_index, /*event_code*/ 3));
  EXPECT_EQ(kOK, AddUniqueActivesEvent(event_aggregator_mgr.get(), project_context, expected_id,
                                       day_index, /*event_code*/ 4));

  AdvanceClock(kSecondsInOneDay);
  ResetObservationStore();

  TriggerAndWaitForDoScheduledTasks(event_aggregator_mgr.get());

  EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs, observation_store_.get(),
                                             update_recipient_.get()));
}

}  // namespace cobalt::local_aggregation

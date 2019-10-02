// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logger/undated_event_manager.h"

#include <memory>
#include <string>

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>

#include "gtest/gtest.h"
#include "src/algorithms/rappor/rappor_encoder.h"
#include "src/lib/util/clock.h"
#include "src/lib/util/datetime_util.h"
#include "src/lib/util/encrypted_message_util.h"
#include "src/logger/encoder.h"
#include "src/logger/event_aggregator.h"
#include "src/logger/fake_logger.h"
#include "src/logger/internal_metrics_config.cb.h"
#include "src/logger/logger_test_utils.h"
#include "src/logger/project_context.h"
#include "src/logger/status.h"
#include "src/logger/testing_constants.h"
#include "src/pb/observation.pb.h"
#include "src/pb/observation2.pb.h"
#include "src/registry/packed_event_codes.h"
#include "src/system_data/client_secret.h"

namespace cobalt {

using encoder::ClientSecret;
using encoder::SystemDataInterface;

using util::EncryptedMessageMaker;
using util::IncrementingSteadyClock;
using util::IncrementingSystemClock;

namespace logger {

using testing::FakeObservationStore;
using testing::GetTestProject;
using testing::MockConsistentProtoStore;
using testing::TestUpdateRecipient;

namespace {
std::tm kSystemTime = {
    .tm_sec = 30,
    .tm_min = 20,
    .tm_hour = 10,
    .tm_mday = 2,    // 2nd
    .tm_mon = 7,     // August
    .tm_year = 119,  // 2019
};
const auto kSystemTimePoint = std::chrono::system_clock::from_time_t(std::mktime(&kSystemTime));
constexpr int kDayIndex = 18110;  // 2019-08-02

// Use a smaller max saved size for testing.
constexpr size_t kMaxSavedEvents = 10;

// Filenames for constructors of ConsistentProtoStores
constexpr char kAggregateStoreFilename[] = "local_aggregate_store_backup";
constexpr char kObsHistoryFilename[] = "obs_history_backup";

}  // namespace

class UndatedEventManagerTest : public ::testing::Test {
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
    event_aggregator_ = std::make_unique<EventAggregator>(encoder_.get(), observation_writer_.get(),
                                                          local_aggregate_proto_store_.get(),
                                                          obs_history_proto_store_.get());
    internal_logger_ = std::make_unique<testing::FakeLogger>();

    mock_system_clock_ =
        std::make_unique<IncrementingSystemClock>(std::chrono::system_clock::duration(0));
    mock_system_clock_->set_time(kSystemTimePoint);
    mock_steady_clock_ = new IncrementingSteadyClock();

    undated_event_manager_ = std::make_unique<UndatedEventManager>(
        encoder_.get(), event_aggregator_.get(), observation_writer_.get(), system_data_.get(),
        kMaxSavedEvents);
    undated_event_manager_->SetSteadyClock(mock_steady_clock_);
  }

  void TearDown() override {
    event_aggregator_.reset();
    undated_event_manager_.reset();
  }

  std::unique_ptr<UndatedEventManager> undated_event_manager_;
  std::unique_ptr<testing::FakeLogger> internal_logger_;
  std::unique_ptr<ObservationWriter> observation_writer_;
  std::unique_ptr<FakeObservationStore> observation_store_;
  std::unique_ptr<TestUpdateRecipient> update_recipient_;
  std::unique_ptr<IncrementingSystemClock> mock_system_clock_;
  IncrementingSteadyClock* mock_steady_clock_;

 private:
  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<EventAggregator> event_aggregator_;
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;
  std::unique_ptr<SystemDataInterface> system_data_;
  std::unique_ptr<MockConsistentProtoStore> local_aggregate_proto_store_;
  std::unique_ptr<MockConsistentProtoStore> obs_history_proto_store_;
};

// Test that the UndatedEventManager can outlive the source of the ProjectContext.
TEST_F(UndatedEventManagerTest, DeletedProjectContext) {
  std::shared_ptr<ProjectContext> project_context =
      GetTestProject(testing::all_report_types::kCobaltRegistryBase64);

  // Create an event and save it.
  auto event_record = std::make_unique<EventRecord>(
      project_context, testing::all_report_types::kErrorOccurredMetricId);
  event_record->event()->mutable_occurrence_event()->set_event_code(42);
  ASSERT_NE(nullptr, event_record->metric());

  // Drop the ProjectContext. If no one else has retained a copy, it will be deleted.
  project_context.reset();

  ASSERT_EQ(kOK, undated_event_manager_->Save(std::move(event_record)));

  // Check that no immediate Observations were generated.
  EXPECT_EQ(0, observation_store_->num_observations_added());
  EXPECT_EQ(1, undated_event_manager_->NumSavedEvents());

  // Flush the saved events, this will generate immediate observations.
  ASSERT_EQ(kOK, undated_event_manager_->Flush(mock_system_clock_.get(), internal_logger_.get()));
  EXPECT_EQ(0, undated_event_manager_->NumSavedEvents());

  // Now there should be an observation.
  EXPECT_EQ(1, observation_store_->num_observations_added());
  // Monotonic clock was not advanced, so the day index is the current one.
  EXPECT_EQ(kDayIndex, observation_store_->metadata_received[0]->day_index());
  // One internal metric call for the number of cached events.
  EXPECT_EQ(1, internal_logger_->call_count());
  EXPECT_EQ(kInaccurateClockEventsCachedMetricId,
            internal_logger_->last_event_logged().metric_id());
}

TEST_F(UndatedEventManagerTest, OneImmediatelyFlushedEvent) {
  std::shared_ptr<ProjectContext> project_context =
      GetTestProject(testing::all_report_types::kCobaltRegistryBase64);

  // Create an event and save it.
  auto event_record = std::make_unique<EventRecord>(
      project_context, testing::all_report_types::kErrorOccurredMetricId);
  event_record->event()->mutable_occurrence_event()->set_event_code(42);
  ASSERT_NE(nullptr, event_record->metric());
  ASSERT_EQ(kOK, undated_event_manager_->Save(std::move(event_record)));

  // Check that no immediate Observations were generated.
  EXPECT_EQ(0, observation_store_->num_observations_added());
  EXPECT_EQ(1, undated_event_manager_->NumSavedEvents());

  // Flush the saved events, this will generate immediate observations.
  ASSERT_EQ(kOK, undated_event_manager_->Flush(mock_system_clock_.get(), internal_logger_.get()));
  EXPECT_EQ(0, undated_event_manager_->NumSavedEvents());

  // Now there should be an observation.
  EXPECT_EQ(1, observation_store_->num_observations_added());
  // Monotonic clock was not advanced, so the day index is the current one.
  EXPECT_EQ(kDayIndex, observation_store_->metadata_received[0]->day_index());
  // One internal metric call for the number of cached events.
  EXPECT_EQ(1, internal_logger_->call_count());
  EXPECT_EQ(kInaccurateClockEventsCachedMetricId,
            internal_logger_->last_event_logged().metric_id());
}

TEST_F(UndatedEventManagerTest, OneDayDelayInSystemClockAccuracy) {
  std::shared_ptr<ProjectContext> project_context =
      GetTestProject(testing::all_report_types::kCobaltRegistryBase64);

  // Create an event and save it.
  auto event_record = std::make_unique<EventRecord>(
      project_context, testing::all_report_types::kErrorOccurredMetricId);
  event_record->event()->mutable_occurrence_event()->set_event_code(42);
  ASSERT_NE(nullptr, event_record->metric());
  ASSERT_EQ(kOK, undated_event_manager_->Save(std::move(event_record)));

  // Check that no immediate Observations were generated.
  EXPECT_EQ(0, observation_store_->num_observations_added());
  EXPECT_EQ(1, undated_event_manager_->NumSavedEvents());

  // One day later, the clock is finally accurate, flush the saved events, this will generate
  // observations.
  mock_steady_clock_->increment_by(std::chrono::hours(24));
  ASSERT_EQ(kOK, undated_event_manager_->Flush(mock_system_clock_.get(), internal_logger_.get()));
  EXPECT_EQ(0, undated_event_manager_->NumSavedEvents());

  // Now there should be an observation.
  EXPECT_EQ(1, observation_store_->num_observations_added());
  // Day index is from the previous day, due to the monotonic time advancing one day.
  EXPECT_EQ(kDayIndex - 1, observation_store_->metadata_received[0]->day_index());
  // One internal metric call for the number of cached events.
  EXPECT_EQ(1, internal_logger_->call_count());
  EXPECT_EQ(kInaccurateClockEventsCachedMetricId,
            internal_logger_->last_event_logged().metric_id());
}

TEST_F(UndatedEventManagerTest, TooManyEventsToStore) {
  std::shared_ptr<ProjectContext> project_context =
      GetTestProject(testing::all_report_types::kCobaltRegistryBase64);

  // Create some events and save them. These will be dropped
  auto event_record = std::make_unique<EventRecord>(
      project_context, testing::all_report_types::kDeviceBootsMetricId);
  event_record->event()->mutable_occurrence_event()->set_event_code(1);
  ASSERT_NE(nullptr, event_record->metric());
  ASSERT_EQ(kOK, undated_event_manager_->Save(std::move(event_record)));
  event_record = std::make_unique<EventRecord>(project_context,
                                               testing::all_report_types::kErrorOccurredMetricId);
  event_record->event()->mutable_occurrence_event()->set_event_code(99);
  ASSERT_NE(nullptr, event_record->metric());
  ASSERT_EQ(kOK, undated_event_manager_->Save(std::move(event_record)));
  event_record = std::make_unique<EventRecord>(project_context,
                                               testing::all_report_types::kFeaturesActiveMetricId);
  event_record->event()->mutable_occurrence_event()->set_event_code(98);
  ASSERT_NE(nullptr, event_record->metric());
  ASSERT_EQ(kOK, undated_event_manager_->Save(std::move(event_record)));
  event_record = std::make_unique<EventRecord>(project_context,
                                               testing::all_report_types::kErrorOccurredMetricId);
  event_record->event()->mutable_occurrence_event()->set_event_code(97);
  ASSERT_NE(nullptr, event_record->metric());
  ASSERT_EQ(kOK, undated_event_manager_->Save(std::move(event_record)));

  // Save events beyond the max size of the cache. Only 10 events should fit in memory.
  // Each event is on a different day.
  for (int i = kMaxSavedEvents; i > 0; i--) {
    mock_steady_clock_->increment_by(std::chrono::hours(24));
    event_record = std::make_unique<EventRecord>(project_context,
                                                 testing::all_report_types::kErrorOccurredMetricId);
    event_record->event()->mutable_occurrence_event()->set_event_code(i);
    ASSERT_NE(nullptr, event_record->metric());
    ASSERT_EQ(kOK, undated_event_manager_->Save(std::move(event_record)));
  }

  // Check that no immediate Observations were generated and only the max were saved.
  ASSERT_EQ(0, observation_store_->num_observations_added());
  EXPECT_EQ(kMaxSavedEvents, undated_event_manager_->NumSavedEvents());

  // Flush the saved events, this will generate immediate observations.
  ASSERT_EQ(kOK, undated_event_manager_->Flush(mock_system_clock_.get(), internal_logger_.get()));
  EXPECT_EQ(0, undated_event_manager_->NumSavedEvents());

  // Now there should be the maximum cache size of observation.
  EXPECT_EQ(kMaxSavedEvents, observation_store_->num_observations_added());

  for (int i = 0; i < kMaxSavedEvents; i++) {
    EXPECT_EQ(testing::all_report_types::kErrorOccurredMetricId,
              observation_store_->metadata_received[i]->metric_id());
    // Day index starts from 9 days ago and ends at today.
    EXPECT_EQ(kDayIndex - (kMaxSavedEvents - 1) + i,
              observation_store_->metadata_received[i]->day_index());
  }
  // Two internal metric calls for the number of cached and dropped events.
  EXPECT_EQ(2, internal_logger_->call_count());
  EXPECT_EQ(kInaccurateClockEventsDroppedMetricId,
            internal_logger_->last_event_logged().metric_id());
}

TEST_F(UndatedEventManagerTest, SaveAfterFlush) {
  std::shared_ptr<ProjectContext> project_context =
      GetTestProject(testing::all_report_types::kCobaltRegistryBase64);

  // Flush the empty manager, this will not generate any observations.
  ASSERT_EQ(kOK, undated_event_manager_->Flush(mock_system_clock_.get(), internal_logger_.get()));
  EXPECT_EQ(0, observation_store_->num_observations_added());
  EXPECT_EQ(0, undated_event_manager_->NumSavedEvents());

  // Create an event and try to save it.
  auto event_record = std::make_unique<EventRecord>(
      project_context, testing::all_report_types::kErrorOccurredMetricId);
  event_record->event()->mutable_occurrence_event()->set_event_code(42);
  ASSERT_NE(nullptr, event_record->metric());

  // One day later, the clock is finally accurate, try to save an event, this will generate
  // observations. (This scenario is not very likely, as the clock should report accurate around the
  // same time as the flush, but this allows us to verify the steady clock time being after the
  // flush time results in a correct date setting.)
  mock_steady_clock_->increment_by(std::chrono::hours(24));
  ASSERT_EQ(kOK, undated_event_manager_->Save(std::move(event_record)));
  EXPECT_EQ(0, undated_event_manager_->NumSavedEvents());
  // Now there should be an observation.
  EXPECT_EQ(1, observation_store_->num_observations_added());
  // Day index is from the previous day, due to the monotonic time advancing one day.
  EXPECT_EQ(kDayIndex + 1, observation_store_->metadata_received[0]->day_index());
  // No internal metric calls for the number of cached events.
  EXPECT_EQ(0, internal_logger_->call_count());
}

}  // namespace logger
}  // namespace cobalt

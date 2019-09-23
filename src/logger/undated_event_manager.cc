// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logger/undated_event_manager.h"

#include <memory>
#include <string>

#include <google/protobuf/stubs/map_util.h>
#include <src/lib/util/clock.h>

#include "src/logger/event_loggers.h"
#include "src/logger/event_record.h"
#include "src/registry/id.h"
#include "src/registry/metric_definition.pb.h"

namespace cobalt::logger {

UndatedEventManager::UndatedEventManager(const Encoder* encoder, EventAggregator* event_aggregator,
                                         ObservationWriter* observation_writer,
                                         encoder::SystemDataInterface* system_data,
                                         int32_t max_saved_events)
    : encoder_(encoder),
      event_aggregator_(event_aggregator),
      observation_writer_(observation_writer),
      system_data_(system_data),
      max_saved_events_(max_saved_events) {
  CHECK(encoder_);
  CHECK(event_aggregator_);
  CHECK(observation_writer_);
  steady_clock_ = std::make_unique<util::SteadyClock>();
}

Status UndatedEventManager::Save(std::unique_ptr<EventRecord> event_record) {
  auto id =
      std::make_pair(event_record->metric()->customer_id(), event_record->metric()->project_id());
  std::unique_ptr<SavedEventRecord> saved_record = std::make_unique<SavedEventRecord>(
      SavedEventRecord{steady_clock_->now(), std::move(event_record)});

  // Save the event record to the FIFO queue.
  auto lock = protected_saved_records_fields_.lock();
  if (lock->saved_records_.size() >= max_saved_events_) {
    auto dropped_record = std::move(lock->saved_records_.front());
    lock->saved_records_.pop_front();
    ++lock->num_events_dropped_[std::make_pair(
        dropped_record->event_record->metric()->customer_id(),
        dropped_record->event_record->metric()->project_id())];
  }
  lock->saved_records_.emplace_back(std::move(saved_record));
  lock->num_events_cached_[id] += 1;

  return Status::kOK;
}

Status UndatedEventManager::Flush(util::SystemClockInterface* system_clock,
                                  LoggerInterface* internal_logger) {
  // Lock for the entire flush process. This could take a while, but there
  // should no longer be incoming records to save as the clock is now accurate.
  auto lock = protected_saved_records_fields_.lock();

  // Get reference times that will be used to convert the saved event record's monotonic time to an
  // accurate system time.
  std::chrono::system_clock::time_point reference_system_time = system_clock->now();
  std::chrono::steady_clock::time_point reference_monotonic_time = steady_clock_->now();
  std::chrono::system_clock::time_point event_timestamp;
  std::unique_ptr<SavedEventRecord> saved_record;

  while (!lock->saved_records_.empty()) {
    saved_record = std::move(lock->saved_records_.front());
    lock->saved_records_.pop_front();

    // Convert the recorded monotonic time to a system time.
    const std::chrono::seconds& time_shift = std::chrono::duration_cast<std::chrono::seconds>(
        reference_monotonic_time - saved_record->monotonic_time);
    if (time_shift < std::chrono::seconds(0)) {
      LOG(ERROR) << "Dropping saved event that occurs in the future for metric type: "
                 << saved_record->event_record->metric()->metric_type();
      continue;
    }
    event_timestamp = reference_system_time - time_shift;

    auto event_logger =
        internal::EventLogger::Create(saved_record->event_record->metric()->metric_type(), encoder_,
                                      event_aggregator_, observation_writer_, system_data_);

    if (event_logger == nullptr) {
      LOG(ERROR) << "Failed to process a metric type of "
                 << saved_record->event_record->metric()->metric_type();
    } else {
      event_logger->Log(std::move(saved_record->event_record), event_timestamp);
    }
  }

  auto internal_metrics = std::make_unique<InternalMetricsImpl>(internal_logger);

  // Record that we saved records due to clock inaccuracy.
  for (auto cached_events : lock->num_events_cached_) {
    auto id = cached_events.first;
    internal_metrics->InaccurateClockEventsCached(cached_events.second, id.first, id.second);
  }
  lock->num_events_cached_.clear();

  // Record that we dropped events due to memory constraints.
  for (auto dropped_events : lock->num_events_dropped_) {
    auto id = dropped_events.first;
    internal_metrics->InaccurateClockEventsDropped(dropped_events.second, id.first, id.second);
  }
  lock->num_events_dropped_.clear();

  return Status::kOK;
}

int UndatedEventManager::NumSavedEvents() const {
  return protected_saved_records_fields_.const_lock()->saved_records_.size();
}

}  // namespace cobalt::logger

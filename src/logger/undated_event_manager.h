// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOGGER_UNDATED_EVENT_MANAGER_H_
#define COBALT_SRC_LOGGER_UNDATED_EVENT_MANAGER_H_

#include <queue>
#include <string>

#include "src/lib/util/clock.h"
#include "src/local_aggregation/event_aggregator.h"
#include "src/logger/encoder.h"
#include "src/logger/internal_metrics.h"
#include "src/logger/logger_interface.h"
#include "src/logger/observation_writer.h"
#include "src/logger/project_context.h"
#include "src/logger/status.h"

namespace cobalt::logger {

// A container for an EventRecord and the time/context when it was logged.
struct SavedEventRecord {
  // The steady clock time when the event occurred.
  const std::chrono::steady_clock::time_point monotonic_time;
  // The event that occurred.
  std::unique_ptr<EventRecord> event_record;
};

constexpr int32_t kDefaultMaxSavedEvents = 10000;

// UndatedEventManager manages events without timestamps while the clock is inaccurate.
//
// While the system clock is unreliable, Cobalt Loggers will pass incoming Events to the
// UndatedEventManager for interim storage. No locally aggregated observations will be generated
// during this time. Buffered events will be stored in memory with the monotonic (steady) time they
// occurred at, and dropped if a reboot occurs before a clock sync or if a storage quota is reached.
//
// When the UndatedEventManager is notified that the system clock is reliable, it will use the
// current offset between the (now accurate) system clock and the monotonic (steady) clock to assign
// a date to each buffered event. The events will then moved back into the main logging path, to be
// either aggregated on-device or formed into observations.
//
// There should be one instance of UndatedEventManager across all Loggers.
class UndatedEventManager {
 public:
  // Constructor
  //
  // The non-optional parameters are needed to be able to construct EventLoggers.
  // See the EventLogger constructor for their use.
  //
  // |max_saved_event_memory| The maximum amount of memory to use to save events. When this limit is
  // reached, old events are dropped to make room for new events.
  //
  // |steady_clock| An optional steady clock to use to record the monotonic time of saved events.
  // Primarily for testing.
  UndatedEventManager(const Encoder* encoder, local_aggregation::EventAggregator* event_aggregator,
                      ObservationWriter* observation_writer,
                      system_data::SystemDataInterface* system_data,
                      int32_t max_saved_events = kDefaultMaxSavedEvents);

  // Saves the fact that an event has occurred.
  //
  // |event_record| The event that occurred.
  Status Save(std::unique_ptr<EventRecord> event_record);

  // Flush all the saved events now that the system clock is accurate.
  //
  // |system_clock| The system clock that can now be used to log events.
  // |internal_logger| The Logger instance used to send metrics about Cobalt to Cobalt.
  Status Flush(util::SystemClockInterface* system_clock, LoggerInterface* internal_logger);

  // Get the current number of events that are being saved.
  int NumSavedEvents() const;

 private:
  friend class UndatedEventManagerTest;  // for testing

  void SetSteadyClock(util::SteadyClockInterface* steady_clock) {
    steady_clock_.reset(steady_clock);
  }

  Status FlushSavedRecord(std::unique_ptr<SavedEventRecord> saved_record,
                          const std::chrono::system_clock::time_point& reference_system_time,
                          const std::chrono::steady_clock::time_point& reference_monotonic_time);

  // Used only to construct EventLogger instances.
  const Encoder* encoder_;
  local_aggregation::EventAggregator* event_aggregator_;
  const ObservationWriter* observation_writer_;
  const system_data::SystemDataInterface* system_data_;

  size_t max_saved_events_;

  // A monotonic (steady) clock for tracking event time when the system clock is inaccurate.
  std::unique_ptr<util::SteadyClockInterface> steady_clock_;

  // Guards access to saved_records_ and the other related fields.
  struct SavedRecordsFields {
    // FIFO queue of SavedEventRecords to process once the clock is accurate.
    std::deque<std::unique_ptr<SavedEventRecord>> saved_records_;

    // Mapping of identifiers of ProjectContexts to the stats for them.
    std::map<std::pair<uint32_t, uint32_t>, int64_t> num_events_cached_;
    std::map<std::pair<uint32_t, uint32_t>, int64_t> num_events_dropped_;

    // Whether Flush() has already been called on this object.
    bool flushed = false;

    // If |flushed_| is true, these will contain the reference clock values at the time of the
    // flush.
    std::chrono::system_clock::time_point reference_system_time_;
    std::chrono::steady_clock::time_point reference_monotonic_time_;
  };
  util::RWProtectedFields<SavedRecordsFields> protected_saved_records_fields_;
};

}  // namespace cobalt::logger

#endif  // COBALT_SRC_LOGGER_UNDATED_EVENT_MANAGER_H_

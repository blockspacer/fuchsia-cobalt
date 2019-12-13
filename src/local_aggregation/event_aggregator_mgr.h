// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOCAL_AGGREGATION_EVENT_AGGREGATOR_MGR_H_
#define COBALT_SRC_LOCAL_AGGREGATION_EVENT_AGGREGATOR_MGR_H_

#include <condition_variable>
#include <memory>
#include <string>
#include <thread>

#include "src/lib/util/clock.h"
#include "src/lib/util/consistent_proto_store.h"
#include "src/lib/util/protected_fields.h"
#include "src/local_aggregation/event_aggregator.h"
#include "src/local_aggregation/local_aggregation.pb.h"
#include "src/logger/encoder.h"
#include "src/logger/observation_writer.h"
#include "src/logger/status.h"

namespace cobalt::local_aggregation {

constexpr int64_t kHoursInADay = 24;

class EventAggregatorManager {
 public:
  // Constructs a class to manage local aggregation and provide EventAggregators.
  //
  // An EventAggregator maintains daily aggregates of Events in a LocalAggregateStore, uses an
  // Encoder to generate Observations for rolling windows ending on a specified day index, and
  // writes the Observations to an ObservationStore using an ObservationWriter.
  //
  // encoder: the singleton instance of an Encoder on the system.
  //
  // local_aggregate_proto_store: A ConsistentProtoStore to be used by the EventAggregator to store
  // snapshots of its in-memory store of event aggregates.
  //
  // obs_history_proto_store: A ConsistentProtoStore to be used by the EventAggregator to store
  // snapshots of its in-memory history of generated Observations.
  //
  // backfill_days: the number of past days for which the EventAggregator generates and sends
  // Observations, in addition to a requested day index. See the comment above
  // EventAggregator::GenerateObservations for more detail. The constructor CHECK-fails if a value
  // larger than |kEventAggregatorMaxAllowedBackfillDays| is passed.
  //
  // aggregate_backup_interval: the interval in seconds at which a snapshot of the in-memory store
  // of event aggregates should be written to |local_aggregate_proto_store|.
  //
  // generate_obs_interval: the interval in seconds at which the EventAggregator should generate
  // Observations.
  //
  // gc_interval: the interval in seconds at which the LocalAggregateStore should be
  // garbage-collected.
  //
  // The constructor CHECK-fails if the value of |aggregate_backup_interval| is larger than either
  // of |generate_obs_interval| or |gc_interval|. In practice, the value of
  // |aggregate_backup_interval| should be small relative to the values of |generate_obs_interval|
  // and |gc_interval|, since each of Observation generation and garbage collection will be done at
  // the smallest multiple of |aggregate_backup_interval| which is greater than or equal to its
  // specified interval.
  EventAggregatorManager(const logger::Encoder* encoder,
                         const logger::ObservationWriter* observation_writer,
                         util::ConsistentProtoStore* local_aggregate_proto_store,
                         util::ConsistentProtoStore* obs_history_proto_store,
                         size_t backfill_days = 0,
                         std::chrono::seconds aggregate_backup_interval = std::chrono::minutes(1),
                         std::chrono::seconds generate_obs_interval = std::chrono::hours(1),
                         std::chrono::seconds gc_interval = std::chrono::hours(kHoursInADay));

  // Starts the worker thread.
  //
  // |clock| The clock that should be used by the worker thread for scheduling tasks and determining
  //         the current day and hour. On systems on which the clock may be initially inaccurate,
  //         the caller should wait to invoke this method until after it is known that the clock is
  //         accurate.
  void Start(std::unique_ptr<util::SystemClockInterface> clock);

  // Returns a pointer to an EventAggregator to be used for logging.
  EventAggregator* GetEventAggregator() { return event_aggregator_.get(); }

  // Checks that the worker thread is shut down, and if so, triggers an out of schedule
  // AggregateStore::GenerateObservations() and returns its result. Returns kOther if the
  // worker thread is joinable. See the documentation on AggregateStore::GenerateObservations()
  // for a description of the parameters.
  //
  // This method is intended for use in the Cobalt testapps which require a single thread to
  // both log events to and generate Observations from an EventAggregator.
  logger::Status GenerateObservationsNoWorker(uint32_t final_day_index_utc,
                                              uint32_t final_day_index_local = 0u);

 private:
  friend class TestEventAggregatorManager;
  friend class EventAggregatorManagerTest;
  friend class CobaltControllerImpl;

  // Sets the EventAggregator's SteadyClockInterface. Only for use in tests.
  void SetSteadyClock(util::SteadyClockInterface* clock) {
    event_aggregator_->SetSteadyClock(clock);
  }

  std::unique_ptr<EventAggregator> event_aggregator_;
};

}  // namespace cobalt::local_aggregation

#endif  // COBALT_SRC_LOCAL_AGGREGATION_EVENT_AGGREGATOR_MGR_H_

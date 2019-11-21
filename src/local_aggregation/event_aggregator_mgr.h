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
#include "src/local_aggregation/aggregate_store.h"
#include "src/local_aggregation/event_aggregator.h"
#include "src/local_aggregation/local_aggregation.pb.h"
#include "src/logger/encoder.h"
#include "src/logger/observation_writer.h"
#include "src/logger/status.h"

namespace cobalt {

constexpr int64_t kHoursInADay = 24;

// Forward declaration used for friend tests. These will be removed once a better solution is
// designed.
// TODO(ninai): remove this
namespace internal {

class RealLoggerFactory;

}  // namespace internal

namespace local_aggregation {

// Class responsible for managing memory, threading, locks, time and the main loop.
//
// A worker thread calls on AggregateStore methods to do the following tasks at intervals
// specified in the EventAggregator's constructor:
// (1) Calls BackUp*() to back up the EventAggregator's state to the file system.
// (2) Calls GenerateObservations() with the previous day's day index to generate all Observations
// for rolling windows ending on that day index, as well as any missing Observations for a specified
// number of days in the past.
// (3) Calls GarbageCollect() to delete daily aggregates which are not needed to compute aggregates
// for any windows of interest in the future.
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

  // Shut down the worker thread before destructing the EventAggregatorManager.
  ~EventAggregatorManager() { ShutDown(); }

  // Starts the worker thread.
  //
  // |clock| The clock that should be used by the worker thread for scheduling tasks and determining
  //         the current day and hour. On systems on which the clock may be initially inaccurate,
  //         the caller should wait to invoke this method until after it is known that the clock is
  //         accurate.
  void Start(std::unique_ptr<util::SystemClockInterface> clock);

  // Returns a pointer to an EventAggregator to be used for logging.
  EventAggregator* GetEventAggregator() { return event_aggregator_.get(); }

 private:
  friend class TestEventAggregatorManager;
  friend class EventAggregatorManagerTest;
  friend class AggregateStoreTest;
  friend class AggregateStoreWorkerTest;
  friend class EventAggregatorTest;
  friend class EventAggregatorManagerTest;
  friend class EventAggregatorWorkerTest;
  friend class internal::RealLoggerFactory;

  // Request that the worker thread shut down and wait for it to exit. The
  // worker thread backs up the LocalAggregateStore before exiting.
  void ShutDown();

  // Main loop executed by the worker thread. The thread sleeps for
  // |aggregate_backup_interval_| seconds or until notified of shutdown, then
  // calls BackUpLocalAggregateStore(). If not notified of shutdown, calls
  // DoScheduledTasks() and schedules the next occurrence of any completed
  // tasks.
  void Run(std::unique_ptr<util::SystemClockInterface> system_clock);

  // Helper method called by Run(). If |next_generate_obs_| is less than or equal to |steady_time|,
  // calls AggregateStore::GenerateObservations() with the day index of the previous day from
  // |system_time| in each of UTC and local time, and then backs up the history of generated
  // Observations. If |next_gc_| is less than or equal to |steady_time|, calls
  // AggregateStore::GarbageCollect() with the day index of the previous day from |system_time| in
  // each of UTC and local time and then backs up the LocalAggregateStore. In each case, an error is
  // logged and execution continues if the operation fails.
  void DoScheduledTasks(std::chrono::system_clock::time_point system_time,
                        std::chrono::steady_clock::time_point steady_time);

  // Sets the EventAggregatorManager's SteadyClockInterface. Only for use in tests.
  void SetSteadyClock(util::SteadyClockInterface* clock) { steady_clock_.reset(clock); }

  struct WorkerThreadController {
    // Setting this value to true requests that the worker thread stop.
    bool shut_down = true;

    // Setting this value to true requests that the worker thread immediately perform its work
    // rather than waiting for the next scheduled time to run. After the worker thread has completed
    // its work, it will reset this value to false.
    bool immediate_run_trigger = false;

    // Used to wait on to execute periodic EventAggregator tasks.
    std::condition_variable_any shutdown_notifier;
  };

  struct ShutDownFlag {
    // Used to trigger a shutdown of the EventAggregator.
    bool shut_down = true;
    // Used in tests to manually trigger a run of the EventAggregator's scheduled tasks.
    bool manual_trigger = false;
    // Used to wait on to execute periodic EventAggregator tasks.
    std::condition_variable_any shutdown_notifier;
  };

  size_t backfill_days_;
  std::chrono::seconds aggregate_backup_interval_;
  std::chrono::seconds generate_obs_interval_;
  std::chrono::seconds gc_interval_;

  std::chrono::steady_clock::time_point next_generate_obs_;
  std::chrono::steady_clock::time_point next_gc_;
  std::unique_ptr<util::SteadyClockInterface> steady_clock_;

  std::unique_ptr<AggregateStore> aggregate_store_;
  std::unique_ptr<EventAggregator> event_aggregator_;

  std::thread worker_thread_;
  util::ProtectedFields<WorkerThreadController> protected_worker_thread_controller_;
};

}  // namespace local_aggregation
}  // namespace cobalt

#endif  // COBALT_SRC_LOCAL_AGGREGATION_EVENT_AGGREGATOR_MGR_H_

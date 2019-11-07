// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOCAL_AGGREGATION_EVENT_AGGREGATOR_H_
#define COBALT_SRC_LOCAL_AGGREGATION_EVENT_AGGREGATOR_H_

#include <condition_variable>
#include <memory>
#include <string>
#include <thread>

#include "src/lib/util/clock.h"
#include "src/lib/util/consistent_proto_store.h"
#include "src/local_aggregation/aggregate_store.h"
#include "src/logger/encoder.h"
#include "src/logger/event_record.h"
#include "src/logger/observation_writer.h"
#include "src/logger/status.h"

namespace cobalt {

// Forward declaration used for friend tests. These will be removed once the
// tests are moved to using mocks rather than private methods.
// TODO(ninai): remove these
namespace logger {

class UniqueActivesLoggerTest;
class TestEventAggregator;

}  // namespace logger

namespace local_aggregation {

// The EventAggregator manages the Loggers' interactions with the local aggregation.
//
// Each Logger interacts with the EventAggregator in the following way:
// (1) When the Logger is created, it calls UpdateAggregationConfigs() to
// provide the EventAggregator with its view of Cobalt's metric and report
// registry.
// (2) When logging an Event for a locally aggregated report, a Logger
// calls a Log*() method with the EventRecord and the report id for the event.
//
// Functionality that the EventAggregator curently has but will be moved to the
// EventAggregatorManager:
// A worker thread calls on AggregateStore methods to do the following tasks at intervals
// specified in the EventAggregator's constructor:
// (1) Calls BackUp*() to back up the EventAggregator's state to the file system.
// (2) Calls GenerateObservations() with the previous day's day index to generate all Observations
// for rolling windows ending on that day index, as well as any missing Observations for a specified
// number of days in the past.
// (3) Calls GarbageCollect() to delete daily aggregates which are not needed to compute aggregates
// for any windows of interest in the future.
class EventAggregator {
 public:
  // Constructs an EventAggregator.
  //
  // encoder: the singleton instance of an Encoder on the system.
  //
  // local_aggregate_proto_store: A ConsistentProtoStore to be used by the
  // AggregateStore to store snapshots of its in-memory store of event
  // aggregates.
  //
  // obs_history_proto_store: A ConsistentProtoStore to be used by the
  // AggregateStore to store snapshots of its in-memory history of generated
  // Observations.
  //
  // backfill_days: the number of past days for which the AggregateStore
  // generates and sends Observations, in addition to a requested day index.
  // See the comment above AggreateStoe::GenerateObservations for more
  // detail. The constructor CHECK-fails if a value larger than
  // |kEventAggregatorMaxAllowedBackfillDays| is passed.
  //
  // aggregate_backup_interval: the interval in seconds at which a snapshot of
  // the in-memory store of event aggregates should be written to
  // |local_aggregate_proto_store|.
  //
  // generate_obs_interval: the interval in seconds at which the
  // EventAggregator should generate Observations.
  //
  // gc_interval: the interval in seconds at which the LocalAggregateStore
  // should be garbage-collected.
  //
  // The constructor CHECK-fails if the value of |aggregate_backup_interval| is
  // larger than either of |generate_obs_interval| or |gc_interval|. In
  // practice, the value of |aggregate_backup_interval| should be small relative
  // to the values of |generate_obs_interval| and |gc_interval|, since each of
  // Observation generation and garbage collection will be done at the smallest
  // multiple of |aggregate_backup_interval| which is greater than or equal to
  // its specified interval.
  EventAggregator(const logger::Encoder* encoder,
                  const logger::ObservationWriter* observation_writer,
                  util::ConsistentProtoStore* local_aggregate_proto_store,
                  util::ConsistentProtoStore* obs_history_proto_store, size_t backfill_days = 0,
                  std::chrono::seconds aggregate_backup_interval = std::chrono::minutes(1),
                  std::chrono::seconds generate_obs_interval = std::chrono::hours(1),
                  std::chrono::seconds gc_interval = kOneDay);

  // Shut down the worker thread before destructing the EventAggregator.
  ~EventAggregator() { ShutDown(); }

  // Starts the worker thread.
  //
  // |clock| The clock that should be used by the worker thread for scheduling
  //         tasks and determining the current day and hour. On systems on which
  //         the clock may be initially inaccurate, the caller should wait to
  //         invoke this method until after it is known that the clock is
  //         accurate.
  void Start(std::unique_ptr<util::SystemClockInterface> clock);

  // Updates the EventAggregator's view of the Cobalt metric and report
  // registry.
  //
  // This method may be called multiple times during the EventAggregator's
  // lifetime. If the EventAggregator is provided with a report whose tuple
  // (customer ID, project ID, metric ID, report ID) matches that of a
  // previously provided report, then the new report is ignored, even if other
  // properties of the customer, Project, MetricDefinition, or
  // ReportDefinition differ from those of the existing report.
  logger::Status UpdateAggregationConfigs(const logger::ProjectContext& project_context);

  // Adds an Event associated to a report of type UNIQUE_N_DAY_ACTIVES to the
  // AggregateStore.
  //
  // report_id: the ID of the report associated to the logged Event.
  //
  // event_record: an EventRecord wrapping an Event of type OccurrenceEvent
  // and the MetricDefinition for which the Event is to be logged.
  //
  // Returns kOK if the LocalAggregateStore was successfully updated, and
  // kInvalidArguments if either a lookup key corresponding to |report_id| was
  // not found in the LocalAggregateStore, or if the Event wrapped by
  // EventRecord is not of type OccurrenceEvent.
  //
  // The EventAggregator does not explicitly validate the event code of
  // the logged Event, and if the event code is larger than the associated
  // metric's max_event_code then the EventAggregator will form and store an
  // aggregate map for that event code just as it does for a valid event code.
  // However, Observations will only be generated for valid event codes, and
  // aggregates associated with invalid event codes will be garbage-collected
  // together with valid aggregates when EventAggregator::GarbageCollect() is
  // called.
  logger::Status LogUniqueActivesEvent(uint32_t report_id, const logger::EventRecord& event_record);

  // LogCountEvent, LogElapsedTimeEvent:
  //
  // Adds an Event associated to a report of type PER_DEVICE_NUMERIC_STATS to
  // the AggregateStore.
  //
  // report_id: the ID of the report associated to the logged Event.
  //
  // event_record: an EventRecord wrapping an Event and the MetricDefinition for
  // which the Event is to be logged.
  //
  // Returns kOK if the LocalAggregateStore was successfully updated, and
  // kInvalidArguments if either a lookup key corresponding to |report_id| was
  // not found in the LocalAggregateStore, or if the Event wrapped by
  // EventRecord is not of the expected type.
  //
  // LogCountEvent: |event_record| should wrap a CountEvent.
  logger::Status LogCountEvent(uint32_t report_id, const logger::EventRecord& event_record);
  // LogElapsedTimeEvent: |event_record| should wrap an ElapsedTimeEvent.
  logger::Status LogElapsedTimeEvent(uint32_t report_id, const logger::EventRecord& event_record);
  // LogFrameRateEvent: |event_record| should wrap a FrameRateEvent.
  logger::Status LogFrameRateEvent(uint32_t report_id, const logger::EventRecord& event_record);
  // LogMemoryUsageEvent: |event_record| should wrap a MemoryUsageEvent.
  logger::Status LogMemoryUsageEvent(uint32_t report_id, const logger::EventRecord& event_record);

  // Checks that the worker thread is shut down, and if so, calls
  // AggregateStore::GenerateObservations() and returns its result. Returns kOther if the
  // worker thread is joinable. See the documentation on AggregateStore::GenerateObservations()
  // for a description of the parameters.
  //
  // This method is intended for use in tests which require a single thread to
  // both log events to and generate Observations from an EventAggregator.
  logger::Status GenerateObservationsNoWorker(uint32_t final_day_index_utc,
                                              uint32_t final_day_index_local = 0u);

 private:
  friend class EventAggregatorManager;  // used for transition during redesign.
  friend class AggregateStoreTest;
  friend class AggregateStoreWorkerTest;
  friend class EventAggregatorTest;
  friend class EventAggregatorManagerTest;
  friend class EventAggregatorWorkerTest;
  friend class logger::TestEventAggregator;
  friend class logger::UniqueActivesLoggerTest;

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

  // Logs a numeric value to the LocalAggregateStore by adding |value| to the
  // current daily aggregate in the bucket indexed by |report_key|, |day_index|,
  // |component|, and |event_code|. This is a helper method called by
  // LogCountEvent and LogElapsedTimeEvent.
  logger::Status LogNumericEvent(const std::string& report_key, uint32_t day_index,
                                 const std::string& component, uint64_t event_code, int64_t value);

  // Sets the EventAggregator's SteadyClockInterface. Only for use in tests.
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

  std::unique_ptr<AggregateStore> aggregate_store_;

  std::thread worker_thread_;
  util::ProtectedFields<WorkerThreadController> protected_worker_thread_controller_;
  std::chrono::seconds aggregate_backup_interval_;
  std::chrono::seconds generate_obs_interval_;
  std::chrono::seconds gc_interval_;
  std::chrono::steady_clock::time_point next_generate_obs_;
  std::chrono::steady_clock::time_point next_gc_;
  std::unique_ptr<util::SteadyClockInterface> steady_clock_;
};

}  // namespace local_aggregation
}  // namespace cobalt

#endif  // COBALT_SRC_LOCAL_AGGREGATION_EVENT_AGGREGATOR_H_

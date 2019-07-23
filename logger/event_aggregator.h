// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_EVENT_AGGREGATOR_H_
#define COBALT_LOGGER_EVENT_AGGREGATOR_H_

#include <condition_variable>
#include <memory>
#include <string>
#include <thread>

#include "./event.pb.h"
#include "./logging.h"
#include "./observation2.pb.h"
#include "config/metric_definition.pb.h"
#include "config/report_definition.pb.h"
#include "logger/encoder.h"
#include "logger/event_record.h"
#include "logger/local_aggregation.pb.h"
#include "logger/observation_writer.h"
#include "logger/status.h"
#include "util/clock.h"
#include "util/consistent_proto_store.h"
#include "util/protected_fields.h"

namespace cobalt {
namespace logger {

const std::chrono::hours kOneDay(24);

// The EventAggregator manages an in-memory store of aggregated Event values,
// indexed by report, day index, and other dimensions specific to the report
// type (e.g. event code). Periodically, this data is used to generate
// Observations representing aggregates of Event values over a day, week, month,
// etc.
//
// Each Logger interacts with the EventAggregator in the following way:
// (1) When the Logger is created, it calls UpdateAggregationConfigs() to
// provide the EventAggregator with its view of Cobalt's metric and report
// registry.
// (2) When logging an Event for a locally aggregated report, a Logger
// calls an Update*Aggregation() method with the Event and the
// ReportAggregationKey of the report.
//
// A worker thread does the following tasks at intervals specified in the
// EventAggregator's constructor:
// (1) Backs up the EventAggregator's state to the file system.
// (2) Calls GenerateObservations() with the previous day's day index to
// generate all Observations for rolling windows ending on that day index,
// as well as any missing Observations for a specified number of days in the
// past.
// (3) Calls GarbageCollect() to delete daily aggregates which are not
// needed to compute aggregates for any windows of interest in the future.
class EventAggregator {
 public:
  // Maximum value of |backfill_days| allowed by the constructor.
  static const size_t kMaxAllowedBackfillDays = 1000;
  // GenerateObservations() ignores all aggregation window sizes larger than
  // this value.
  static const uint32_t kMaxAllowedAggregationWindowSize = 365;

  // Constructs an EventAggregator.
  //
  // An EventAggregator maintains daily aggregates of Events in a
  // LocalAggregateStore, uses an Encoder to generate Observations for rolling
  // windows ending on a specified day index, and writes the Observations to
  // an ObservationStore using an ObservationWriter.
  //
  // encoder: the singleton instance of an Encoder on the system.
  //
  // local_aggregate_proto_store: A ConsistentProtoStore to be used by the
  // EventAggregator to store snapshots of its in-memory store of event
  // aggregates.
  //
  // obs_history_proto_store: A ConsistentProtoStore to be used by the
  // EventAggregator to store snapshots of its in-memory history of generated
  // Observations.
  //
  // backfill_days: the number of past days for which the EventAggregator
  // generates and sends Observations, in addition to a requested day index.
  // See the comment above EventAggregator::GenerateObservations for more
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
  EventAggregator(const Encoder* encoder, const ObservationWriter* observation_writer,
                  util::ConsistentProtoStore* local_aggregate_proto_store,
                  util::ConsistentProtoStore* obs_history_proto_store, size_t backfill_days = 0,
                  std::chrono::seconds aggregate_backup_interval = std::chrono::minutes(1),
                  std::chrono::seconds generate_obs_interval = std::chrono::hours(1),
                  std::chrono::seconds gc_interval = kOneDay);

  // Shut down the worker thread before destructing the EventAggregator.
  ~EventAggregator() { ShutDown(); }

  // Start the worker thread.
  void Start();

  // Updates the EventAggregator's view of the Cobalt metric and report
  // registry.
  //
  // This method may be called multiple times during the EventAggregator's
  // lifetime. If the EventAggregator is provided with a report whose tuple
  // (customer ID, project ID, metric ID, report ID) matches that of a
  // previously provided report, then the new report is ignored, even if other
  // properties of the customer, Project, MetricDefinition, or
  // ReportDefinition differ from those of the existing report.
  Status UpdateAggregationConfigs(const ProjectContext& project_context);

  // Logs an Event associated to a report of type UNIQUE_N_DAY_ACTIVES to the
  // EventAggregator.
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
  Status LogUniqueActivesEvent(uint32_t report_id, EventRecord* event_record);

  // LogCountEvent, LogElapsedTimeEvent:
  //
  // Logs an Event associated to a report of type PER_DEVICE_NUMERIC_STATS to
  // the EventAggregator.
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
  Status LogCountEvent(uint32_t report_id, EventRecord* event_record);
  // LogElapsedTimeEvent: |event_record| should wrap an ElapsedTimeEvent.
  Status LogElapsedTimeEvent(uint32_t report_id, EventRecord* event_record);
  // LogFrameRateEvent: |event_record| should wrap a FrameRateEvent.
  Status LogFrameRateEvent(uint32_t report_id, EventRecord* event_record);
  // LogMemoryUsageEvent: |event_record| should wrap a MemoryUsageEvent.
  Status LogMemoryUsageEvent(uint32_t report_id, EventRecord* event_record);

  // Checks that the worker thread is shut down, and if so, calls the private
  // method GenerateObservations() and returns its result. Returns kOther if the
  // worker thread is joinable. See the documentation on GenerateObservations()
  // for a description of the parameters.
  //
  // This method is intended for use in tests which require a single thread to
  // both log events to and generate Observations from an EventAggregator.
  Status GenerateObservationsNoWorker(uint32_t final_day_index_utc,
                                      uint32_t final_day_index_local = 0u);

 private:
  friend class LoggerTest;
  friend class EventAggregatorTest;
  friend class EventAggregatorWorkerTest;

  // Request that the worker thread shut down and wait for it to exit. The
  // worker thread backs up the LocalAggregateStore before exiting.
  void ShutDown();

  // Main loop executed by the worker thread. The thread sleeps for
  // |aggregate_backup_interval_| seconds or until notified of shutdown, then
  // calls BackUpLocalAggregateStore(). If not notified of shutdown, calls
  // DoScheduledTasks() and schedules the next occurrence of any completed
  // tasks.
  void Run();

  // Helper method called by Run(). If |next_generate_obs_| is less than or
  // equal to |current_time|, calls GenerateObservations() with the day index of
  // the previous day in each of UTC and local time, and then backs up the
  // history of generated Observations. If |next_gc_| is less than or equal to
  // |current_time|, calls GarbageCollect() with the day index of the previous
  // day in each of UTC and local time and then backs up the
  // LocalAggregateStore. In each case, an error is logged and execution
  // continues if the operation fails.
  void DoScheduledTasks(std::chrono::system_clock::time_point current_time);

  // Writes a snapshot of the LocalAggregateStore to
  // |local_aggregate_proto_store_|.
  Status BackUpLocalAggregateStore();

  // Writes a snapshot of |obs_history_|to |obs_history_proto_store_|.
  Status BackUpObservationHistory();

  // Removes from the LocalAggregateStore all daily aggregates that are too
  // old to contribute to their parent report's largest rolling window on the
  // day which is |backfill_days| before |day_index_utc| (if the parent
  // MetricDefinitions' time zone policy is UTC) or which is |backfill_days|
  // before |day_index_local| (if the parent MetricDefinition's time zone policy
  // is LOCAL). If |day_index_local| is 0, then we set |day_index_local| =
  // |day_index_utc|.
  //
  // If the time zone policy of a report's parent metric is UTC (resp., LOCAL)
  // and if day_index is the largest value of the |day_index_utc| (resp.,
  // |day_index_local|) argument with which GarbageCollect() has been called,
  // then the LocalAggregateStore contains the data needed to generate
  // Observations for that report for day index (day_index + k) for any k >= 0.
  Status GarbageCollect(uint32_t day_index_utc, uint32_t day_index_local = 0u);

  // Generates one or more Observations for all of the registered locally
  // aggregated reports known to this EventAggregator, for rolling windows
  // ending on specified day indices.
  //
  // Each MetricDefinition specifies a time zone policy, which determines the
  // day index for which an Event associated with that MetricDefinition is
  // logged. For all MetricDefinitions whose Events are logged with respect to
  // UTC, this method generates Observations for rolling windows ending on
  // |final_day_index_utc|. For all MetricDefinitions whose Events are logged
  // with respect to local time, this method generates Observations for rolling
  // windows ending on |final_day_index_local|. If |final_day_index_local| is
  // 0, then we set |final_day_index_local| = |final_day_index_utc|.
  //
  // The generated Observations are written to the |observation_writer| passed
  // to the constructor.
  //
  // This class maintains a history of generated Observations and this method
  // additionally performs backfill: Observations are also generated for
  // rolling windows ending on any day in the interval [final_day_index -
  // backfill_days, final_day_index] (where final_day_index is either
  // final_day_index_utc or final_day_index_local, depending on the time zone
  // policy of the associated MetricDefinition), if this history indicates they
  // were not previously generated. Does not generate duplicate Observations
  // when called multiple times with the same day index.
  //
  // Observations are not generated for aggregation windows larger than
  // |kMaxAllowedAggregationWindowSize|.
  Status GenerateObservations(uint32_t final_day_index_utc, uint32_t final_day_index_local = 0u);

  // Logs a numeric value to the LocalAggregateStore by adding |value| to the
  // current daily aggregate in the bucket indexed by |report_key|, |day_index|,
  // |component|, and |event_code|. This is a helper method called by
  // LogCountEvent and LogElapsedTimeEvent.
  Status LogNumericEvent(const std::string& report_key, uint32_t day_index,
                         const std::string& component, uint64_t event_code, int64_t value);

  // Returns the most recent day index for which an Observation was generated
  // for a given UNIQUE_N_DAY_ACTIVES report, event code, and window size,
  // according to |obs_history_|. Returns 0 if no Observation has been generated
  // for the given arguments.
  uint32_t UniqueActivesLastGeneratedDayIndex(const std::string& report_key, uint32_t event_code,
                                              uint32_t window_size) const;

  // Returns the most recent day index for which an Observation was generated
  // for a given PER_DEVICE_NUMERIC_STATS report, component, event code,
  // and window size, according to |obs_history_|. Returns 0 if no Observation
  // has been generated for the given arguments.
  uint32_t PerDeviceNumericLastGeneratedDayIndex(const std::string& report_key,
                                                 const std::string& component, uint32_t event_code,
                                                 uint32_t window_size) const;

  // Returns the most recent day index for which a
  // ReportParticipationObservation was generated for a given report, according
  // to |obs_history_|. Returns 0 if no Observation has been generated for the
  // given arguments.
  uint32_t ReportParticipationLastGeneratedDayIndex(const std::string& report_key) const;

  // For a fixed report of type UNIQUE_N_DAY_ACTIVES, generates an Observation
  // for each event code of the parent metric, for each window size of the
  // report, for the window of that size ending on |final_day_index|, unless
  // an Observation with those parameters was generated in the past. Also
  // generates Observations for days in the backfill period if needed.
  // Writes the Observations to an ObservationStore via the ObservationWriter
  // that was passed to the constructor.
  //
  // Observations are not generated for aggregation windows larger than
  // |kMaxAllowedAggregationWindowSize|.
  Status GenerateUniqueActivesObservations(MetricRef metric_ref, const std::string& report_key,
                                           const ReportAggregates& report_aggregates,
                                           uint32_t num_event_codes, uint32_t final_day_index);

  // Helper method called by GenerateUniqueActivesObservations() to generate
  // and write a single Observation.
  Status GenerateSingleUniqueActivesObservation(MetricRef metric_ref,
                                                const ReportDefinition* report,
                                                uint32_t obs_day_index, uint32_t event_code,
                                                uint32_t window_size, bool was_active) const;

  // For a fixed report of type PER_DEVICE_NUMERIC_STATS, generates a
  // PerDeviceNumericObservation for each tuple (component, event code, window
  // size) for which a numeric event was logged for that event code and
  // component during the window of that size ending on |final_day_index|,
  // unless an Observation with those parameters has been generated in the past.
  // The value of the Observation is the sum, max, or min (depending on the
  // aggregation_type field of the report definition) of all numeric events
  // logged for that report during the window. Also generates
  // PerDeviceNumericObservations for days in the backfill period if needed.
  //
  // In addition to PerDeviceNumericObservations, generates a
  // ReportParticipationObservation for |final_day_index| and any needed days in
  // the backfill period. These ReportParticipationObservations are used by the
  // PerDeviceNumericStats report generator to infer the fleet-wide number of
  // devices for which the sum of numeric events associated to each tuple
  // (component, event code, window size) was zero.
  //
  // Observations are not generated for aggregation windows larger than
  // |kMaxAllowedAggregationWindowSize|.
  Status GeneratePerDeviceNumericObservations(MetricRef metric_ref, const std::string& report_key,
                                              const ReportAggregates& report_aggregates,
                                              uint32_t final_day_index);

  // Helper method called by GeneratePerDeviceNumericObservations() to generate
  // and write a single Observation with value |value|.
  Status GenerateSinglePerDeviceNumericObservation(
      MetricRef metric_ref, const ReportDefinition* report, uint32_t obs_day_index,
      const std::string& component, uint32_t event_code, uint32_t window_size, int64_t value) const;

  // Helper method called by GeneratePerDeviceNumericObservations() to generate
  // and write a single ReportParticipationObservation.
  Status GenerateSingleReportParticipationObservation(MetricRef metric_ref,
                                                      const ReportDefinition* report,
                                                      uint32_t obs_day_index) const;

  LocalAggregateStore CopyLocalAggregateStore() {
    auto local_aggregate_store = protected_aggregate_store_.lock()->local_aggregate_store;
    return local_aggregate_store;
  }

  // Sets the EventAggregator's ClockInterface. Only for use in tests.
  void SetClock(util::ClockInterface* clock) { clock_.reset(clock); }

  struct AggregateStoreFields {
    LocalAggregateStore local_aggregate_store;
  };

  struct ShutDownFlag {
    bool shut_down = true;
    std::condition_variable shutdown_notifier;
  };

  const Encoder* encoder_;
  const ObservationWriter* observation_writer_;
  util::ConsistentProtoStore* local_aggregate_proto_store_;
  util::ConsistentProtoStore* obs_history_proto_store_;
  util::ProtectedFields<AggregateStoreFields> protected_aggregate_store_;
  // Not protected by a mutex. Should only be accessed by |worker_thread_|.
  AggregatedObservationHistoryStore obs_history_;
  size_t backfill_days_ = 0;

  std::thread worker_thread_;
  util::ProtectedFields<ShutDownFlag> protected_shutdown_flag_;
  std::chrono::seconds aggregate_backup_interval_;
  std::chrono::seconds generate_obs_interval_;
  std::chrono::seconds gc_interval_;
  std::chrono::system_clock::time_point next_generate_obs_;
  std::chrono::system_clock::time_point next_gc_;
  std::unique_ptr<util::ClockInterface> clock_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_EVENT_AGGREGATOR_H_

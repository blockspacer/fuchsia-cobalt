// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_EVENT_AGGREGATOR_H_
#define COBALT_LOGGER_EVENT_AGGREGATOR_H_

#include <memory>
#include <string>

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
#include "util/consistent_proto_store.h"

namespace cobalt {
namespace logger {

// Maximum value of |backfill_days| allowed by the constructor of
// EventAggregator.
static const size_t kEventAggregatorMaxAllowedBackfillDays = 1000;
// EventAggregator::GenerateObservations() ignores all aggregation window sizes
// larger than this value.
static const size_t kMaxAllowedAggregationWindowSize = 365;

// The EventAggregator class manages an in-memory store of aggregated values of
// Events logged for locally aggregated report types. For each day, this
// LocalAggregateStore contains an aggregate of the values of logged Events of a
// given event code over that day. These daily aggregates are used to produce
// Observations of values aggregated over a rolling window of size specified in
// the ReportDefinition.
//
// Each Logger interacts with the EventAggregator in the following way:
// (1) When the Logger is created, it calls UpdateAggregationConfigs() to
// provide the EventAggregator with the configurations of its locally aggregated
// reports.
// (2) When logging an Event for a locally aggregated report, a Logger calls
// an Update*Aggregation() method with the Event and the ReportAggregationKey of
// the report.
//
// TODO(pesk): Write the following worker thread.
// In the future, a worker thread will do the following on a regular schedule:
// (1) Call GenerateObservations() with the previous day's day index to generate
// all Observations for rolling windows ending on that day index, as well as any
// missing Observations for a specified number of days in the past.
// (2) Call GarbageCollect() to delete daily aggregates which are not needed to
// compute aggregates for any windows of interest in the future.
// (3) Back up the EventAggregator's state to disk at appropriate intervals.
class EventAggregator {
 public:
  // Constructs an EventAggregator.
  //
  // An EventAggregator maintains daily aggregates of Events in a
  // LocalAggregateStore, uses an Encoder to generate Observations for rolling
  // windows ending on a specified day index, and writes the Observations to an
  // ObservationStore using an ObservationWriter.
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
  // generates and sends Observations, in addition to a requested day index. See
  // the comment above EventAggregator::GenerateObservations for more detail.
  // The constructor CHECK-fails if a value larger than
  // |kEventAggregatorMaxAllowedBackfillDays| is passed.
  EventAggregator(const Encoder* encoder,
                  const ObservationWriter* observation_writer,
                  util::ConsistentProtoStore* local_aggregate_proto_store,
                  util::ConsistentProtoStore* obs_history_proto_store,
                  const size_t backfill_days = 0);

  ~EventAggregator() {}

  // Updates the EventAggregator's view of the set of locally aggregated report
  // configurations.
  //
  // This method may be called multiple times during the EventAggregator's
  // lifetime. If the EventAggregator is provided with a report whose tuple
  // (customer ID, project ID, metric ID, report ID) matches that of a
  // previously provided report, then the new report is ignored, even if other
  // properties of the customer, Project, MetricDefinition, or ReportDefinition
  // differ from those of the existing report.
  Status UpdateAggregationConfigs(const ProjectContext& project_context);

  // Logs an Event associated to a report of type UNIQUE_N_DAY_ACTIVES to the
  // EventAggregator.
  //
  // report_id: the ID of the report associated to the logged Event.
  //
  // event_record: an EventRecord wrapping an Event of type OccurrenceEvent and
  // the MetricDefinition for which the Event is to be logged.
  //
  // Returns kOK if the LocalAggregateStore was successfully updated, and
  // kInvalidArguments if either a lookup key corresponding to |report_id|  was
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

  // Generates one or more Observations for all of the registered locally
  // aggregated reports known to this EventAggregator, for rolling windows
  // ending on |day_index|. The generated Observations are written to the
  // |observation_writer| passed to the constructor.
  //
  // This class maintains a history of generated observations and this method
  // additionally performs backfill: observations are also generated for rolling
  // windows ending on any day in the interval [day_index - backfill_days,
  // day_index], if this history indicates they were not previously generated.
  // Does not generate duplicate Observations when called multiple times with
  // the same day index.
  //
  // Observations are not generated for aggregation windows larger than
  // |kMaxAllowedAggregationWindowSize|.
  Status GenerateObservations(uint32_t final_day_index);

  // Removes from the LocalAggregateStore all daily aggregates which are too old
  // to contribute to their parent report's largest rolling window on the day
  // which is |backfill_days| before |day_index|.
  //
  // If |day_index| is the latest day index for which GarbageCollect() has been
  // called, then the LocalAggregateStore contains the data needed to call
  // GenerateObservations() with |day_index + k| for any k >= 0.
  Status GarbageCollect(uint32_t day_index);

 private:
  friend class LoggerTest;
  friend class EventAggregatorTest;

  // Write a copy of the LocalAggregateStore to |local_aggregate_proto_store_|.
  Status BackUpLocalAggregateStore();

  // Write a copy of the AggregatedObservationHistory to
  // |obs_history_proto_store_|.
  Status BackUpObservationHistory();

  // Returns the most recent day index for which an Observation was generated
  // for a given report, event code, and window size, according to
  // |obs_history_|. Returns 0 if no Observation has been generated for the
  // given arguments.
  uint32_t LastGeneratedDayIndex(const std::string& report_key,
                                 uint32_t event_code,
                                 uint32_t window_size) const;

  // For a fixed report of type UNIQUE_N_DAY_ACTIVES, generates an Observation
  // for each event code of the parent metric, for each window size of the
  // report, for the window of that size ending on |final_day_index|, unless an
  // Observation with those parameters was generated in the past. Also
  // generates Observations for days in the backfill period if needed.
  // Writes the Observations to an ObservationStore via the ObservationWriter
  // that was passed to the constructor.
  //
  // Observations are not generated for aggregation windows larger than
  // |kMaxAllowedAggregationWindowSize|.
  Status GenerateUniqueActivesObservations(
      const MetricRef metric_ref, const std::string& report_key,
      const ReportAggregates& report_aggregates, uint32_t num_event_codes,
      uint32_t final_day_index);

  // Helper method called by GenerateUniqueActivesObservations() to generate and
  // write a single Observation.
  Status GenerateSingleUniqueActivesObservation(const MetricRef metric_ref,
                                                const ReportDefinition* report,
                                                uint32_t obs_day_index,
                                                uint32_t event_code,
                                                uint32_t window_size,
                                                bool was_active) const;

  const Encoder* encoder_;
  const ObservationWriter* observation_writer_;
  LocalAggregateStore local_aggregate_store_;
  AggregatedObservationHistory obs_history_;
  size_t backfill_days_ = 0;
  util::ConsistentProtoStore* local_aggregate_proto_store_;
  util::ConsistentProtoStore* obs_history_proto_store_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_EVENT_AGGREGATOR_H_

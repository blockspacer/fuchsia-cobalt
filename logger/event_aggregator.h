// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_EVENT_AGGREGATOR_H_
#define COBALT_LOGGER_EVENT_AGGREGATOR_H_

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

namespace cobalt {
namespace logger {

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
// In the future, a worker thread will do the following on a daily schedule:
// (1) Call GenerateObservations() with the previous day's day index to generate
// all Observations for rolling windows ending on that day index.
// (2) Call GarbageCollect() to delete daily aggregates which are not needed to
// compute aggregates for any windows of interest.
class EventAggregator {
 public:
  // Constructs an EventAggregator.
  //
  // encoder: the singleton instance of an Encoder on the system.
  //
  // observation_writer: the singleton instance of an ObservationWriter on the
  // system.
  //
  // local_aggregate_store: a LocalAggregateStore proto message.
  EventAggregator(const Encoder* encoder,
                  const ObservationWriter* observation_writer,
                  LocalAggregateStore* local_aggregate_store);

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
  Status LogUniqueActivesEvent(uint32_t report_id,
                               EventRecord* event_record) const;

  // Generates an Observation for each window size in each ReportDefinition
  // known to the EventAggregator, for each event code less than the parent
  // MetricDefinition's max_event_code, for the rolling window of that size
  // ending on |day_index|.
  Status GenerateObservations(uint32_t day_index);

  // Removes from the LocalAggregateStore all daily aggregates which are too old
  // to contribute to their parent report's largest rolling window on
  // |day_index|.
  Status GarbageCollect(uint32_t day_index);

 private:
  friend class LoggerTest;

  // For a fixed report of type UNIQUE_N_DAY_ACTIVES, generates an Observation
  // for each event code of the parent metric, for each window size of the
  // report, for the window of that size ending on |final_day_index|. Writes the
  // Observation to an ObservationStore via the ObservationWriter.
  Status GenerateUniqueActivesObservations(
      const ReportAggregates& report_aggregates,
      uint32_t final_day_index) const;

  // Called by GenerateUniqueActivesObservations to generate a single
  // Observation.
  Status GenerateSingleUniqueActivesObservation(const MetricRef metric_ref,
                                                const ReportDefinition* report,
                                                uint32_t final_day_index,
                                                uint32_t event_code,
                                                bool was_active,
                                                uint32_t window_size) const;

  const Encoder* encoder_;
  const ObservationWriter* observation_writer_;
  LocalAggregateStore* local_aggregate_store_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_EVENT_AGGREGATOR_H_

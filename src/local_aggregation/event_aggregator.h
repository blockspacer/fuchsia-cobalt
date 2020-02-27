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

namespace cobalt::local_aggregation {

// The EventAggregator manages the Loggers' interactions with the local aggregation.
//
// Each Logger interacts with the EventAggregator in the following way:
// (1) When the Logger is created, it calls UpdateAggregationConfigs() to
// provide the EventAggregator with its view of Cobalt's metric and report
// registry.
// (2) When logging an Event for a locally aggregated report, a Logger
// calls an Add*() method with the EventRecord and the report id for the event.
class EventAggregator {
 public:
  // Constructs an EventAggregator.
  //
  // aggregate_store: an AggregateStore, which is used to store the local aggregates.
  explicit EventAggregator(AggregateStore* aggregate_store);

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
  // event_record: an EventRecord wrapping an Event of type EventOccurredEvent
  // and the MetricDefinition for which the Event is to be logged.
  //
  // Returns kOK if the LocalAggregateStore was successfully updated, and
  // kInvalidArguments if either a lookup key corresponding to |report_id| was
  // not found in the LocalAggregateStore, or if the Event wrapped by
  // EventRecord is not of type EventOccurredEvent.
  //
  // The EventAggregator does not explicitly validate the event code of
  // the logged Event, and if the event code is larger than the associated
  // metric's max_event_code then the EventAggregator will form and store an
  // aggregate map for that event code just as it does for a valid event code.
  // However, Observations will only be generated for valid event codes, and
  // aggregates associated with invalid event codes will be garbage-collected
  // together with valid aggregates when EventAggregator::GarbageCollect() is
  // called.
  logger::Status AddUniqueActivesEvent(uint32_t report_id, const logger::EventRecord& event_record);

  // AddEventCountEvent,AddElapsedTimeEvent:
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
  // AddEventCountEvent: |event_record| should wrap a EventCountEvent.
  logger::Status AddEventCountEvent(uint32_t report_id, const logger::EventRecord& event_record);
  // AddElapsedTimeEvent: |event_record| should wrap an ElapsedTimeEvent.
  logger::Status AddElapsedTimeEvent(uint32_t report_id, const logger::EventRecord& event_record);
  // AddFrameRateEvent: |event_record| should wrap a FrameRateEvent.
  logger::Status AddFrameRateEvent(uint32_t report_id, const logger::EventRecord& event_record);
  // AddMemoryUsageEvent: |event_record| should wrap a MemoryUsageEvent.
  logger::Status AddMemoryUsageEvent(uint32_t report_id, const logger::EventRecord& event_record);

 private:
  AggregateStore* aggregate_store_;  // not owned
};

}  // namespace cobalt::local_aggregation

#endif  // COBALT_SRC_LOCAL_AGGREGATION_EVENT_AGGREGATOR_H_

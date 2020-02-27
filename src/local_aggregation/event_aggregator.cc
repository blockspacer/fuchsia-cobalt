// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/local_aggregation/event_aggregator.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/lib/util/datetime_util.h"
#include "src/lib/util/proto_util.h"
#include "src/registry/packed_event_codes.h"

namespace cobalt::local_aggregation {

using logger::EventRecord;
using logger::kInvalidArguments;
using logger::kOK;
using logger::ProjectContext;
using logger::Status;

EventAggregator::EventAggregator(AggregateStore* aggregate_store)
    : aggregate_store_(aggregate_store) {}

// TODO(pesk): update the EventAggregator's view of a Metric
// or ReportDefinition when appropriate.
Status EventAggregator::UpdateAggregationConfigs(const ProjectContext& project_context) {
  Status status;
  for (const auto& metric : project_context.metrics()) {
    switch (metric.metric_type()) {
      case MetricDefinition::EVENT_OCCURRED: {
        for (const auto& report : metric.reports()) {
          switch (report.report_type()) {
            case ReportDefinition::UNIQUE_N_DAY_ACTIVES: {
              status = aggregate_store_->MaybeInsertReportConfig(project_context, metric, report);
              if (status != kOK) {
                return status;
              }
            }
            default:
              continue;
          }
        }
      }
      case MetricDefinition::EVENT_COUNT:
      case MetricDefinition::ELAPSED_TIME:
      case MetricDefinition::FRAME_RATE:
      case MetricDefinition::MEMORY_USAGE: {
        for (const auto& report : metric.reports()) {
          switch (report.report_type()) {
            case ReportDefinition::PER_DEVICE_NUMERIC_STATS:
            case ReportDefinition::PER_DEVICE_HISTOGRAM: {
              status = aggregate_store_->MaybeInsertReportConfig(project_context, metric, report);
              if (status != kOK) {
                return status;
              }
            }
            default:
              continue;
          }
        }
      }
      default:
        continue;
    }
  }
  return kOK;
}

// Helper functions for the Log*Event() methods.
namespace {

// Checks that an Event has type |expected_event_type|.
bool ValidateEventType(Event::TypeCase expected_event_type, const Event& event) {
  Event::TypeCase event_type = event.type_case();
  if (event_type != expected_event_type) {
    LOG(ERROR) << "Expected Event type is " << expected_event_type << "; found " << event_type
               << ".";
    return false;
  }
  return true;
}

}  // namespace

Status EventAggregator::AddUniqueActivesEvent(uint32_t report_id, const EventRecord& event_record) {
  auto* event = event_record.event();
  if (!ValidateEventType(Event::kEventOccurredEvent, *event)) {
    return kInvalidArguments;
  }
  auto* metric = event_record.metric();
  return aggregate_store_->SetActive(metric->customer_id(), metric->project_id(), metric->id(),
                                     report_id, event->event_occurred_event().event_code(),
                                     event->day_index());
}

Status EventAggregator::AddEventCountEvent(uint32_t report_id, const EventRecord& event_record) {
  auto* event = event_record.event();
  if (!ValidateEventType(Event::kEventCountEvent, *event)) {
    return kInvalidArguments;
  }

  auto* metric = event_record.metric();
  const EventCountEvent& event_count_event = event->event_count_event();

  return aggregate_store_->UpdateNumericAggregate(
      metric->customer_id(), metric->project_id(), metric->id(), report_id,
      event_count_event.component(), config::PackEventCodes(event_count_event.event_code()),
      event->day_index(), event_count_event.count());
}

Status EventAggregator::AddElapsedTimeEvent(uint32_t report_id, const EventRecord& event_record) {
  auto* event = event_record.event();
  if (!ValidateEventType(Event::kElapsedTimeEvent, *event)) {
    return kInvalidArguments;
  }

  auto* metric = event_record.metric();
  const ElapsedTimeEvent& elapsed_time_event = event->elapsed_time_event();

  return aggregate_store_->UpdateNumericAggregate(
      metric->customer_id(), metric->project_id(), metric->id(), report_id,
      elapsed_time_event.component(), config::PackEventCodes(elapsed_time_event.event_code()),
      event->day_index(), elapsed_time_event.elapsed_micros());
}

Status EventAggregator::AddFrameRateEvent(uint32_t report_id, const EventRecord& event_record) {
  auto* event = event_record.event();
  if (!ValidateEventType(Event::kFrameRateEvent, *event)) {
    return kInvalidArguments;
  }
  auto* metric = event_record.metric();
  const FrameRateEvent& frame_rate_event = event->frame_rate_event();

  return aggregate_store_->UpdateNumericAggregate(
      metric->customer_id(), metric->project_id(), metric->id(), report_id,
      frame_rate_event.component(), config::PackEventCodes(frame_rate_event.event_code()),
      event->day_index(), frame_rate_event.frames_per_1000_seconds());
}

Status EventAggregator::AddMemoryUsageEvent(uint32_t report_id, const EventRecord& event_record) {
  auto* event = event_record.event();
  if (!ValidateEventType(Event::kMemoryUsageEvent, *event)) {
    return kInvalidArguments;
  }

  auto* metric = event_record.metric();
  const MemoryUsageEvent& memory_usage_event = event->memory_usage_event();

  return aggregate_store_->UpdateNumericAggregate(
      metric->customer_id(), metric->project_id(), metric->id(), report_id,
      memory_usage_event.component(), config::PackEventCodes(memory_usage_event.event_code()),
      event->day_index(), memory_usage_event.bytes());
}
}  // namespace cobalt::local_aggregation

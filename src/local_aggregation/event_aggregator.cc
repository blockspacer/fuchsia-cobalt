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

using logger::Encoder;
using logger::EventRecord;
using logger::kInvalidArguments;
using logger::kOK;
using logger::kOther;
using logger::ObservationWriter;
using logger::ProjectContext;
using logger::Status;
using util::ConsistentProtoStore;
using util::SerializeToBase64;
using util::SteadyClock;
using util::TimeToDayIndex;

namespace {

// Populates a ReportAggregationKey proto message and then populates a string
// with the base64 encoding of the serialized proto.
bool PopulateReportKey(uint32_t customer_id, uint32_t project_id, uint32_t metric_id,
                       uint32_t report_id, std::string* key) {
  ReportAggregationKey key_data;
  key_data.set_customer_id(customer_id);
  key_data.set_project_id(project_id);
  key_data.set_metric_id(metric_id);
  key_data.set_report_id(report_id);
  return SerializeToBase64(key_data, key);
}

}  // namespace

EventAggregator::EventAggregator(const Encoder* encoder,
                                 const ObservationWriter* observation_writer,
                                 ConsistentProtoStore* local_aggregate_proto_store,
                                 ConsistentProtoStore* obs_history_proto_store,
                                 const size_t backfill_days,
                                 const std::chrono::seconds aggregate_backup_interval,
                                 const std::chrono::seconds generate_obs_interval,
                                 const std::chrono::seconds gc_interval) {
  CHECK_LE(aggregate_backup_interval.count(), generate_obs_interval.count())
      << "aggregate_backup_interval must be less than or equal to "
         "generate_obs_interval";
  CHECK_LE(aggregate_backup_interval.count(), gc_interval.count())
      << "aggregate_backup_interval must be less than or equal to gc_interval";
  aggregate_backup_interval_ = aggregate_backup_interval;
  generate_obs_interval_ = generate_obs_interval;
  gc_interval_ = gc_interval;

  aggregate_store_ =
      std::make_unique<AggregateStore>(encoder, observation_writer, local_aggregate_proto_store,
                                       obs_history_proto_store, backfill_days);

  steady_clock_ = std::make_unique<SteadyClock>();
}

void EventAggregator::Start(std::unique_ptr<util::SystemClockInterface> clock) {
  auto locked = protected_worker_thread_controller_.lock();
  locked->shut_down = false;
  std::thread t(std::bind(
      [this](std::unique_ptr<util::SystemClockInterface>& clock) { this->Run(std::move(clock)); },
      std::move(clock)));
  worker_thread_ = std::move(t);
}

// TODO(pesk): update the EventAggregator's view of a Metric
// or ReportDefinition when appropriate.
Status EventAggregator::UpdateAggregationConfigs(const ProjectContext& project_context) {
  auto locked = aggregate_store_->protected_aggregate_store_.lock();
  Status status;
  for (const auto& metric : project_context.metrics()) {
    switch (metric.metric_type()) {
      case MetricDefinition::EVENT_OCCURRED: {
        for (const auto& report : metric.reports()) {
          switch (report.report_type()) {
            case ReportDefinition::UNIQUE_N_DAY_ACTIVES: {
              status = aggregate_store_->MaybeInsertReportConfigLocked(
                  project_context, metric, report, &(locked->local_aggregate_store));
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
              status = aggregate_store_->MaybeInsertReportConfigLocked(
                  project_context, metric, report, &(locked->local_aggregate_store));
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

Status EventAggregator::LogUniqueActivesEvent(uint32_t report_id, const EventRecord& event_record) {
  auto* event = event_record.event();
  if (!ValidateEventType(Event::kOccurrenceEvent, *event)) {
    return kInvalidArguments;
  }
  auto* metric = event_record.metric();
  std::string key;
  if (!PopulateReportKey(metric->customer_id(), metric->project_id(), metric->id(), report_id,
                         &key)) {
    return kInvalidArguments;
  }
  auto locked = aggregate_store_->protected_aggregate_store_.lock();
  auto aggregates = locked->local_aggregate_store.mutable_by_report_key()->find(key);
  if (aggregates == locked->local_aggregate_store.mutable_by_report_key()->end()) {
    LOG(ERROR) << "The Local Aggregate Store received an unexpected key.";
    return kInvalidArguments;
  }
  if (!aggregates->second.has_unique_actives_aggregates()) {
    LOG(ERROR) << "The local aggregates for this report key are not of type "
                  "UniqueActivesReportAggregates.";
    return kInvalidArguments;
  }
  (*(*aggregates->second.mutable_unique_actives_aggregates()
          ->mutable_by_event_code())[event->occurrence_event().event_code()]
        .mutable_by_day_index())[event->day_index()]
      .mutable_activity_daily_aggregate()
      ->set_activity_indicator(true);
  return kOK;
}

Status EventAggregator::LogCountEvent(uint32_t report_id, const EventRecord& event_record) {
  auto* event = event_record.event();
  if (!ValidateEventType(Event::kCountEvent, *event)) {
    return kInvalidArguments;
  }
  auto* metric = event_record.metric();
  std::string key;
  if (!PopulateReportKey(metric->customer_id(), metric->project_id(), metric->id(), report_id,
                         &key)) {
    return kInvalidArguments;
  }
  const CountEvent& count_event = event->count_event();
  return LogNumericEvent(key, event->day_index(), count_event.component(),
                         config::PackEventCodes(count_event.event_code()), count_event.count());
}

Status EventAggregator::LogElapsedTimeEvent(uint32_t report_id, const EventRecord& event_record) {
  auto* event = event_record.event();
  if (!ValidateEventType(Event::kElapsedTimeEvent, *event)) {
    return kInvalidArguments;
  }
  std::string key;
  auto* metric = event_record.metric();
  if (!PopulateReportKey(metric->customer_id(), metric->project_id(), metric->id(), report_id,
                         &key)) {
    return kInvalidArguments;
  }
  const ElapsedTimeEvent& elapsed_time_event = event->elapsed_time_event();
  return LogNumericEvent(key, event->day_index(), elapsed_time_event.component(),
                         config::PackEventCodes(elapsed_time_event.event_code()),
                         elapsed_time_event.elapsed_micros());
}

Status EventAggregator::LogFrameRateEvent(uint32_t report_id, const EventRecord& event_record) {
  auto* event = event_record.event();
  if (!ValidateEventType(Event::kFrameRateEvent, *event)) {
    return kInvalidArguments;
  }
  std::string key;
  auto* metric = event_record.metric();
  if (!PopulateReportKey(metric->customer_id(), metric->project_id(), metric->id(), report_id,
                         &key)) {
    return kInvalidArguments;
  }
  const FrameRateEvent& frame_rate_event = event->frame_rate_event();
  return LogNumericEvent(key, event->day_index(), frame_rate_event.component(),
                         config::PackEventCodes(frame_rate_event.event_code()),
                         frame_rate_event.frames_per_1000_seconds());
}

Status EventAggregator::LogMemoryUsageEvent(uint32_t report_id, const EventRecord& event_record) {
  auto* event = event_record.event();
  if (!ValidateEventType(Event::kMemoryUsageEvent, *event)) {
    return kInvalidArguments;
  }
  std::string key;
  auto* metric = event_record.metric();
  if (!PopulateReportKey(metric->customer_id(), metric->project_id(), metric->id(), report_id,
                         &key)) {
    return kInvalidArguments;
  }
  const MemoryUsageEvent& memory_usage_event = event->memory_usage_event();
  return LogNumericEvent(key, event->day_index(), memory_usage_event.component(),
                         config::PackEventCodes(memory_usage_event.event_code()),
                         memory_usage_event.bytes());
}

Status EventAggregator::LogNumericEvent(const std::string& report_key, uint32_t day_index,
                                        const std::string& component, uint64_t event_code,
                                        int64_t value) {
  auto locked = aggregate_store_->protected_aggregate_store_.lock();
  auto aggregates = locked->local_aggregate_store.mutable_by_report_key()->find(report_key);
  if (aggregates == locked->local_aggregate_store.mutable_by_report_key()->end()) {
    LOG(ERROR) << "The Local Aggregate Store received an unexpected key.";
    return kInvalidArguments;
  }
  if (!aggregates->second.has_numeric_aggregates()) {
    LOG(ERROR) << "The local aggregates for this report key are not of a "
                  "compatible type.";
    return kInvalidArguments;
  }
  auto aggregates_by_day =
      (*(*aggregates->second.mutable_numeric_aggregates()->mutable_by_component())[component]
            .mutable_by_event_code())[event_code]
          .mutable_by_day_index();
  bool first_event_today = ((*aggregates_by_day).find(day_index) == aggregates_by_day->end());
  auto day_aggregate = (*aggregates_by_day)[day_index].mutable_numeric_daily_aggregate();
  const auto& aggregation_type =
      aggregates->second.aggregation_config().report().aggregation_type();
  switch (aggregation_type) {
    case ReportDefinition::SUM:
      day_aggregate->set_value(value + day_aggregate->value());
      return kOK;
    case ReportDefinition::MAX:
      day_aggregate->set_value(std::max(value, day_aggregate->value()));
      return kOK;
    case ReportDefinition::MIN:
      if (first_event_today) {
        day_aggregate->set_value(value);
      } else {
        day_aggregate->set_value(std::min(value, day_aggregate->value()));
      }
      return kOK;
    default:
      LOG(ERROR) << "Unexpected aggregation type " << aggregation_type;
      return kInvalidArguments;
  }
}

Status EventAggregator::GenerateObservationsNoWorker(uint32_t final_day_index_utc,
                                                     uint32_t final_day_index_local) {
  if (worker_thread_.joinable()) {
    LOG(ERROR) << "GenerateObservationsNoWorker() was called while "
                  "worker thread was running.";
    return kOther;
  }
  return aggregate_store_->GenerateObservations(final_day_index_utc, final_day_index_local);
}

void EventAggregator::ShutDown() {
  if (worker_thread_.joinable()) {
    {
      auto locked = protected_worker_thread_controller_.lock();
      locked->shut_down = true;
      locked->shutdown_notifier.notify_all();
    }
    worker_thread_.join();
  } else {
    protected_worker_thread_controller_.lock()->shut_down = true;
  }
}

void EventAggregator::Run(std::unique_ptr<util::SystemClockInterface> system_clock) {
  std::chrono::steady_clock::time_point steady_time = steady_clock_->now();
  // Schedule Observation generation to happen in the first cycle.
  next_generate_obs_ = steady_time;
  // Schedule garbage collection to happen |gc_interval_| seconds from now.
  next_gc_ = steady_time + gc_interval_;
  // Acquire the mutex protecting the shutdown flag and condition variable.
  auto locked = protected_worker_thread_controller_.lock();
  while (true) {
    // If shutdown has been requested, back up the LocalAggregateStore and
    // exit.
    if (locked->shut_down) {
      aggregate_store_->BackUpLocalAggregateStore();
      return;
    }
    // Sleep until the next scheduled backup of the LocalAggregateStore or
    // until notified of shutdown. Back up the LocalAggregateStore after
    // waking.
    locked->shutdown_notifier.wait_for(locked, aggregate_backup_interval_, [&locked]() {
      if (locked->immediate_run_trigger) {
        locked->immediate_run_trigger = false;
        return true;
      }
      return locked->shut_down;
    });
    aggregate_store_->BackUpLocalAggregateStore();
    // If the worker thread was woken up by a shutdown request, exit.
    // Otherwise, complete any scheduled Observation generation and garbage
    // collection.
    if (locked->shut_down) {
      return;
    }
    // Check whether it is time to generate Observations or to garbage-collect
    // the LocalAggregate store. If so, do that task and schedule the next
    // occurrence.
    DoScheduledTasks(system_clock->now(), steady_clock_->now());
  }
}

void EventAggregator::DoScheduledTasks(std::chrono::system_clock::time_point system_time,
                                       std::chrono::steady_clock::time_point steady_time) {
  auto current_time_t = std::chrono::system_clock::to_time_t(system_time);
  auto yesterday_utc = TimeToDayIndex(current_time_t, MetricDefinition::UTC) - 1;
  auto yesterday_local_time = TimeToDayIndex(current_time_t, MetricDefinition::LOCAL) - 1;

  // Skip the tasks (but do schedule a retry) if either day index is too small.
  uint32_t min_allowed_day_index = kMaxAllowedAggregationDays + aggregate_store_->backfill_days_;
  bool skip_tasks =
      (yesterday_utc < min_allowed_day_index || yesterday_local_time < min_allowed_day_index);
  if (steady_time >= next_generate_obs_) {
    next_generate_obs_ += generate_obs_interval_;
    if (skip_tasks) {
      LOG_FIRST_N(ERROR, 10) << "EventAggregator is skipping Observation generation because the "
                                "current day index is too small.";
    } else {
      auto obs_status = aggregate_store_->GenerateObservations(yesterday_utc, yesterday_local_time);
      if (obs_status == kOK) {
        aggregate_store_->BackUpObservationHistory();
      } else {
        LOG(ERROR) << "GenerateObservations failed with status: " << obs_status;
      }
    }
  }
  if (steady_time >= next_gc_) {
    next_gc_ += gc_interval_;
    if (skip_tasks) {
      LOG_FIRST_N(ERROR, 10) << "EventAggregator is skipping garbage collection because the "
                                "current day index is too small.";
    } else {
      auto gc_status = aggregate_store_->GarbageCollect(yesterday_utc, yesterday_local_time);
      if (gc_status == kOK) {
        aggregate_store_->BackUpLocalAggregateStore();
      } else {
        LOG(ERROR) << "GarbageCollect failed with status: " << gc_status;
      }
    }
  }
}

}  // namespace cobalt::local_aggregation

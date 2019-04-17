// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/event_aggregator.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "algorithms/rappor/rappor_config_helper.h"
#include "config/metric_definition.pb.h"
#include "config/packed_event_codes.h"
#include "logger/project_context.h"
#include "util/datetime_util.h"
#include "util/proto_util.h"
#include "util/status.h"

namespace cobalt {

using google::protobuf::RepeatedField;
using rappor::RapporConfigHelper;
using util::ConsistentProtoStore;
using util::SerializeToBase64;
using util::StatusCode;
using util::SystemClock;
using util::TimeToDayIndex;

namespace logger {

namespace {

// Creates an AggregationConfig from a ProjectContext, MetricDefinition, and
// ReportDefinition and populates the aggregation_config field of a specified
// ReportAggregates. Also sets the type of the ReportAggregates based on the
// ReportDefinition's type.
bool PopulateReportAggregates(const ProjectContext& project_context,
                              const MetricDefinition& metric,
                              const ReportDefinition& report,
                              ReportAggregates* report_aggregates) {
  if (report.window_size_size() == 0) {
    LOG(ERROR) << "Report must have at least one window size.";
    return false;
  }
  AggregationConfig* aggregation_config =
      report_aggregates->mutable_aggregation_config();
  *aggregation_config->mutable_project() = project_context.project();
  *aggregation_config->mutable_metric() =
      *project_context.GetMetric(metric.id());
  *aggregation_config->mutable_report() = report;

  std::vector<uint32_t> window_sizes;
  for (const uint32_t window_size : report.window_size()) {
    if (window_size == 0 ||
        window_size > EventAggregator::kMaxAllowedAggregationWindowSize) {
      LOG(ERROR) << "Window size must be positive and cannot exceed "
                 << EventAggregator::kMaxAllowedAggregationWindowSize;
      return false;
    }
    window_sizes.push_back(window_size);
  }
  std::sort(window_sizes.begin(), window_sizes.end());
  for (const uint32_t window_size : window_sizes) {
    aggregation_config->add_window_size(window_size);
  }

  switch (report.report_type()) {
    case ReportDefinition::UNIQUE_N_DAY_ACTIVES: {
      report_aggregates->set_allocated_unique_actives_aggregates(
          new UniqueActivesReportAggregates);
      return true;
    }
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
      report_aggregates->set_allocated_numeric_aggregates(
          new PerDeviceNumericAggregates);
      return true;
    }
    default:
      return false;
  }
}

// Populates a ReportAggregationKey proto message and then populates a string
// with the base64 encoding of the serialized proto.
bool PopulateReportKey(uint32_t customer_id, uint32_t project_id,
                       uint32_t metric_id, uint32_t report_id,
                       std::string* key) {
  ReportAggregationKey key_data;
  key_data.set_customer_id(customer_id);
  key_data.set_project_id(project_id);
  key_data.set_metric_id(metric_id);
  key_data.set_report_id(report_id);
  return SerializeToBase64(key_data, key);
}

// Given a ProjectContext, MetricDefinition, and ReportDefinition and a pointer
// to the LocalAggregateStore, checks whether a key with the same customer,
// project, metric, and report ID already exists in the LocalAggregateStore. If
// not, creates and inserts a new key and value. Returns kInvalidArguments if
// creation of the key or value fails, and kOK otherwise. The caller should hold
// the mutex protecting the LocalAggregateStore.
Status MaybeInsertReportConfig(const ProjectContext& project_context,
                               const MetricDefinition& metric,
                               const ReportDefinition& report,
                               LocalAggregateStore* store) {
  std::string key;
  if (!PopulateReportKey(project_context.project().customer_id(),
                         project_context.project().project_id(), metric.id(),
                         report.id(), &key)) {
    return kInvalidArguments;
  }
  ReportAggregates report_aggregates;
  if (store->by_report_key().count(key) == 0) {
    if (!PopulateReportAggregates(project_context, metric, report,
                                  &report_aggregates)) {
      return kInvalidArguments;
    }
    (*store->mutable_by_report_key())[key] = report_aggregates;
  }
  return kOK;
}

RepeatedField<uint32_t> UnpackEventCodesProto(uint64_t packed_event_codes) {
  RepeatedField<uint32_t> fields;
  for (auto code : config::UnpackEventCodes(packed_event_codes)) {
    *fields.Add() = code;
  }
  return fields;
}

}  // namespace

EventAggregator::EventAggregator(
    const Encoder* encoder, const ObservationWriter* observation_writer,
    ConsistentProtoStore* local_aggregate_proto_store,
    ConsistentProtoStore* obs_history_proto_store, const size_t backfill_days,
    const std::chrono::seconds aggregate_backup_interval,
    const std::chrono::seconds generate_obs_interval,
    const std::chrono::seconds gc_interval)
    : encoder_(encoder),
      observation_writer_(observation_writer),
      local_aggregate_proto_store_(local_aggregate_proto_store),
      obs_history_proto_store_(obs_history_proto_store) {
  CHECK_LE(aggregate_backup_interval.count(), generate_obs_interval.count())
      << "aggregate_backup_interval must be less than or equal to "
         "generate_obs_interval";
  CHECK_LE(aggregate_backup_interval.count(), gc_interval.count())
      << "aggregate_backup_interval must be less than or equal to gc_interval";
  CHECK_LE(backfill_days, kMaxAllowedBackfillDays)
      << "backfill_days must be less than or equal to "
      << kMaxAllowedBackfillDays;
  aggregate_backup_interval_ = aggregate_backup_interval;
  generate_obs_interval_ = generate_obs_interval;
  gc_interval_ = gc_interval;
  backfill_days_ = backfill_days;
  auto locked = protected_aggregate_store_.lock();
  auto restore_aggregates_status =
      local_aggregate_proto_store_->Read(&(locked->local_aggregate_store));
  switch (restore_aggregates_status.error_code()) {
    case StatusCode::OK: {
      VLOG(4) << "Read LocalAggregateStore from disk.";
      break;
    }
    case StatusCode::NOT_FOUND: {
      VLOG(4) << "No file found for local_aggregate_proto_store. Proceeding "
                 "with empty LocalAggregateStore. File will be created on "
                 "first snapshot of the LocalAggregateStore.";
      break;
    }
    default: {
      LOG(ERROR)
          << "Read to local_aggregate_proto_store failed with status code: "
          << restore_aggregates_status.error_code()
          << "\nError message: " << restore_aggregates_status.error_message()
          << "\nError details: " << restore_aggregates_status.error_details()
          << "\nProceeding with empty LocalAggregateStore.";
      locked->local_aggregate_store = LocalAggregateStore();
    }
  }
  auto restore_history_status = obs_history_proto_store_->Read(&obs_history_);
  switch (restore_history_status.error_code()) {
    case StatusCode::OK: {
      VLOG(4) << "Read AggregatedObservationHistoryStore from disk.";
      break;
    }
    case StatusCode::NOT_FOUND: {
      VLOG(4) << "No file found for obs_history_proto_store. Proceeding "
                 "with empty AggregatedObservationHistoryStore. File will be "
                 "created on first snapshot of the "
                 "AggregatedObservationHistoryStore.";
      break;
    }
    default: {
      LOG(ERROR) << "Read to obs_history_proto_store failed with status code: "
                 << restore_history_status.error_code() << "\nError message: "
                 << restore_history_status.error_message()
                 << "\nError details: "
                 << restore_history_status.error_details()
                 << "\nProceeding with empty AggregatedObservationHistory.";
      obs_history_ = AggregatedObservationHistoryStore();
    }
  }
  clock_.reset(new SystemClock());
}

void EventAggregator::Start() {
  auto locked = protected_shutdown_flag_.lock();
  locked->shut_down = false;
  std::thread t([this] { this->Run(); });
  worker_thread_ = std::move(t);
}

// TODO(pesk): update the EventAggregator's view of a Metric
// or ReportDefinition when appropriate.
Status EventAggregator::UpdateAggregationConfigs(
    const ProjectContext& project_context) {
  auto locked = protected_aggregate_store_.lock();
  Status status;
  for (const auto& metric : project_context.metrics()) {
    switch (metric.metric_type()) {
      case MetricDefinition::EVENT_OCCURRED: {
        for (const auto& report : metric.reports()) {
          switch (report.report_type()) {
            case ReportDefinition::UNIQUE_N_DAY_ACTIVES: {
              status =
                  MaybeInsertReportConfig(project_context, metric, report,
                                          &(locked->local_aggregate_store));
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
      case MetricDefinition::ELAPSED_TIME: {
        for (const auto& report : metric.reports()) {
          switch (report.report_type()) {
            case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
              status =
                  MaybeInsertReportConfig(project_context, metric, report,
                                          &(locked->local_aggregate_store));
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

Status EventAggregator::LogUniqueActivesEvent(uint32_t report_id,
                                              EventRecord* event_record) {
  if (!event_record->event->has_occurrence_event()) {
    LOG(ERROR) << "EventAggregator::LogUniqueActivesEvent can only "
                  "accept OccurrenceEvents.";
    return kInvalidArguments;
  }
  std::string key;
  if (!PopulateReportKey(event_record->metric->customer_id(),
                         event_record->metric->project_id(),
                         event_record->metric->id(), report_id, &key)) {
    return kInvalidArguments;
  }
  auto locked = protected_aggregate_store_.lock();
  auto aggregates =
      locked->local_aggregate_store.mutable_by_report_key()->find(key);
  if (aggregates ==
      locked->local_aggregate_store.mutable_by_report_key()->end()) {
    LOG(ERROR) << "The Local Aggregate Store received an unexpected key.";
    return kInvalidArguments;
  }
  if (!aggregates->second.has_unique_actives_aggregates()) {
    LOG(ERROR) << "The local aggregates for this report key are not of type "
                  "UniqueActivesReportAggregates.";
    return kInvalidArguments;
  }
  (*(*aggregates->second.mutable_unique_actives_aggregates()
          ->mutable_by_event_code())[event_record->event->occurrence_event()
                                         .event_code()]
        .mutable_by_day_index())[event_record->event->day_index()]
      .mutable_activity_daily_aggregate()
      ->set_activity_indicator(true);
  return kOK;
}

Status EventAggregator::LogCountEvent(uint32_t report_id,
                                      EventRecord* event_record) {
  if (!event_record->event->has_count_event()) {
    LOG(ERROR) << "EventAggregator::LogCountEvent can only accept "
                  "CountEvents.";
    return kInvalidArguments;
  }
  std::string key;
  if (!PopulateReportKey(event_record->metric->customer_id(),
                         event_record->metric->project_id(),
                         event_record->metric->id(), report_id, &key)) {
    return kInvalidArguments;
  }
  const CountEvent& count_event = event_record->event->count_event();
  return LogNumericEvent(
      key, event_record->event->day_index(), count_event.component(),
      config::PackEventCodes(count_event.event_code()), count_event.count());
}

Status EventAggregator::LogElapsedTimeEvent(uint32_t report_id,
                                            EventRecord* event_record) {
  if (event_record->event->type_case() != Event::kElapsedTimeEvent) {
    LOG(ERROR) << "EventAggregator: LogElapsedTimeEvent can only accept "
                  "ElapsedTimeEvents.";
    return kInvalidArguments;
  }
  std::string key;
  if (!PopulateReportKey(event_record->metric->customer_id(),
                         event_record->metric->project_id(),
                         event_record->metric->id(), report_id, &key)) {
    return kInvalidArguments;
  }
  const ElapsedTimeEvent& elapsed_time_event =
      event_record->event->elapsed_time_event();
  return LogNumericEvent(
      key, event_record->event->day_index(), elapsed_time_event.component(),
      config::PackEventCodes(elapsed_time_event.event_code()),
      elapsed_time_event.elapsed_micros());
}

Status EventAggregator::LogNumericEvent(const std::string& report_key,
                                        uint32_t day_index,
                                        const std::string& component,
                                        uint64_t event_code, int64_t value) {
  auto locked = protected_aggregate_store_.lock();
  auto aggregates =
      locked->local_aggregate_store.mutable_by_report_key()->find(report_key);
  if (aggregates ==
      locked->local_aggregate_store.mutable_by_report_key()->end()) {
    LOG(ERROR) << "The Local Aggregate Store received an unexpected key.";
    return kInvalidArguments;
  }
  if (!aggregates->second.has_numeric_aggregates()) {
    LOG(ERROR) << "The local aggregates for this report key are not of a "
                  "compatible type.";
    return kInvalidArguments;
  }
  auto daily_aggregate = (*(*(*aggregates->second.mutable_numeric_aggregates()
                                   ->mutable_by_component())[component]
                                 .mutable_by_event_code())[event_code]
                               .mutable_by_day_index())[day_index]
                             .mutable_numeric_daily_aggregate();
  daily_aggregate->set_sum(daily_aggregate->sum() + value);
  return kOK;
}

Status EventAggregator::GenerateObservationsNoWorker(
    uint32_t final_day_index_utc, uint32_t final_day_index_local) {
  if (worker_thread_.joinable()) {
    LOG(ERROR) << "GenerateObservationsNoWorker() was called while "
                  "worker thread was running.";
    return kOther;
  }
  return GenerateObservations(final_day_index_utc, final_day_index_local);
}

Status EventAggregator::BackUpLocalAggregateStore() {
  // Lock, copy the LocalAggregateStore, and release the lock. Write the copy
  // to |local_aggregate_proto_store_|.
  auto local_aggregate_store = CopyLocalAggregateStore();
  auto status = local_aggregate_proto_store_->Write(local_aggregate_store);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to back up the LocalAggregateStore with error code: "
               << status.error_code()
               << "\nError message: " << status.error_message()
               << "\nError details: " << status.error_details();
    return kOther;
  }
  return kOK;
}

Status EventAggregator::BackUpObservationHistory() {
  auto status = obs_history_proto_store_->Write(obs_history_);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to back up the AggregatedObservationHistoryStore. "
                  "::cobalt::util::Status error code: "
               << status.error_code()
               << "\nError message: " << status.error_message()
               << "\nError details: " << status.error_details();
    return kOther;
  }
  return kOK;
}

void EventAggregator::ShutDown() {
  if (worker_thread_.joinable()) {
    {
      auto locked = protected_shutdown_flag_.lock();
      locked->shut_down = true;
      locked->shutdown_notifier.notify_all();
    }
    worker_thread_.join();
  } else {
    protected_shutdown_flag_.lock()->shut_down = true;
  }
}

void EventAggregator::Run() {
  auto current_time = clock_->now();
  // Schedule Observation generation to happen in the first cycle.
  next_generate_obs_ = current_time;
  // Schedule garbage collection to happen |gc_interval_| seconds from now.
  next_gc_ = current_time + gc_interval_;
  // Acquire the mutex protecting the shutdown flag and condition variable.
  auto locked = protected_shutdown_flag_.lock();
  while (true) {
    // If shutdown has been requested, back up the LocalAggregateStore and
    // exit.
    if (locked->shut_down) {
      BackUpLocalAggregateStore();
      return;
    }
    // Sleep until the next scheduled backup of the LocalAggregateStore or
    // until notified of shutdown. Back up the LocalAggregateStore after
    // waking.
    auto shutdown_requested = locked.wait_for_with(
        &(locked->shutdown_notifier), aggregate_backup_interval_,
        [&locked]() { return locked->shut_down; });
    BackUpLocalAggregateStore();
    // If the worker thread was woken up by a shutdown request, exit.
    // Otherwise, complete any scheduled Observation generation and garbage
    // collection.
    if (shutdown_requested) {
      return;
    }
    // Check whether it is time to generate Observations or to garbage-collect
    // the LocalAggregate store. If so, do that task and schedule the next
    // occurrence.
    DoScheduledTasks(clock_->now());
  }
}

void EventAggregator::DoScheduledTasks(
    std::chrono::system_clock::time_point current_time) {
  auto current_time_t = std::chrono::system_clock::to_time_t(current_time);
  auto current_day_index_utc =
      TimeToDayIndex(current_time_t, MetricDefinition::UTC);
  auto current_day_index_local =
      TimeToDayIndex(current_time_t, MetricDefinition::LOCAL);
  if (current_time >= next_generate_obs_) {
    auto obs_status = GenerateObservations(current_day_index_utc - 1,
                                           current_day_index_local - 1);
    if (obs_status == kOK) {
      BackUpObservationHistory();
    } else {
      LOG(ERROR) << "GenerateObservations failed with status: " << obs_status;
    }
    next_generate_obs_ += generate_obs_interval_;
  }
  if (current_time >= next_gc_) {
    auto gc_status =
        GarbageCollect(current_day_index_utc - 1, current_day_index_local - 1);
    if (gc_status == kOK) {
      BackUpLocalAggregateStore();
    } else {
      LOG(ERROR) << "GarbageCollect failed with status: " << gc_status;
    }
    next_gc_ += gc_interval_;
  }
}

Status EventAggregator::GenerateObservations(uint32_t final_day_index_utc,
                                             uint32_t final_day_index_local) {
  if (final_day_index_local == 0u) {
    final_day_index_local = final_day_index_utc;
  }
  if (std::min(final_day_index_utc, final_day_index_local) <
      backfill_days_ + kMaxAllowedAggregationWindowSize) {
    LOG(ERROR) << "GenerateObservations: Day index of Observation must be >= "
                  "backfill_days_ + kMaxAllowedAggregationWindowSize.";
    return kInvalidArguments;
  }
  // Lock, copy the LocalAggregateStore, and release the lock. Use the copy to
  // generate observations.
  auto local_aggregate_store = CopyLocalAggregateStore();
  for (auto pair : local_aggregate_store.by_report_key()) {
    const auto& config = pair.second.aggregation_config();

    const auto& metric = config.metric();
    auto metric_ref = MetricRef(&config.project(), &metric);
    uint32_t final_day_index;
    switch (metric.time_zone_policy()) {
      case MetricDefinition::UTC: {
        final_day_index = final_day_index_utc;
        break;
      }
      case MetricDefinition::LOCAL: {
        final_day_index = final_day_index_local;
        break;
      }
      default:
        LOG(ERROR) << "The TimeZonePolicy of this MetricDefinition is invalid.";
        return kInvalidConfig;
    }

    const auto& report = config.report();
    // PopulateReportAggregates has checked that there is at least one window
    // size and that all window sizes are positive and <=
    // kMaxAllowedAggregationWindowSize, and has sorted the elements of
    // config.window_size() in increasing order.
    auto max_window_size = config.window_size(config.window_size_size() - 1);
    if (final_day_index < max_window_size) {
      LOG(ERROR) << "final_day_index must be >= max_window_size.";
      return kInvalidArguments;
    }

    switch (metric.metric_type()) {
      case MetricDefinition::EVENT_OCCURRED: {
        auto num_event_codes =
            RapporConfigHelper::BasicRapporNumCategories(metric);

        switch (report.report_type()) {
          case ReportDefinition::UNIQUE_N_DAY_ACTIVES: {
            auto status = GenerateUniqueActivesObservations(
                metric_ref, pair.first, pair.second, num_event_codes,
                final_day_index);
            if (status != kOK) {
              return status;
            }
            break;
          }
          default:
            continue;
        }
        break;
      }
      case MetricDefinition::EVENT_COUNT:
      case MetricDefinition::ELAPSED_TIME: {
        switch (report.report_type()) {
          case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
            auto status = GeneratePerDeviceNumericObservations(
                metric_ref, pair.first, pair.second, final_day_index);
            if (status != kOK) {
              return status;
            }
            break;
          }
          default:
            continue;
        }
        break;
      }
      default:
        continue;
    }
  }
  return kOK;
}

Status EventAggregator::GarbageCollect(uint32_t day_index_utc,
                                       uint32_t day_index_local) {
  if (day_index_local == 0u) {
    day_index_local = day_index_utc;
  }
  if (std::min(day_index_utc, day_index_local) <
      backfill_days_ + kMaxAllowedAggregationWindowSize) {
    LOG(ERROR) << "GarbageCollect: Day index must be >= backfill_days_ + "
                  "kMaxAllowedAggregationWindowSize.";
    return kInvalidArguments;
  }
  auto locked = protected_aggregate_store_.lock();
  for (auto pair : locked->local_aggregate_store.by_report_key()) {
    uint32_t day_index;
    switch (pair.second.aggregation_config().metric().time_zone_policy()) {
      case MetricDefinition::UTC: {
        day_index = day_index_utc;
        break;
      }
      case MetricDefinition::LOCAL: {
        day_index = day_index_local;
        break;
      }
      default:
        LOG(ERROR) << "The TimeZonePolicy of this MetricDefinition is invalid.";
        return kInvalidConfig;
    }
    uint32_t max_window_size = pair.second.aggregation_config().window_size(
        pair.second.aggregation_config().window_size_size() - 1);
    // For each ReportAggregates, descend to and iterate over the sub-map of
    // local aggregates keyed by day index. Keep buckets with day indices
    // greater than |day_index| - |backfill_days_| - |max_window_size|, and
    // remove all buckets with smaller day indices.
    switch (pair.second.type_case()) {
      case ReportAggregates::kUniqueActivesAggregates: {
        for (auto event_code_aggregates :
             pair.second.unique_actives_aggregates().by_event_code()) {
          for (auto day_aggregates :
               event_code_aggregates.second.by_day_index()) {
            if (day_aggregates.first <=
                day_index - backfill_days_ - max_window_size) {
              locked->local_aggregate_store.mutable_by_report_key()
                  ->at(pair.first)
                  .mutable_unique_actives_aggregates()
                  ->mutable_by_event_code()
                  ->at(event_code_aggregates.first)
                  .mutable_by_day_index()
                  ->erase(day_aggregates.first);
            }
          }
          // If the day index map under this event code is empty, remove the
          // event code from the event code-keyed map under this
          // ReportAggregationKey.
          if (locked->local_aggregate_store.by_report_key()
                  .at(pair.first)
                  .unique_actives_aggregates()
                  .by_event_code()
                  .at(event_code_aggregates.first)
                  .by_day_index()
                  .empty()) {
            locked->local_aggregate_store.mutable_by_report_key()
                ->at(pair.first)
                .mutable_unique_actives_aggregates()
                ->mutable_by_event_code()
                ->erase(event_code_aggregates.first);
          }
        }
        break;
      }
      case ReportAggregates::kNumericAggregates: {
        for (auto component_aggregates :
             pair.second.numeric_aggregates().by_component()) {
          for (auto event_code_aggregates :
               component_aggregates.second.by_event_code()) {
            for (auto day_aggregates :
                 event_code_aggregates.second.by_day_index()) {
              if (day_aggregates.first <=
                  day_index - backfill_days_ - max_window_size) {
                locked->local_aggregate_store.mutable_by_report_key()
                    ->at(pair.first)
                    .mutable_numeric_aggregates()
                    ->mutable_by_component()
                    ->at(component_aggregates.first)
                    .mutable_by_event_code()
                    ->at(event_code_aggregates.first)
                    .mutable_by_day_index()
                    ->erase(day_aggregates.first);
              }
            }
            // If the day index map under this event code is empty, remove the
            // event code from the event code-keyed map under this
            // ReportAggregationKey.
            if (locked->local_aggregate_store.by_report_key()
                    .at(pair.first)
                    .numeric_aggregates()
                    .by_component()
                    .at(component_aggregates.first)
                    .by_event_code()
                    .at(event_code_aggregates.first)
                    .by_day_index()
                    .empty()) {
              locked->local_aggregate_store.mutable_by_report_key()
                  ->at(pair.first)
                  .mutable_numeric_aggregates()
                  ->mutable_by_component()
                  ->at(component_aggregates.first)
                  .mutable_by_event_code()
                  ->erase(event_code_aggregates.first);
            }
          }
          // If the event code map under this component string is empty,
          // remove the component string from the component-keyed map under
          // this ReportAggregationKey.
          if (locked->local_aggregate_store.by_report_key()
                  .at(pair.first)
                  .numeric_aggregates()
                  .by_component()
                  .at(component_aggregates.first)
                  .by_event_code()
                  .empty()) {
            locked->local_aggregate_store.mutable_by_report_key()
                ->at(pair.first)
                .mutable_numeric_aggregates()
                ->mutable_by_component()
                ->erase(component_aggregates.first);
          }
        }
        break;
      }
      default:
        continue;
    }
  }
  return kOK;
}

////////// GenerateUniqueActivesObservations and helper methods ////////////////

// Given the set of daily aggregates for a fixed event code, and the size and
// end date of an aggregation window, returns the first day index within that
// window on which the event code occurred. Returns 0 if the event code did
// not occur within the window.
uint32_t FirstActiveDayIndexInWindow(const DailyAggregates& daily_aggregates,
                                     uint32_t obs_day_index,
                                     uint32_t window_size) {
  for (uint32_t day_index = obs_day_index - window_size + 1;
       day_index <= obs_day_index; day_index++) {
    auto day_aggregate = daily_aggregates.by_day_index().find(day_index);
    if (day_aggregate != daily_aggregates.by_day_index().end() &&
        day_aggregate->second.activity_daily_aggregate().activity_indicator() ==
            true) {
      return day_index;
    }
  }
  return 0u;
}

// Given the day index of an event occurrence and the size and end date
// of an aggregation window, returns true if the occurrence falls within
// the window and false if not.
bool IsActivityInWindow(uint32_t active_day_index, uint32_t obs_day_index,
                        uint32_t window_size) {
  return (active_day_index <= obs_day_index &&
          active_day_index > obs_day_index - window_size);
}

uint32_t EventAggregator::UniqueActivesLastGeneratedDayIndex(
    const std::string& report_key, uint32_t event_code,
    uint32_t window_size) const {
  auto report_history = obs_history_.by_report_key().find(report_key);
  if (report_history == obs_history_.by_report_key().end()) {
    return 0u;
  }
  auto event_code_history =
      report_history->second.unique_actives_history().by_event_code().find(
          event_code);
  if (event_code_history ==
      report_history->second.unique_actives_history().by_event_code().end()) {
    return 0u;
  }
  auto window_size_history =
      event_code_history->second.by_window_size().find(window_size);
  if (window_size_history ==
      event_code_history->second.by_window_size().end()) {
    return 0u;
  }
  return window_size_history->second;
}

Status EventAggregator::GenerateSingleUniqueActivesObservation(
    const MetricRef metric_ref, const ReportDefinition* report,
    uint32_t obs_day_index, uint32_t event_code, uint32_t window_size,
    bool was_active) const {
  auto encoder_result = encoder_->EncodeUniqueActivesObservation(
      metric_ref, report, obs_day_index, event_code, was_active, window_size);
  if (encoder_result.status != kOK) {
    return encoder_result.status;
  }
  if (encoder_result.observation == nullptr ||
      encoder_result.metadata == nullptr) {
    LOG(ERROR) << "Failed to encode UniqueActivesObservation";
    return kOther;
  }

  auto writer_status = observation_writer_->WriteObservation(
      *encoder_result.observation, std::move(encoder_result.metadata));
  if (writer_status != kOK) {
    return writer_status;
  }
  return kOK;
}

Status EventAggregator::GenerateUniqueActivesObservations(
    const MetricRef metric_ref, const std::string& report_key,
    const ReportAggregates& report_aggregates, uint32_t num_event_codes,
    uint32_t final_day_index) {
  // The earliest day index for which we might need to generate an
  // Observation. GenerateObservations() has checked that this value is > 0.
  auto backfill_period_start = uint32_t(final_day_index - backfill_days_);

  for (uint32_t event_code = 0; event_code < num_event_codes; event_code++) {
    auto daily_aggregates =
        report_aggregates.unique_actives_aggregates().by_event_code().find(
            event_code);
    // Have any events ever been logged for this report and event code?
    bool found_event_code =
        (daily_aggregates !=
         report_aggregates.unique_actives_aggregates().by_event_code().end());
    for (uint32_t window_size :
         report_aggregates.aggregation_config().report().window_size()) {
      // Skip any window size larger than
      // kMaxAllowedAggregationWindowSize.
      if (window_size > kMaxAllowedAggregationWindowSize) {
        LOG(WARNING) << "GenerateUniqueActivesObservations ignoring a window "
                        "size exceeding the maximum allowed value";
        continue;
      }
      // Find the earliest day index for which an Observation has not yet
      // been generated for this report, event code, and window size. If
      // that day index is later than |final_day_index|, no Observation is
      // generated on this invocation.
      auto last_gen = UniqueActivesLastGeneratedDayIndex(report_key, event_code,
                                                         window_size);
      auto first_day_index = std::max(last_gen + 1, backfill_period_start);
      // The latest day index on which |event_type| is known to have
      // occurred, so far. This value will be updated as we search
      // forward from the earliest day index belonging to a window of
      // interest.
      uint32_t active_day_index = 0u;
      // Iterate over the day indices |obs_day_index| for which we need
      // to generate Observations. On each iteration, generate an
      // Observation for the window of size |window_size| ending on
      // |obs_day_index|.
      for (uint32_t obs_day_index = first_day_index;
           obs_day_index <= final_day_index; obs_day_index++) {
        bool was_active = false;
        if (found_event_code) {
          // If the current value of |active_day_index| falls within the
          // window, generate an Observation of activity. If not, search
          // forward in the window, update |active_day_index|, and generate an
          // Observation of activity or inactivity depending on the result of
          // the search.
          if (IsActivityInWindow(active_day_index, obs_day_index,
                                 window_size)) {
            was_active = true;
          } else {
            active_day_index = FirstActiveDayIndexInWindow(
                daily_aggregates->second, obs_day_index, window_size);
            was_active = IsActivityInWindow(active_day_index, obs_day_index,
                                            window_size);
          }
        }
        auto status = GenerateSingleUniqueActivesObservation(
            metric_ref, &report_aggregates.aggregation_config().report(),
            obs_day_index, event_code, window_size, was_active);
        if (status != kOK) {
          return status;
        }
        // Update |obs_history_| with the latest date of Observation
        // generation for this report, event code, and window size.
        (*(*(*obs_history_.mutable_by_report_key())[report_key]
                .mutable_unique_actives_history()
                ->mutable_by_event_code())[event_code]
              .mutable_by_window_size())[window_size] = obs_day_index;
      }
    }
  }
  return kOK;
}

////////// GeneratePerDeviceNumericObservations and helper methods /////////////

uint32_t EventAggregator::PerDeviceNumericLastGeneratedDayIndex(
    const std::string& report_key, const std::string& component,
    uint32_t event_code, uint32_t window_size) const {
  const auto& report_history = obs_history_.by_report_key().find(report_key);
  if (report_history == obs_history_.by_report_key().end()) {
    return 0u;
  }
  if (!report_history->second.has_per_device_numeric_history()) {
    return 0u;
  }
  const auto& component_history =
      report_history->second.per_device_numeric_history().by_component().find(
          component);
  if (component_history == report_history->second.per_device_numeric_history()
                               .by_component()
                               .end()) {
    return 0u;
  }
  const auto& event_code_history =
      component_history->second.by_event_code().find(event_code);
  if (event_code_history == component_history->second.by_event_code().end()) {
    return 0u;
  }
  const auto& window_size_history =
      event_code_history->second.by_window_size().find(window_size);
  if (window_size_history ==
      event_code_history->second.by_window_size().end()) {
    return 0u;
  }
  return window_size_history->second;
}

Status EventAggregator::GenerateSinglePerDeviceNumericObservation(
    const MetricRef metric_ref, const ReportDefinition* report,
    uint32_t obs_day_index, const std::string& component, uint32_t event_code,
    uint32_t window_size, int64_t sum) const {
  auto encoder_result = encoder_->EncodePerDeviceNumericObservation(
      metric_ref, report, obs_day_index, component,
      UnpackEventCodesProto(event_code), sum, window_size);
  if (encoder_result.status != kOK) {
    return encoder_result.status;
  }
  if (encoder_result.observation == nullptr ||
      encoder_result.metadata == nullptr) {
    LOG(ERROR) << "Failed to encode PerDeviceNumericObservation";
    return kOther;
  }

  const auto& writer_status = observation_writer_->WriteObservation(
      *encoder_result.observation, std::move(encoder_result.metadata));
  if (writer_status != kOK) {
    return writer_status;
  }
  return kOK;
}

Status EventAggregator::GeneratePerDeviceNumericObservations(
    const MetricRef metric_ref, const std::string& report_key,
    const ReportAggregates& report_aggregates, uint32_t final_day_index) {
  // The first day index for which we might have to generate an Observation.
  // GenerateObservations() has checked that this value is > 0.
  auto backfill_period_start = uint32_t(final_day_index - backfill_days_);

  // Generate any necessary PerDeviceNumericObservations for this report.
  for (const auto& component_pair :
       report_aggregates.numeric_aggregates().by_component()) {
    const auto& component = component_pair.first;
    for (const auto& event_code_pair : component_pair.second.by_event_code()) {
      auto event_code = event_code_pair.first;
      const auto& event_code_aggregates = event_code_pair.second;
      // Populate a helper map keyed by day indices which belong to the range
      // [|backfill_period_start|, |final_day_index|]. The value at a day
      // index is the list of window sizes, in increasing order, for which an
      // Observation should be generated for that day index.
      std::map<uint32_t, std::vector<uint32_t>> window_sizes_by_obs_day;
      for (auto window_size :
           report_aggregates.aggregation_config().window_size()) {
        auto last_gen = PerDeviceNumericLastGeneratedDayIndex(
            report_key, component, event_code, window_size);
        auto first_day_index = std::max(last_gen + 1, backfill_period_start);
        for (auto obs_day_index = first_day_index;
             obs_day_index <= final_day_index; obs_day_index++) {
          window_sizes_by_obs_day[obs_day_index].push_back(window_size);
        }
      }
      // Iterate over the day indices |obs_day_index| for which we might need
      // to generate an Observation. For each day index, generate an
      // Observation for each needed window size.
      //
      // More precisely, for each needed window size, compute the sum over
      // the window and generate a PerDeviceNumericObservation only if the sum
      // is nonzero. Whether or not the sum was zero, update the
      // AggregatedObservationHistory for this report, component, event code,
      // and window size with |obs_day_index| as the most recent date of
      // Observation generation. This reflects the fact that the sum was
      // computed for the window ending on that date, even though an
      // Observation is only sent if the sum is nonzero.
      for (auto obs_day_index = backfill_period_start;
           obs_day_index <= final_day_index; obs_day_index++) {
        const auto& window_sizes = window_sizes_by_obs_day.find(obs_day_index);
        if (window_sizes == window_sizes_by_obs_day.end()) {
          continue;
        }
        int64_t sum = 0;
        uint32_t num_days = 0;
        for (auto window_size : window_sizes->second) {
          while (num_days < window_size) {
            const auto& day_aggregates =
                event_code_aggregates.by_day_index().find(obs_day_index -
                                                          num_days);
            if (day_aggregates != event_code_aggregates.by_day_index().end()) {
              sum += day_aggregates->second.numeric_daily_aggregate().sum();
            }
            num_days++;
          }
          if (sum != 0) {
            auto status = GenerateSinglePerDeviceNumericObservation(
                metric_ref, &report_aggregates.aggregation_config().report(),
                obs_day_index, component, event_code, window_size, sum);
            if (status != kOK) {
              return status;
            }
          }
          // Update |obs_history_| with the latest date of Observation
          // generation for this report, component, event code, and window
          // size.
          (*(*(*(*obs_history_.mutable_by_report_key())[report_key]
                    .mutable_per_device_numeric_history()
                    ->mutable_by_component())[component]
                  .mutable_by_event_code())[event_code]
                .mutable_by_window_size())[window_size] = obs_day_index;
        }
      }
    }
  }
  // Generate any necessary ReportParticipationObservations for this report.
  auto participation_last_gen =
      ReportParticipationLastGeneratedDayIndex(report_key);
  auto participation_first_day_index =
      std::max(participation_last_gen + 1, backfill_period_start);
  for (auto obs_day_index = participation_first_day_index;
       obs_day_index <= final_day_index; obs_day_index++) {
    GenerateSingleReportParticipationObservation(
        metric_ref, &report_aggregates.aggregation_config().report(),
        obs_day_index);
    (*obs_history_.mutable_by_report_key())[report_key]
        .mutable_report_participation_history()
        ->set_last_generated(obs_day_index);
  }
  return kOK;
}

uint32_t EventAggregator::ReportParticipationLastGeneratedDayIndex(
    const std::string& report_key) const {
  const auto& report_history = obs_history_.by_report_key().find(report_key);
  if (report_history == obs_history_.by_report_key().end()) {
    return 0u;
  }
  return report_history->second.report_participation_history().last_generated();
}

Status EventAggregator::GenerateSingleReportParticipationObservation(
    const MetricRef metric_ref, const ReportDefinition* report,
    uint32_t obs_day_index) const {
  auto encoder_result = encoder_->EncodeReportParticipationObservation(
      metric_ref, report, obs_day_index);
  if (encoder_result.status != kOK) {
    return encoder_result.status;
  }
  if (encoder_result.observation == nullptr ||
      encoder_result.metadata == nullptr) {
    LOG(ERROR) << "Failed to encode ReportParticipationObservation";
    return kOther;
  }

  const auto& writer_status = observation_writer_->WriteObservation(
      *encoder_result.observation, std::move(encoder_result.metadata));
  if (writer_status != kOK) {
    return writer_status;
  }
  return kOK;
}

}  // namespace logger
}  // namespace cobalt

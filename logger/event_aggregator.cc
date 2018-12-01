// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/event_aggregator.h"

#include <algorithm>
#include <string>
#include <utility>

#include "algorithms/rappor/rappor_config_helper.h"
#include "config/metric_definition.pb.h"
#include "logger/project_context.h"
#include "util/proto_util.h"
#include "util/status.h"

namespace cobalt {

using rappor::RapporConfigHelper;
using util::ConsistentProtoStore;
using util::SerializeToBase64;
using util::StatusCode;

namespace logger {

EventAggregator::EventAggregator(
    const Encoder* encoder, const ObservationWriter* observation_writer,
    ConsistentProtoStore* local_aggregate_proto_store,
    ConsistentProtoStore* obs_history_proto_store, const size_t backfill_days)
    : encoder_(encoder),
      observation_writer_(observation_writer),
      local_aggregate_proto_store_(local_aggregate_proto_store),
      obs_history_proto_store_(obs_history_proto_store) {
  CHECK_LE(backfill_days, kEventAggregatorMaxAllowedBackfillDays)
      << "backfill_days must be less than or equal to "
      << kEventAggregatorMaxAllowedBackfillDays;
  backfill_days_ = backfill_days;
  auto restore_aggregates_status =
      local_aggregate_proto_store_->Read(&local_aggregate_store_);
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
      local_aggregate_store_ = LocalAggregateStore();
    }
  }
  auto restore_history_status = obs_history_proto_store_->Read(&obs_history_);
  switch (restore_history_status.error_code()) {
    case StatusCode::OK: {
      VLOG(4) << "Read AggregatedObservationHistory from disk.";
      break;
    }
    case StatusCode::NOT_FOUND: {
      VLOG(4)
          << "No file found for obs_history_proto_store. Proceeding "
             "with empty AggregatedObservationHistory. File will be created on "
             "first snapshot of the AggregatedObservationHistory.";
      break;
    }
    default: {
      LOG(ERROR) << "Read to obs_history_proto_store failed with status code: "
                 << restore_history_status.error_code() << "\nError message: "
                 << restore_history_status.error_message()
                 << "\nError details: "
                 << restore_history_status.error_details()
                 << "\nProceeding with empty AggregatedObservationHistory.";
      obs_history_ = AggregatedObservationHistory();
    }
  }
}

// TODO(pesk): Have the config parser verify that each locally
// aggregated report has at least one window size and that all window
// sizes are <= |kMaxAllowedAggregationWindowSize|. Additionally, have
// this method filter out any window sizes larger than
// |kMaxAllowedAggregationWindowSize|.
Status EventAggregator::UpdateAggregationConfigs(
    const ProjectContext& project_context) {
  std::string key;
  ReportAggregationKey key_data;
  key_data.set_customer_id(project_context.project().customer_id());
  key_data.set_project_id(project_context.project().project_id());
  for (const auto& metric : project_context.metric_definitions()->metric()) {
    switch (metric.metric_type()) {
      case MetricDefinition::EVENT_OCCURRED: {
        key_data.set_metric_id(metric.id());
        for (const auto& report : metric.reports()) {
          switch (report.report_type()) {
            case ReportDefinition::UNIQUE_N_DAY_ACTIVES: {
              key_data.set_report_id(report.id());
              if (!SerializeToBase64(key_data, &key)) {
                return kInvalidArguments;
              }
              // TODO(pesk): update the EventAggregator's view of a
              // Metric or ReportDefinition when appropriate.
              if (local_aggregate_store_.aggregates().count(key) == 0) {
                AggregationConfig aggregation_config;
                *aggregation_config.mutable_project() =
                    project_context.project();
                *aggregation_config.mutable_metric() =
                    *project_context.GetMetric(metric.id());
                *aggregation_config.mutable_report() = report;
                ReportAggregates report_aggregates;
                *report_aggregates.mutable_aggregation_config() =
                    aggregation_config;
                (*local_aggregate_store_.mutable_aggregates())[key] =
                    report_aggregates;
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
  ReportAggregationKey key_data;
  key_data.set_customer_id(event_record->metric->customer_id());
  key_data.set_project_id(event_record->metric->project_id());
  key_data.set_metric_id(event_record->metric->id());
  key_data.set_report_id(report_id);
  std::string key;
  if (!SerializeToBase64(key_data, &key)) {
    return kInvalidArguments;
  }

  auto aggregates = local_aggregate_store_.mutable_aggregates()->find(key);
  if (aggregates == local_aggregate_store_.mutable_aggregates()->end()) {
    LOG(ERROR) << "The Local Aggregate Store received an unexpected key.";
    return kInvalidArguments;
  }
  (*(*aggregates->second.mutable_by_event_code())
        [event_record->event->occurrence_event().event_code()]
            .mutable_by_day_index())[event_record->event->day_index()]
      .mutable_activity_daily_aggregate()
      ->set_activity_indicator(true);
  return kOK;
}

Status EventAggregator::BackUpLocalAggregateStore() {
  auto status = local_aggregate_proto_store_->Write(local_aggregate_store_);
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
    LOG(ERROR) << "Failed to back up the AggregatedObservationHistory. "
                  "::cobalt::util::Status error code: "
               << status.error_code()
               << "\nError message: " << status.error_message()
               << "\nError details: " << status.error_details();
    return kOther;
  }
  return kOK;
}

Status EventAggregator::GenerateObservations(uint32_t final_day_index) {
  for (auto pair : local_aggregate_store_.aggregates()) {
    const auto& config = pair.second.aggregation_config();

    const auto& metric = config.metric();
    auto metric_ref = MetricRef(&config.project(), &metric);

    const auto& report = config.report();
    auto max_window_size = 0u;
    for (uint32_t window_size : report.window_size()) {
      if (window_size > kMaxAllowedAggregationWindowSize) {
        LOG(WARNING) << "Window size exceeding "
                        "kMaxAllowedAggregationWindowSize will be "
                        "ignored by GenerateObservations";
      } else if (window_size > max_window_size) {
        max_window_size = window_size;
      }
    }
    if (max_window_size == 0) {
      LOG(ERROR) << "Each locally aggregated report must specify a positive "
                    "window size.";
      return kInvalidConfig;
    }
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
          }
          default:
            continue;
        }
      }
      default:
        continue;
    }
  }
  return kOK;
}

Status EventAggregator::GarbageCollect(uint32_t day_index) {
  for (auto pair : local_aggregate_store_.aggregates()) {
    // Determine the largest window size in the report associated to
    // this key-value pair.
    uint32_t max_window_size = 1;
    for (uint32_t window_size :
         pair.second.aggregation_config().report().window_size()) {
      if (window_size > max_window_size &&
          window_size <= kMaxAllowedAggregationWindowSize) {
        max_window_size = window_size;
      }
    }
    if (max_window_size == 0) {
      LOG(ERROR) << "Each locally aggregated report must specify a positive "
                    "window size.";
      return kInvalidConfig;
    }
    if (day_index < backfill_days_ + max_window_size) {
      LOG(ERROR) << "day_index must be >= backfill_days_ + max_window_size.";
      return kInvalidArguments;
    }
    // For each event code, iterate over the sub-map of local aggregates
    // keyed by day index. Keep buckets with day indices greater than
    // |day_index| - |backfill_days_| - |max_window_size|, and remove
    // all buckets with smaller day indices.
    for (auto event_code_aggregates : pair.second.by_event_code()) {
      for (auto day_aggregates : event_code_aggregates.second.by_day_index()) {
        if (day_aggregates.first <=
            day_index - backfill_days_ - max_window_size) {
          local_aggregate_store_.mutable_aggregates()
              ->at(pair.first)
              .mutable_by_event_code()
              ->at(event_code_aggregates.first)
              .mutable_by_day_index()
              ->erase(day_aggregates.first);
        }
      }
      // If the day index map under this event code is empty, remove the
      // event code from the event code map under this
      // ReportAggregationKey.
      if (local_aggregate_store_.aggregates()
              .at(pair.first)
              .by_event_code()
              .at(event_code_aggregates.first)
              .by_day_index()
              .empty()) {
        local_aggregate_store_.mutable_aggregates()
            ->at(pair.first)
            .mutable_by_event_code()
            ->erase(event_code_aggregates.first);
      }
    }
  }
  return kOK;
}

////////// GenerateUniqueActivesObservations and helper methods
/////////////////

// Given the set of daily aggregates for a fixed event code, and the
// size and end date of an aggregation window, returns the first day
// index within that window on which the event code occurred. Returns 0
// if the event code did not occur within the window.
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

uint32_t EventAggregator::LastGeneratedDayIndex(const std::string& report_key,
                                                uint32_t event_code,
                                                uint32_t window_size) const {
  auto report_history = obs_history_.by_report_key().find(report_key);
  if (report_history == obs_history_.by_report_key().end()) {
    return 0u;
  }
  auto event_code_history =
      report_history->second.by_event_code().find(event_code);
  if (event_code_history == report_history->second.by_event_code().end()) {
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
  for (uint32_t event_code = 0; event_code < num_event_codes; event_code++) {
    auto daily_aggregates = report_aggregates.by_event_code().find(event_code);
    // Have any events ever been logged for this report and event code?
    bool found_event_code =
        (daily_aggregates != report_aggregates.by_event_code().end());
    for (uint32_t window_size :
         report_aggregates.aggregation_config().report().window_size()) {
      // Skip any window size larger than
      // kMaxAllowedAggregationWindowSize.
      if (window_size > kMaxAllowedAggregationWindowSize) {
        LOG(WARNING) << "GenerateUniqueActivesObservations ignoring a window "
                        "size exceeding the maximum allowed value";
        continue;
      }
      // Find the earliest day index for which an Observation has not
      // yet been generated for this report, event code, and window
      // size. If that day index is later than |final_day_index|, no
      // Observation is generated on this invocation.
      auto last_gen =
          LastGeneratedDayIndex(report_key, event_code, window_size);
      auto first_day_index =
          std::max(last_gen + 1, uint32_t(final_day_index - backfill_days_));
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
          // forward in the window, update |active_day_index|, and
          // generate an Observation of activity or inactivity depending
          // on the result of the search.
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
                .mutable_by_event_code())[event_code]
              .mutable_by_window_size())[window_size] = obs_day_index;
      }
    }
  }
  return kOK;
}

}  // namespace logger
}  // namespace cobalt

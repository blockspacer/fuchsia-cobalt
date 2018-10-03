// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/event_aggregator.h"

#include <utility>

#include "algorithms/rappor/rappor_config_helper.h"
#include "logger/project_context.h"
#include "util/proto_util.h"

namespace cobalt {

using rappor::RapporConfigHelper;
using util::SerializeToBase64;

namespace logger {

EventAggregator::EventAggregator(const Encoder* encoder,
                                 const ObservationWriter* observation_writer,
                                 LocalAggregateStore* local_aggregate_store)
    : encoder_(encoder),
      observation_writer_(observation_writer),
      local_aggregate_store_(local_aggregate_store) {}

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
              // TODO(pesk): update the EventAggregator's view of a Metric
              // or ReportDefinition when appropriate.
              if (local_aggregate_store_->aggregates().count(key) == 0) {
                AggregationConfig aggregation_config;
                *aggregation_config.mutable_project() =
                    project_context.project();
                *aggregation_config.mutable_metric() =
                    *project_context.GetMetric(metric.id());
                *aggregation_config.mutable_report() = report;
                ReportAggregates report_aggregates;
                *report_aggregates.mutable_aggregation_config() =
                    aggregation_config;
                (*local_aggregate_store_->mutable_aggregates())[key] =
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
                                              EventRecord* event_record) const {
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
  auto aggregates = local_aggregate_store_->mutable_aggregates()->find(key);
  if (aggregates == local_aggregate_store_->mutable_aggregates()->end()) {
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

Status EventAggregator::GenerateObservations(uint32_t final_day_index) {
  for (auto pair : local_aggregate_store_->aggregates()) {
    const auto& config = pair.second.aggregation_config();
    switch (config.metric().metric_type()) {
      case MetricDefinition::EVENT_OCCURRED:
        switch (config.report().report_type()) {
          case ReportDefinition::UNIQUE_N_DAY_ACTIVES: {
            auto status =
                GenerateUniqueActivesObservations(pair.second, final_day_index);
            if (status != kOK) {
              return status;
            }
          }
          default:
            continue;
        }
      default:
        continue;
    }
  }
  return kOK;
}

Status EventAggregator::GarbageCollect(uint32_t day_index) {
  for (auto pair : local_aggregate_store_->aggregates()) {
    // Determine the largest window size in the report associated to this
    // key-value pair.
    uint32_t max_window_size = 1;
    for (uint32_t window_size :
         pair.second.aggregation_config().report().window_size()) {
      if (window_size > max_window_size) {
        max_window_size = window_size;
      }
    }
    // For each event code, iterate over the sub-map of local aggregates
    // keyed by day index. Keep buckets with day indices greater than
    // |day_index| - |max_window_size|, and remove all buckets with smaller day
    // indices.
    for (auto event_code_aggregates : pair.second.by_event_code()) {
      for (auto day_aggregates : event_code_aggregates.second.by_day_index()) {
        if (day_aggregates.first <= day_index - max_window_size) {
          local_aggregate_store_->mutable_aggregates()
              ->at(pair.first)
              .mutable_by_event_code()
              ->at(event_code_aggregates.first)
              .mutable_by_day_index()
              ->erase(day_aggregates.first);
        }
      }
      // If the day index map under this event code is empty, remove the event
      // code from the event code map under this ReportAggregationKey.
      if (local_aggregate_store_->aggregates()
              .at(pair.first)
              .by_event_code()
              .at(event_code_aggregates.first)
              .by_day_index()
              .empty()) {
        local_aggregate_store_->mutable_aggregates()
            ->at(pair.first)
            .mutable_by_event_code()
            ->erase(event_code_aggregates.first);
      }
    }
  }
  return kOK;
}

Status EventAggregator::GenerateUniqueActivesObservations(
    const ReportAggregates& report_aggregates, uint32_t final_day_index) const {
  const auto& config = report_aggregates.aggregation_config();
  auto metric_ref = MetricRef(&config.project(), &config.metric());
  auto num_event_codes =
      RapporConfigHelper::BasicRapporNumCategories(config.metric());

  Status status;
  for (uint32_t event_code = 0; event_code < num_event_codes; event_code++) {
    // If no occurrences have been logged for this event code, generate
    // a negative Observation for each window size specified in |report|.
    if (report_aggregates.by_event_code().count(event_code) == 0) {
      for (auto window_size : config.report().window_size()) {
        status = GenerateSingleUniqueActivesObservation(
            metric_ref, &config.report(), final_day_index, event_code, false,
            window_size);
        if (status != kOK) {
          return status;
        }
      }
    } else {
      // If an occurrence has been logged for this event code, then
      // for each window size, check whether a logged occurrence fell within
      // the window and generate a positive or null Observation
      // accordingly.
      const auto& daily_aggregates =
          report_aggregates.by_event_code().at(event_code).by_day_index();
      uint32_t max_window_size = 0;
      for (uint32_t window_size : config.report().window_size()) {
        if (window_size > max_window_size) {
          max_window_size = window_size;
        }
      }
      if (max_window_size <= 0) {
        LOG(ERROR) << "Maximum window size must be positive";
        return kInvalidConfig;
      }
      uint32_t days_since_activity = max_window_size;
      for (uint32_t day_index = final_day_index;
           day_index > final_day_index - max_window_size; day_index--) {
        if (daily_aggregates.count(day_index) > 0 &&
            daily_aggregates.at(day_index)
                    .activity_daily_aggregate()
                    .activity_indicator() == true) {
          days_since_activity = final_day_index - day_index;
          break;
        }
      }
      // If the most recent activity falls within a given window, generate an
      // observation of activity. Otherwise, generate an observation of
      // inactivity.
      for (uint32_t window_size : config.report().window_size()) {
        if (window_size > days_since_activity) {
          status = GenerateSingleUniqueActivesObservation(
              metric_ref, &config.report(), final_day_index, event_code,
              /* was_active */ true, window_size);
        } else {
          status = GenerateSingleUniqueActivesObservation(
              metric_ref, &config.report(), final_day_index, event_code,
              /* was_active */ false, window_size);
        }
      }
      if (status != kOK) {
        return status;
      }
    }
  }
  return kOK;
}

Status EventAggregator::GenerateSingleUniqueActivesObservation(
    const MetricRef metric_ref, const ReportDefinition* report,
    uint32_t final_day_index, uint32_t event_code, bool was_active,
    uint32_t window_size) const {
  auto encoder_result = encoder_->EncodeUniqueActivesObservation(
      metric_ref, report, final_day_index, event_code, was_active, window_size);
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

}  // namespace logger
}  // namespace cobalt

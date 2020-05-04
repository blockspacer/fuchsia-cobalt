// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/local_aggregation_1.1/observation_generator.h"

#include <thread>

#include "src/lib/util/clock.h"
#include "src/lib/util/datetime_util.h"
#include "src/local_aggregation_1.1/aggregation_procedure.h"
#include "src/local_aggregation_1.1/local_aggregate_storage.h"
#include "src/logger/observation_writer.h"

namespace cobalt::local_aggregation {

const std::chrono::seconds kDefaultGenerateObsInterval = std::chrono::hours(1);

ObservationGenerator::ObservationGenerator(
    LocalAggregateStorage* aggregate_storage, const logger::Encoder* encoder,
    const logger::ProjectContextFactory* global_project_context_factory,
    const logger::ObservationWriter* observation_writer)
    : aggregate_storage_(aggregate_storage),
      encoder_(encoder),
      global_project_context_factory_(global_project_context_factory),
      observation_writer_(observation_writer),
      steady_clock_(new util::SteadyClock()),
      generate_obs_interval_(kDefaultGenerateObsInterval) {}

void ObservationGenerator::Start(std::unique_ptr<util::SystemClockInterface> clock) {
  auto locked = protected_fields_.lock();
  locked->shut_down = false;
  worker_thread_ =
      std::thread([this, clock = std::move(clock)]() mutable { this->Run(std::move(clock)); });
}

void ObservationGenerator::ShutDown() {
  if (worker_thread_.joinable()) {
    {
      auto locked = protected_fields_.lock();
      locked->shut_down = true;
      locked->wakeup_notifier.notify_all();
    }
    worker_thread_.join();
  } else {
    protected_fields_.lock()->shut_down = true;
  }
}

void ObservationGenerator::Run(std::unique_ptr<util::SystemClockInterface> clock) {
  auto start_time = steady_clock_->now();
  // Schedule Observation generation to happen now.
  next_generate_obs_ = start_time;
  auto locked = protected_fields_.lock();
  while (true) {
    // Exit if a shutdown has been requested.
    if (locked->shut_down) {
      return;
    }

    // Sleep until the next scheduled observation generation or until notified of a shutdown.
    locked->wakeup_notifier.wait_until(locked, next_generate_obs_,
                                       [&locked]() { return locked->shut_down; });

    GenerateObservations(clock->now(), steady_clock_->now());
  }
}

logger::Status ObservationGenerator::GenerateObservations(
    std::chrono::system_clock::time_point system_time,
    std::chrono::steady_clock::time_point steady_time) {
  auto current_time_t = std::chrono::system_clock::to_time_t(system_time);
  AggregationProcedure::TimeInfo time_info;
  time_info.final_day_index_utc = util::TimeToDayIndex(current_time_t, MetricDefinition::UTC) - 1;
  time_info.final_day_index_local =
      util::TimeToDayIndex(current_time_t, MetricDefinition::LOCAL) - 1;
  time_info.final_hour_index_utc = util::TimeToHourIndex(current_time_t, MetricDefinition::UTC) - 1;
  time_info.final_hour_index_local =
      util::TimeToHourIndex(current_time_t, MetricDefinition::LOCAL) - 1;

  if (steady_time >= next_generate_obs_) {
    next_generate_obs_ += generate_obs_interval_;
    for (auto& project_id : global_project_context_factory_->ListProjects()) {
      auto project = global_project_context_factory_->NewProjectContext(std::get<0>(project_id),
                                                                        std::get<1>(project_id));

      for (auto& metric_id : project->ListMetrics()) {
        auto metric = project->GetMetric(metric_id);
        auto aggregate = aggregate_storage_->GetMetricAggregate(
            project->project().customer_id(), project->project().project_id(), metric->id());

        for (auto& report : metric->reports()) {
          auto procedure = AggregationProcedure::Get(*metric, report);

          if (procedure) {
            // Current time needs to be passed to MaybeGenerateObservation.
            auto encoded_observation = procedure->MaybeGenerateObservation(
                time_info, encoder_, &aggregate->mutable_by_report_id()->at(report.id()));

            if (encoded_observation) {
              auto status = observation_writer_->WriteObservation(
                  *encoded_observation->observation, std::move(encoded_observation->metadata));
              if (status != logger::kOK) {
                return status;
              }
            }
          }
        }

        aggregate_storage_->SaveMetricAggregate(project->project().customer_id(),
                                                project->project().project_id(), metric->id());
      }
    }
  }

  return logger::kOK;
}

}  // namespace cobalt::local_aggregation

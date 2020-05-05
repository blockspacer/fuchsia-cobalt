// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/local_aggregation_1.1/local_aggregation.h"

#include <memory>

#include "src/lib/util/clock.h"
#include "src/local_aggregation_1.1/aggregation_procedure.h"
#include "src/logger/project_context_factory.h"
#include "src/pb/observation_batch.pb.h"

namespace cobalt::local_aggregation {

LocalAggregation::LocalAggregation(
    const CobaltConfig &cfg, const logger::ProjectContextFactory *global_project_context_factory,
    util::FileSystem *fs, const logger::Encoder *encoder,
    const logger::ObservationWriter *observation_writer)
    : aggregate_storage_(cfg.local_aggregate_store_dir, fs, global_project_context_factory),
      observation_generator_(&aggregate_storage_, encoder, global_project_context_factory,
                             observation_writer) {}

util::Status LocalAggregation::AddEvent(const logger::EventRecord &event_record) {
  auto aggregate = aggregate_storage_.GetMetricAggregate(
      event_record.project_context()->project().customer_id(),
      event_record.project_context()->project().project_id(), event_record.metric()->id());

  if (!aggregate) {
    return util::Status(util::StatusCode::NOT_FOUND, "Unable to get MetricAggregate");
  }

  for (auto &report : event_record.metric()->reports()) {
    auto procedure = AggregationProcedure::Get(*event_record.metric(), report);
    if (procedure) {
      procedure->UpdateAggregate(event_record, &aggregate->mutable_by_report_id()->at(report.id()));
    }
  }

  return aggregate_storage_.SaveMetricAggregate(
      event_record.project_context()->project().customer_id(),
      event_record.project_context()->project().project_id(), event_record.metric()->id());
}

void LocalAggregation::Start(std::unique_ptr<util::SystemClockInterface> clock) {
  observation_generator_.Start(std::move(clock));
}

}  // namespace cobalt::local_aggregation

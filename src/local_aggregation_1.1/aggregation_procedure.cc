// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/local_aggregation_1.1/aggregation_procedure.h"

#include "src/logging.h"

namespace cobalt::local_aggregation {

std::unique_ptr<AggregationProcedure> AggregationProcedure::Get(const MetricDefinition &metric,
                                                                const ReportDefinition &report) {
  switch (report.report_type()) {
    case ReportDefinition::FLEETWIDE_OCCURRENCE_COUNTS:
      return std::make_unique<UnimplementedAggregationProcedure>("COUNT");
    case ReportDefinition::UNIQUE_DEVICE_COUNTS:
      switch (report.local_aggregation_procedure()) {
        case ReportDefinition::AT_LEAST_ONCE:
          return std::make_unique<UnimplementedAggregationProcedure>("AT_LEAST_ONCE");
        case ReportDefinition::SELECT_FIRST:
          return std::make_unique<UnimplementedAggregationProcedure>("SELECT_FIRST");
        case ReportDefinition::SELECT_MOST_COMMON:
          return std::make_unique<UnimplementedAggregationProcedure>("SELECT_MOST_COMMON");
        default:
          LOG(ERROR) << "Report of type UNIQUE_DEVICE_COUNTS does not support selected aggregation "
                        "procedure: "
                     << report.local_aggregation_procedure();
          return nullptr;
      }
      break;
    case ReportDefinition::UNIQUE_DEVICE_HISTOGRAMS:
    case ReportDefinition::HOURLY_VALUE_HISTOGRAMS:
    case ReportDefinition::UNIQUE_DEVICE_NUMERIC_STATS:
    case ReportDefinition::HOURLY_VALUE_NUMERIC_STATS:
      switch (metric.metric_type()) {
        case MetricDefinition::OCCURRENCE:
          return std::make_unique<UnimplementedAggregationProcedure>("COUNT_AS_INTEGER");
        case MetricDefinition::INTEGER:
          return std::make_unique<UnimplementedAggregationProcedure>("NUMERIC_STAT");
        default:
          LOG(ERROR) << "Report of type UNIQUE_DEVICE_HISTOGRAMS is not valid for metric of type: "
                     << metric.metric_type();
          return nullptr;
      }
      break;
    case ReportDefinition::FLEETWIDE_HISTOGRAMS:
      return std::make_unique<UnimplementedAggregationProcedure>("INTEGER_HISTOGRAM");
    case ReportDefinition::FLEETWIDE_MEANS:
      return std::make_unique<UnimplementedAggregationProcedure>("SUM_AND_COUNT");
    case ReportDefinition::STRING_HISTOGRAMS:
      return std::make_unique<UnimplementedAggregationProcedure>("STRING_HISTOGRAM");
    default:
      VLOG(10) << "Report doesn't seem to be a cobalt 1.1 report";
      return nullptr;
  }
}

}  // namespace cobalt::local_aggregation

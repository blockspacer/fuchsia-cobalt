// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOCAL_AGGREGATION_1_1_AGGREGATION_PROCEDURE_H_
#define COBALT_SRC_LOCAL_AGGREGATION_1_1_AGGREGATION_PROCEDURE_H_

#include <cstdint>
#include <string>

#include "src/local_aggregation_1.1/local_aggregation.pb.h"
#include "src/logger/encoder.h"
#include "src/logger/event_record.h"
#include "src/logging.h"
#include "src/pb/event.pb.h"
#include "src/pb/observation2.pb.h"
#include "src/registry/metric_definition.pb.h"
#include "src/registry/report_definition.pb.h"

namespace cobalt::local_aggregation {

// AggregationProcedure is the abstract interface for aggregation logic.
//
// For each local aggregation procedure, there should be an implementation of this class.
//
// An AggregationProcedure should not have any of its own state. Its role is to update the state
// within the instance of ReportAggregate it is passed in UpdateAggregate() and
// MaybeGenerateObservation().
//
// See Also:
// * LocalAggregation calls UpdateAggregate() whenever AddEvent() is called.
// * ObservationGenerator calls MaybeGenerateObservation() on a regular schedule (~once per hour).
class AggregationProcedure {
 public:
  // A struct for passing time information to MaybeGenerateObservation()
  struct TimeInfo {
    // These are used for daily/weekly/monthly aggregates, this will be set to the day index of
    // yesterday in UTC and system local timezone.
    uint32_t final_day_index_utc;
    uint32_t final_day_index_local;

    // These are used for hourly aggregates, this will be set to the hour index of the hour that
    // just passed in UTC and system local timezone.
    uint32_t final_hour_index_utc;
    uint32_t final_hour_index_local;
  };

  AggregationProcedure() = default;
  virtual ~AggregationProcedure() = default;

  // AggregationProcedure::Get returns the appropriate AggregationProcedure for the given
  // metric/report.
  //
  // A return value of nullptr should be interpreted as a report that is not supported by cobalt 1.1
  // (perhaps a cobalt 1.0 metric/report)
  static std::unique_ptr<AggregationProcedure> Get(const MetricDefinition &metric,
                                                   const ReportDefinition &report);

  // UpdateAggregate takes an |event_record| and adds it to the given |aggregate| object according
  // to the aggregation procedure implemented by this instance of AggregationProcedure.
  //
  // |event_record|: The event that needs to be added to the aggregate.
  // |aggregate|: A mutable ReportAggregate object.
  virtual void UpdateAggregate(const logger::EventRecord &event_record,
                               ReportAggregate *aggregate) = 0;

  // MaybeGenerateObservation attempts to generate an observation for the given report. If the
  // report should not generate an observation at this time (for example a daily metric that has
  // generated an observation within the last day), nullptr should be returned.
  //
  // |time_info|: Time period for which this observation should be generated.
  // |encoder|: An instance of logger::Encoder that should be used to encode the observations.
  // |aggregate|: A mutable ReportAggregate object.
  //
  // Note: This function should maintain only the minimum amount of data required to generate future
  // observations. If the ReportAggregate contains data points that cannot be useful for any future
  // observations, they should be deleted.
  virtual std::unique_ptr<logger::Encoder::Result> MaybeGenerateObservation(
      const TimeInfo &time_info, const logger::Encoder *encoder, ReportAggregate *aggregate) = 0;
};

class UnimplementedAggregationProcedure : public AggregationProcedure {
 public:
  explicit UnimplementedAggregationProcedure(std::string name) : name_(std::move(name)) {}
  ~UnimplementedAggregationProcedure() override = default;

  void UpdateAggregate(const logger::EventRecord & /* event_record */,
                       ReportAggregate * /* aggregate */) override {
    LOG(ERROR) << "UpdateAggregate is UNIMPLEMENTED for " << name_;
  }

  std::unique_ptr<logger::Encoder::Result> MaybeGenerateObservation(
      const TimeInfo & /* time_info */, const logger::Encoder * /* encoder */,
      ReportAggregate * /* aggregate */) override {
    LOG(ERROR) << "MaybeGenerateObservation is UNIMPLEMENTED for " << name_;

    return nullptr;
  }

 private:
  std::string name_;
};

}  // namespace cobalt::local_aggregation

#endif  // COBALT_SRC_LOCAL_AGGREGATION_1_1_AGGREGATION_PROCEDURE_H_

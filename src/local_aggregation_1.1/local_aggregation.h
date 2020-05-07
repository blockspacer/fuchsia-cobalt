// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOCAL_AGGREGATION_1_1_LOCAL_AGGREGATION_H_
#define COBALT_SRC_LOCAL_AGGREGATION_1_1_LOCAL_AGGREGATION_H_

#include "src/lib/util/file_system.h"
#include "src/lib/util/status.h"
#include "src/local_aggregation_1.1/local_aggregate_storage.h"
#include "src/local_aggregation_1.1/observation_generator.h"
#include "src/logger/encoder.h"
#include "src/logger/event_record.h"
#include "src/logger/observation_writer.h"
#include "src/logger/project_context_factory.h"
#include "src/public/cobalt_config.h"

namespace cobalt::local_aggregation {

// LocalAggregation is the top-level object responsible for scheduling and managing the current
// aggregated observations.
class LocalAggregation {
 public:
  // Consntructor for LocalAggregation
  //
  // |cfg|: A reference to the CobaltConfig used to construct the CobaltService.
  // |global_project_context_factory|: The current global registry.
  // |fs|: An instance of util::FileSystem
  // |encoder|: An instance of Encoder, used to create Observation2s.
  // |observation_writer|: An implementation of the ObservationWriter interface.
  LocalAggregation(const CobaltConfig &cfg,
                   const logger::ProjectContextFactory *global_project_context_factory,
                   util::FileSystem *fs, const logger::Encoder *encoder,
                   const logger::ObservationWriter *observation_writer);

  // Start should be called when the system is ready to start the background process for generating
  // observations based on the aggregated events.
  //
  // |clock|: An implementation of the SystemClockInterface. The caller should make sure that this
  // clock is an accurate representation of the current system time before calling Start.
  void Start(std::unique_ptr<util::SystemClockInterface> clock);

  // AddEvent adds an EventRecord to local aggregation.
  util::Status AddEvent(const logger::EventRecord &event_record);

 private:
  LocalAggregateStorage aggregate_storage_;
  ObservationGenerator observation_generator_;
};

}  // namespace cobalt::local_aggregation

#endif  // COBALT_SRC_LOCAL_AGGREGATION_1_1_LOCAL_AGGREGATION_H_

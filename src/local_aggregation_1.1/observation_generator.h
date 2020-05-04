// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOCAL_AGGREGATION_1_1_OBSERVATION_GENERATOR_H_
#define COBALT_SRC_LOCAL_AGGREGATION_1_1_OBSERVATION_GENERATOR_H_

#include <condition_variable>
#include <thread>

#include "src/lib/util/clock.h"
#include "src/local_aggregation_1.1/local_aggregate_storage.h"
#include "src/logger/encoder.h"
#include "src/logger/observation_writer.h"
#include "src/logger/project_context_factory.h"
#include "src/logger/status.h"

namespace cobalt::local_aggregation {

// ObservationGenerator manages the background task that is responsible for generating observations
// and writing them to |observation_writer| on a regular schedule (~once per hour).
//
// There should be only one ObservationGenerator per system.
//
// Once the system clock has become accurate, the Start() method should be called with the
// SystemClockInterface exactly once. After that point, the ObservationGenerator will generate
// observations on a regular schedule until ShutDown() is called.
class ObservationGenerator {
 public:
  // Constructor for ObservationGenerator
  //
  // |aggregate_storage|: The file-backed storage for MetricAggregate objects.
  // |encoder|: Used to generate Observation2s.
  // |global_project_context_factory|: The current global registry.
  // |observation_writer|: Used for writing generated observations to observation storage.
  ObservationGenerator(LocalAggregateStorage* aggregate_storage, const logger::Encoder* encoder,
                       const logger::ProjectContextFactory* global_project_context_factory,
                       const logger::ObservationWriter* observation_writer);

  // Start begins the background thread to generate observations on a fixed schedule. This method
  // should only be called once when the system clock has become accurate.
  void Start(std::unique_ptr<util::SystemClockInterface> clock);

  // ShutDown halts the background thread, allowing the ObservationGenerator to be destroyed.
  void ShutDown();

 private:
  void Run(std::unique_ptr<util::SystemClockInterface> clock);

  // GenerateObservations iterates through all of the metrics/reports in the global registry and
  // attempts to generate observations for them. Any generated observations will be written to the
  // |observation_writer_|.
  logger::Status GenerateObservations(std::chrono::system_clock::time_point system_time,
                                      std::chrono::steady_clock::time_point steady_time);

  LocalAggregateStorage* aggregate_storage_;
  const logger::Encoder* encoder_;
  const logger::ProjectContextFactory* global_project_context_factory_;
  const logger::ObservationWriter* observation_writer_;
  std::unique_ptr<util::SteadyClockInterface> steady_clock_;
  std::chrono::steady_clock::time_point next_generate_obs_;
  std::chrono::seconds generate_obs_interval_;

  std::thread worker_thread_;
  struct ProtectedFields {
    bool shut_down;

    std::condition_variable_any wakeup_notifier;
  };
  util::ProtectedFields<ProtectedFields> protected_fields_;
};

}  // namespace cobalt::local_aggregation

#endif  // COBALT_SRC_LOCAL_AGGREGATION_1_1_OBSERVATION_GENERATOR_H_

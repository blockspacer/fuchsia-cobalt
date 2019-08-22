// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOGGER_LOGGER_H_
#define COBALT_SRC_LOGGER_LOGGER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/lib/util/clock.h"
#include "src/logger/encoder.h"
#include "src/logger/event_aggregator.h"
#include "src/logger/internal_metrics.h"
#include "src/logger/logger_interface.h"
#include "src/logger/observation_writer.h"
#include "src/logger/project_context.h"
#include "src/logger/status.h"

namespace cobalt {

namespace internal {

class RealLoggerFactory;

}  // namespace internal

namespace logger {

// Concrete implementation of LoggerInterface.
//
// After constructing a Logger use the Log*() methods to Log Events to Cobalt.
//
// There should be an instance of Logger for each client-side Project.
// On a Fuchsia system instances of Logger are created by the Cobalt FIDL
// service for each FIDL connection from a client project.
class Logger : public LoggerInterface {
 public:
  // Constructor
  //
  // |project_context| The ProjectContext of the client-side project for which
  // the Logger will log events. Must not be null.
  //
  // |encoder| The system's singleton instance of Encoder. This must remain
  // valid as long as the Logger is being used. The Logger uses this to
  // encode immediate Observations.
  //
  // |event_aggregator| The system's singleton instance of EventAggregator.
  // This must remain valid as long as the Logger is being used. The Logger
  // uses this to aggregate values derived from Events and to produce locally
  // aggregated Observations.
  //
  // |observation_writer| An instance of ObservationWriter, used by the Logger
  // to write immediate Observations to an ObservationStore. Must remain valid
  // as long as the Logger is in use.
  //
  // |system_data| A pointer to a SystemDataInterface.
  //
  // |internal_logger| An instance of LoggerInterface, used internally by the
  // Logger to send metrics about Cobalt to Cobalt. If nullptr, no such
  // internal logging will be performed by this Logger.
  Logger(std::unique_ptr<ProjectContext> project_context, const Encoder* encoder,
         EventAggregator* event_aggregator, ObservationWriter* observation_writer,
         encoder::SystemDataInterface* system_data, LoggerInterface* internal_logger = nullptr);

  // DEPRECATED Constructor
  Logger(std::unique_ptr<ProjectContext> project_context, const Encoder* encoder,
         EventAggregator* event_aggregator, ObservationWriter* observation_writer,
         LoggerInterface* internal_logger = nullptr);

  ~Logger() override = default;

  Status LogEvent(uint32_t metric_id, uint32_t event_code) override;

  // In order to import the LogEventCount method that doesn't take a vector of
  // event_codes, we need to manually import it, since c++ won't do it
  // automatically.
  using LoggerInterface::LogEventCount;
  Status LogEventCount(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                       const std::string& component, int64_t period_duration_micros,
                       uint32_t count) override;

  // In order to import the LogElapsedTime method that doesn't take a vector of
  // event_codes, we need to manually import it, since c++ won't do it
  // automatically.
  using LoggerInterface::LogElapsedTime;
  Status LogElapsedTime(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                        const std::string& component, int64_t elapsed_micros) override;

  // In order to import the LogFrameRate method that doesn't take a vector of
  // event_codes, we need to manually import it, since c++ won't do it
  // automatically.
  using LoggerInterface::LogFrameRate;
  Status LogFrameRate(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                      const std::string& component, float fps) override;

  // In order to import the LogMemoryUsage method that doesn't take a vector of
  // event_codes, we need to manually import it, since c++ won't do it
  // automatically.
  using LoggerInterface::LogMemoryUsage;
  Status LogMemoryUsage(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                        const std::string& component, int64_t bytes) override;

  // In order to import the LogIntHistogram method that doesn't take a vector of
  // event_codes, we need to manually import it, since c++ won't do it
  // automatically.
  using LoggerInterface::LogIntHistogram;
  Status LogIntHistogram(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                         const std::string& component, HistogramPtr histogram) override;

  Status LogString(uint32_t metric_id, const std::string& str) override;

  Status LogCustomEvent(uint32_t metric_id, EventValuesPtr event_values) override;

  InternalMetrics* internal_metrics() { return internal_metrics_.get(); }
  const ProjectContext* project_context() { return project_context_.get(); }

  // Pauses Cobalt's internal metrics collection.
  void PauseInternalLogging();

  // Resumes Cobalt's internal metrics collection.
  void ResumeInternalLogging();

 private:
  friend class LoggerTest;
  friend class cobalt::internal::RealLoggerFactory;

  void SetClock(util::SystemClockInterface* clock) { clock_.reset(clock); }

  const std::unique_ptr<ProjectContext> project_context_;

  const Encoder* encoder_;
  EventAggregator* event_aggregator_;
  const ObservationWriter* observation_writer_;
  const encoder::SystemDataInterface* system_data_;
  std::unique_ptr<util::SystemClockInterface> clock_;

  std::unique_ptr<InternalMetrics> internal_metrics_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_SRC_LOGGER_LOGGER_H_

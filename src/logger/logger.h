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
#include "src/local_aggregation/event_aggregator.h"
#include "src/logger/encoder.h"
#include "src/logger/internal_metrics.h"
#include "src/logger/internal_metrics_config.cb.h"
#include "src/logger/logger_interface.h"
#include "src/logger/observation_writer.h"
#include "src/logger/project_context.h"
#include "src/logger/status.h"
#include "src/logger/undated_event_manager.h"

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
  // Constructor for a Logger when the system clock is, and will remain, accurate.
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
         local_aggregation::EventAggregator* event_aggregator,
         ObservationWriter* observation_writer, system_data::SystemDataInterface* system_data,
         LoggerInterface* internal_logger = nullptr);

  // Constructor for a Logger when the clock is not yet accurate.
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
  // |validated_clock| An instance of a clock that refuses to provide the time if a quality
  // condition is not satisfied.
  //
  // |undated_event_manager| An instance of UndatedEventManager to use to save events until the
  // clock becomes accurate.
  //
  // |internal_logger| An instance of LoggerInterface, used internally by the
  // Logger to send metrics about Cobalt to Cobalt. If nullptr, no such
  // internal logging will be performed by this Logger.
  Logger(std::unique_ptr<ProjectContext> project_context, const Encoder* encoder,
         local_aggregation::EventAggregator* event_aggregator,
         ObservationWriter* observation_writer, system_data::SystemDataInterface* system_data,
         util::ValidatedClockInterface* validated_clock,
         std::weak_ptr<UndatedEventManager> undated_event_manager,
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

  Status LogCustomEvent(uint32_t metric_id, EventValuesPtr event_values) override;

  // LoggerCalled (cobalt_internal::metrics::logger_calls_made) and
  // (cobalt_internal::metrics::per_project_logger_calls_made) are logged for
  // every call to Logger along with which method was called and the project
  // that called it.
  void RecordLoggerCall(PerProjectLoggerCallsMadeMetricDimensionLoggerMethod method) {
    internal_metrics_->LoggerCalled(method, project_context_->project());
  }

  // Pauses Cobalt's internal metrics collection.
  void PauseInternalLogging();

  // Resumes Cobalt's internal metrics collection.
  void ResumeInternalLogging();

 private:
  friend class LoggerTest;
  friend class cobalt::internal::RealLoggerFactory;

  // Constructs an appropriate event logger and sends it an |event_record| to log.
  Status Log(uint32_t metric_id, MetricDefinition::MetricType metric_type,
             std::unique_ptr<EventRecord> event_record);

  // ProjectContext is shared as it is used in all created EventRecords, which can outlive the
  // Logger class.
  const std::shared_ptr<const ProjectContext> project_context_;

  const Encoder* encoder_;
  local_aggregation::EventAggregator* event_aggregator_;
  const ObservationWriter* observation_writer_;
  const system_data::SystemDataInterface* system_data_;
  util::ValidatedClockInterface* validated_clock_;
  std::unique_ptr<util::ValidatedClockInterface> local_validated_clock_;
  std::weak_ptr<UndatedEventManager> undated_event_manager_;

  std::unique_ptr<InternalMetrics> internal_metrics_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_SRC_LOGGER_LOGGER_H_

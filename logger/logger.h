// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_LOGGER_H_
#define COBALT_LOGGER_LOGGER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./observation2.pb.h"
#include "logger/encoder.h"
#include "logger/event_aggregator.h"
#include "logger/internal_metrics.h"
#include "logger/logger_interface.h"
#include "logger/observation_writer.h"
#include "logger/project_context.h"
#include "logger/status.h"
#include "util/clock.h"

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
  // the Logger will log events.
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
  // |internal_logger| An instance of LoggerInterface, used internally by the
  // Logger to send metrics about Cobalt to Cobalt. If nullptr, no such
  // internal logging will be performed by this Logger.
  Logger(std::unique_ptr<ProjectContext> project_context,
         const Encoder* encoder, EventAggregator* event_aggregator,
         ObservationWriter* observation_writer,
         LoggerInterface* internal_logger = nullptr);

  // Constructor
  //
  // Deprecated. Prefer the constructor that gives ownership of the
  // ProjectContext. TODO(rudominer) Delete this constructor when all uses of it
  // have been removed.
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
  // |project_context| The ProjectContext of the client-side project for which
  // the Logger will log events.
  //
  // |internal_logger| An instance of LoggerInterface, used internally by the
  // Logger to send metrics about Cobalt to Cobalt. If nullptr, no such
  // internal logging will be performed by this Logger.
  Logger(const Encoder* encoder, EventAggregator* event_aggregator,
         ObservationWriter* observation_writer,
         const ProjectContext* project_context,
         LoggerInterface* internal_logger = nullptr);

  // Deprecated constructor
  //
  // This constructor does not take an EventAggregator. Loggers created with
  // this constructor do not log Events for locally aggregated reports. This
  // constructor is currently used by the Cobalt test app in Garnet and will be
  // removed once those tests are updated.
  Logger(const Encoder* encoder, ObservationWriter* observation_writer,
         const ProjectContext* project_context,
         LoggerInterface* internal_logger = nullptr);

  virtual ~Logger() = default;

  Status LogEvent(uint32_t metric_id, uint32_t event_code) override;

  // In order to import the LogEventCount method that doesn't take a vector of
  // event_codes, we need to manually import it, since c++ won't do it
  // automatically.
  using LoggerInterface::LogEventCount;
  Status LogEventCount(uint32_t metric_id, std::vector<uint32_t> event_codes,
                       const std::string& component,
                       int64_t period_duration_micros, uint32_t count) override;

  // In order to import the LogElapsedTime method that doesn't take a vector of
  // event_codes, we need to manually import it, since c++ won't do it
  // automatically.
  using LoggerInterface::LogElapsedTime;
  Status LogElapsedTime(uint32_t metric_id, std::vector<uint32_t> event_codes,
                        const std::string& component,
                        int64_t elapsed_micros) override;

  // In order to import the LogFrameRate method that doesn't take a vector of
  // event_codes, we need to manually import it, since c++ won't do it
  // automatically.
  using LoggerInterface::LogFrameRate;
  Status LogFrameRate(uint32_t metric_id, std::vector<uint32_t> event_codes,
                      const std::string& component, float fps) override;

  // In order to import the LogMemoryUsage method that doesn't take a vector of
  // event_codes, we need to manually import it, since c++ won't do it
  // automatically.
  using LoggerInterface::LogMemoryUsage;
  Status LogMemoryUsage(uint32_t metric_id, std::vector<uint32_t> event_codes,
                        const std::string& component, int64_t bytes) override;

  // In order to import the LogIntHistogram method that doesn't take a vector of
  // event_codes, we need to manually import it, since c++ won't do it
  // automatically.
  using LoggerInterface::LogIntHistogram;
  Status LogIntHistogram(uint32_t metric_id, std::vector<uint32_t> event_codes,
                         const std::string& component,
                         HistogramPtr histogram) override;

  Status LogString(uint32_t metric_id, const std::string& str) override;

  Status LogCustomEvent(uint32_t metric_id,
                        EventValuesPtr event_values) override;

 private:
  friend class EventLogger;
  friend class LoggerTest;
  friend class cobalt::internal::RealLoggerFactory;

  // Constructor
  //
  // |maybe_null_project_context| The ProjectContext of the client-side project
  // for which the Logger will log events. Exactly one of
  // |maybe_null_project_context| and |project_context| must be non-null.
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
  // |project_context| The ProjectContext of the client-side project for which
  // the Logger will log events. Exactly one of |maybe_null_project_context| and
  // |project_context| must be non-null.
  //
  // |internal_logger| An instance of LoggerInterface, used internally by the
  // Logger to send metrics about Cobalt to Cobalt. If nullptr, no such
  // internal logging will be performed by this Logger.
  Logger(std::unique_ptr<ProjectContext> maybe_null_project_context,
         const Encoder* encoder, EventAggregator* event_aggregator,
         ObservationWriter* observation_writer,
         const ProjectContext* project_context,
         LoggerInterface* internal_logger = nullptr);

  void SetClock(util::ClockInterface* clock) { clock_.reset(clock); }

  // TODO(rudominer) We are transitioning to having a Logger own its
  // ProjectContext. In the interim we maintain these two variables.
  // After the transition is complete we will delete |project_context_|
  // and rename |maybe_null_project_context_| to |project_context_|.
  // As long as both variables exist, only access |project_context_|,
  // do not access |maybe_null_project_context_|. If
  // |maybe_null_project_context_| is not null then project_context_ points
  // to the same thing.
  const ProjectContext* project_context_;
  const std::unique_ptr<ProjectContext> maybe_null_project_context_;

  const Encoder* encoder_;
  EventAggregator* event_aggregator_;
  const ObservationWriter* observation_writer_;
  std::unique_ptr<util::ClockInterface> clock_;

  std::unique_ptr<InternalMetrics> internal_metrics_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_LOGGER_H_

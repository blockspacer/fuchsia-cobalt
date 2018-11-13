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
#include "logger/internal_metrics.h"
#include "logger/logger_interface.h"
#include "logger/observation_writer.h"
#include "logger/project_context.h"
#include "logger/status.h"
#include "util/clock.h"

namespace cobalt {
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
  // |encoder| The system's singleton instance of Encoder. This must remain
  // valid as long as the Logger is being used. The Logger uses this to
  // encode immediate Observations.
  //
  // |observation_writer| An instance of ObservationWriter, used by the Logger
  // to write immediate Observations to an ObservationStore. Must remain valid
  // as long as the Logger is in use.
  //
  // |project| The ProjectContext of the client-side project for which the
  // Logger will log events.
  //
  // |internal_logger| An instance of LoggerInterface, used internally by the
  // Logger to send metrics about Cobalt to Cobalt. If nullptr, no such internal
  // logging will be performed by this Logger.
  Logger(const Encoder* encoder, ObservationWriter* observation_writer,
         const ProjectContext* project,
         LoggerInterface* internal_logger = nullptr);

  virtual ~Logger() = default;

  Status LogEvent(uint32_t metric_id, uint32_t event_code) override;

  Status LogEventCount(uint32_t metric_id, uint32_t event_code,
                       const std::string& component,
                       int64_t period_duration_micros, uint32_t count) override;

  Status LogElapsedTime(uint32_t metric_id, uint32_t event_code,
                        const std::string& component,
                        int64_t elapsed_micros) override;

  Status LogFrameRate(uint32_t metric_id, uint32_t event_code,
                      const std::string& component, float fps) override;

  Status LogMemoryUsage(uint32_t metric_id, uint32_t event_code,
                        const std::string& component, int64_t bytes) override;

  Status LogIntHistogram(uint32_t metric_id, uint32_t event_code,
                         const std::string& component,
                         HistogramPtr histogram) override;

  Status LogString(uint32_t metric_id, const std::string& str) override;

  Status LogCustomEvent(uint32_t metric_id,
                        EventValuesPtr event_values) override;

 private:
  friend class EventLogger;

  void SetClock(std::unique_ptr<util::ClockInterface> clock) {
    clock_ = std::move(clock);
  }

  const Encoder* encoder_;
  const ObservationWriter* observation_writer_;
  const ProjectContext* project_context_;
  std::unique_ptr<util::ClockInterface> clock_;

  std::unique_ptr<InternalMetrics> internal_metrics_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_LOGGER_H_

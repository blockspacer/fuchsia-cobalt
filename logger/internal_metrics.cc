// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/internal_metrics.h"

#include <memory>
#include <string>
#include <utility>

#include "./logging.h"

namespace cobalt {
namespace logger {

InternalMetricsImpl::InternalMetricsImpl(LoggerInterface* logger)
    : paused_(false), logger_(logger) {
  CHECK(logger_);
}

void InternalMetricsImpl::LoggerCalled(
    LoggerCallsMadeMetricDimensionLoggerMethod method, const Project& project) {
  if (paused_) {
    return;
  }

  auto status = logger_->LogEvent(kLoggerCallsMadeMetricId,
                                  static_cast<uint32_t>(method));

  if (status != kOK) {
    VLOG(1) << "InternalMetricsImpl::LoggerCalled: LogEvent() returned status="
            << status;
  }

  std::ostringstream component;
  component << project.customer_name() << '/' << project.project_name();
  status = logger_->LogEventCount(kPerProjectLoggerCallsMadeMetricId,
                                  static_cast<uint32_t>(method),
                                  component.str(), 0, 1);

  if (status != kOK) {
    VLOG(1)
        << "InternalMetricsImpl::LoggerCalled: LogEventCount() returned status="
        << status;
  }
}

}  // namespace logger
}  // namespace cobalt

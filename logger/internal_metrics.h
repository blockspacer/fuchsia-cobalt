// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_INTERNAL_METRICS_H_
#define COBALT_LOGGER_INTERNAL_METRICS_H_

#include <memory>
#include <string>

#include "logger/internal_metrics_config.cb.h"
#include "logger/logger_interface.h"

namespace cobalt {
namespace logger {

// InternalMetrics defines the methods used for collecting cobalt-internal
// metrics.
class InternalMetrics {
 public:
  // LoggerCalled (cobalt_internal::metrics::logger_calls_made) is logged for
  // every call to Logger along with which method was called.
  virtual void LoggerCalled(
      LoggerCallsMadeMetricDimensionLoggerMethod method) = 0;

  // After PauseLogging is called, all calls to log internal metrics will be
  // ignored.
  virtual void PauseLogging() = 0;

  // Once ResumeLogging is called, calls to log internal metrics are will no
  // longer be ignored.
  virtual void ResumeLogging() = 0;

  virtual ~InternalMetrics() {}
};

// NoOpInternalMetrics is to be used when the LoggerInterface* provided to the
// Logger constructor is nullptr. It stubs out all of the calls in the
// InternalMetrics interface, allowing code to safely make these calls even if
// no LoggerInterface* was provided.
class NoOpInternalMetrics : public InternalMetrics {
  void LoggerCalled(
      LoggerCallsMadeMetricDimensionLoggerMethod method) override {}

  void PauseLogging() override {}
  void ResumeLogging() override {}

  ~NoOpInternalMetrics() override {}
};

// InternalMetricsImpl is the actual implementation of InternalMetrics. It is a
// wrapper around the (non nullptr) LoggerInterface* that was provided to the
// Logger constructor.
class InternalMetricsImpl : public InternalMetrics {
 public:
  explicit InternalMetricsImpl(LoggerInterface* logger);

  void LoggerCalled(LoggerCallsMadeMetricDimensionLoggerMethod method) override;

  void PauseLogging() override { paused_ = true; }
  void ResumeLogging() override { paused_ = false; }

  ~InternalMetricsImpl() override {}

 private:
  bool paused_;
  LoggerInterface* logger_;  // not owned
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_INTERNAL_METRICS_H_

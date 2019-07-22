// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/internal_metrics.h"

#include <memory>
#include <string>
#include <utility>

#include "./logging.h"

namespace cobalt::logger {

std::unique_ptr<InternalMetrics> InternalMetrics::NewWithLogger(
    LoggerInterface* logger) {
  if (logger) {
    return std::make_unique<InternalMetricsImpl>(logger);
  }
  return std::make_unique<NoOpInternalMetrics>();
}

InternalMetricsImpl::InternalMetricsImpl(LoggerInterface* logger)
    : paused_(false), logger_(logger) {
  CHECK(logger_);
}

void InternalMetricsImpl::LoggerCalled(
    LoggerCallsMadeMetricDimensionLoggerMethod method, const Project& project) {
  if (paused_) {
    return;
  }

  auto status = logger_->LogEvent(kLoggerCallsMadeMetricId, method);

  if (status != kOK) {
    VLOG(1) << "InternalMetricsImpl::LoggerCalled: LogEvent() returned status="
            << status;
  }

  std::ostringstream component;
  component << project.customer_name() << '/' << project.project_name();
  status = logger_->LogEventCount(kPerProjectLoggerCallsMadeMetricId, method,
                                  component.str(), 0, 1);

  if (status != kOK) {
    VLOG(1)
        << "InternalMetricsImpl::LoggerCalled: LogEventCount() returned status="
        << status;
  }
}

void InternalMetricsImpl::BytesUploaded(
    PerDeviceBytesUploadedMetricDimensionStatus upload_status,
    int64_t byte_count) {
  if (paused_) {
    return;
  }

  auto status = logger_->LogEventCount(kPerDeviceBytesUploadedMetricId,
                                       upload_status, "", 0, byte_count);

  if (status != kOK) {
    VLOG(1) << "InternalMetricsImpl::BytesUploaded: LogEventCount() returned "
            << "status=" << status;
  }
}

void InternalMetricsImpl::BytesStored(
    PerProjectBytesStoredMetricDimensionStatus upload_status,
    int64_t byte_count, uint32_t customer_id, uint32_t project_id) {
  if (paused_) {
    return;
  }

  std::ostringstream component;
  component << customer_id << '/' << project_id;

  auto status =
      logger_->LogMemoryUsage(kPerProjectBytesStoredMetricId, upload_status,
                              component.str(), byte_count);

  if (status != kOK) {
    VLOG(1)
        << "InternalMetricsImpl::BytesStored: LogEventCount() returned status="
        << status;
  }
}

}  // namespace cobalt::logger

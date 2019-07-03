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

std::unique_ptr<InternalMetrics> InternalMetrics::NewWithLogger(
    LoggerInterface* logger) {
  return NewWithLoggerAndCobaltRegistry(logger, nullptr);
}

std::unique_ptr<InternalMetrics>
InternalMetrics::NewWithLoggerAndCobaltRegistry(
    LoggerInterface* logger, const CobaltRegistry* cobalt_registry) {
  if (logger) {
    return std::make_unique<InternalMetricsImpl>(logger, cobalt_registry);
  } else {
    return std::make_unique<NoOpInternalMetrics>();
  }
}

InternalMetricsImpl::InternalMetricsImpl(LoggerInterface* logger,
                                         const CobaltRegistry* cobalt_registry)
    : paused_(false), logger_(logger) {
  CHECK(logger_);

  if (cobalt_registry == nullptr) {
    LOG(ERROR) << "The provided cobalt_registry was nullptr.";
    return;
  }

  for (const auto& customer : cobalt_registry->customers()) {
    for (const auto& project : customer.projects()) {
      std::ostringstream component_name;
      component_name << customer.customer_name() << '/'
                     << project.project_name();
      component_name_by_ids[std::make_tuple(
          customer.customer_id(), project.project_id())] = component_name.str();
    }
  }
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

  auto status = logger_->LogMemoryUsage(
      kPerProjectBytesStoredMetricId, upload_status,
      GetComponentName(customer_id, project_id), byte_count);

  if (status != kOK) {
    VLOG(1)
        << "InternalMetricsImpl::BytesStored: LogEventCount() returned status="
        << status;
  }
}

std::string InternalMetricsImpl::GetComponentName(uint32_t customer_id,
                                                  uint32_t project_id) {
  auto component_name_iter =
      component_name_by_ids.find(std::make_tuple(customer_id, project_id));

  if (component_name_iter == component_name_by_ids.end()) {
    LOG(ERROR) << "Could not find Customer and Project names for customer_id="
               << customer_id << " and project_id=" << project_id;

    std::ostringstream component;
    component << customer_id << '/' << project_id;
    return component.str();
  }

  return component_name_iter->second;
}

}  // namespace logger
}  // namespace cobalt

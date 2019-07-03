// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_INTERNAL_METRICS_H_
#define COBALT_LOGGER_INTERNAL_METRICS_H_

#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "config/cobalt_registry.pb.h"
#include "config/project.pb.h"
#include "logger/internal_metrics_config.cb.h"
#include "logger/logger_interface.h"

namespace cobalt {
namespace logger {

// InternalMetrics defines the methods used for collecting cobalt-internal
// metrics.
class InternalMetrics {
 public:
  // Returns a pointer to an InternalMetrics object which can be used for
  // collecting cobalt-internal metrics.
  //
  // |logger| the logger used to log internal metrics. If the pointer is null,
  // NoOpInternalMetrics will be used.
  static std::unique_ptr<InternalMetrics> NewWithLogger(
      LoggerInterface* logger);

  // Returns a pointer to an InternalMetrics object which can be used for
  // collecting cobalt-internal metrics.
  //
  // |logger| the logger used to log internal metrics. If the pointer is null,
  // NoOpInternalMetrics will be used.
  // |cobalt_registry_bytes| serialized contents of CobaltRegistry. It will be
  // used to map customer_id and project_id to customer_name and project_name.
  static std::unique_ptr<InternalMetrics> NewWithLoggerAndCobaltRegistry(
      LoggerInterface* logger, const CobaltRegistry* cobalt_registry);

  // LoggerCalled (cobalt_internal::metrics::logger_calls_made) and
  // (cobalt_internal::metrics::per_project_logger_calls_made) are logged for
  // every call to Logger along with which method was called and the project
  // that called it.
  virtual void LoggerCalled(LoggerCallsMadeMetricDimensionLoggerMethod method,
                            const Project& project) = 0;

  // cobalt_internal::metrics::per_device_bytes_uploaded is logged when the
  // Shipping Manager attempts or succeeds to upload observations from the
  // device.
  virtual void BytesUploaded(
      PerDeviceBytesUploadedMetricDimensionStatus upload_status,
      int64_t byte_count) = 0;

  // cobalt_internal::metrics::per_project_bytes_stored is logged when the
  // Observation Store attempts or succeeds to store observations on the device.
  virtual void BytesStored(
      PerProjectBytesStoredMetricDimensionStatus upload_status,
      int64_t byte_count, uint32_t customer_id, uint32_t project_id) = 0;

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
 public:
  void LoggerCalled(LoggerCallsMadeMetricDimensionLoggerMethod method,
                    const Project& project) override {}

  void BytesUploaded(PerDeviceBytesUploadedMetricDimensionStatus upload_status,
                     int64_t byte_count) override {}

  void BytesStored(PerProjectBytesStoredMetricDimensionStatus upload_status,
                   int64_t byte_count, uint32_t customer_id,
                   uint32_t project_id) override {}

  void PauseLogging() override {}
  void ResumeLogging() override {}

  ~NoOpInternalMetrics() override {}
};

// InternalMetricsImpl is the actual implementation of InternalMetrics. It is a
// wrapper around the (non nullptr) LoggerInterface* that was provided to the
// Logger constructor.
class InternalMetricsImpl : public InternalMetrics {
 public:
  InternalMetricsImpl(LoggerInterface* logger,
                      const CobaltRegistry* cobalt_registry);

  void LoggerCalled(LoggerCallsMadeMetricDimensionLoggerMethod method,
                    const Project& project) override;

  void BytesUploaded(PerDeviceBytesUploadedMetricDimensionStatus upload_status,
                     int64_t byte_count) override;

  void BytesStored(PerProjectBytesStoredMetricDimensionStatus upload_status,
                   int64_t byte_count, uint32_t customer_id,
                   uint32_t project_id) override;

  void PauseLogging() override { paused_ = true; }
  void ResumeLogging() override { paused_ = false; }

  ~InternalMetricsImpl() override {}

 private:
  // Returns "<customer_name>/<project_name>" if the mapping exists in the
  // Cobalt Registry. Otherwise returns "<customer_id>/<project_id>".
  std::string GetComponentName(uint32_t customer_id, uint32_t project_id);

  bool paused_;

  // Map of <customer_id, project_id> to a string of the format
  // "<customer_name>/<project_name>".
  //
  // TODO(ninai): Add the option of updating these mappings in the
  // ObservationStore and ShippingManager by the Loggers. Currently, these will
  // become stale if the CobaltApp is not updated.
  std::map<std::tuple<uint32_t, uint32_t>, std::string> component_name_by_ids;

  LoggerInterface* logger_;  // not owned
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_INTERNAL_METRICS_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOGGER_LOGGER_INTERFACE_H_
#define COBALT_SRC_LOGGER_LOGGER_INTERFACE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/logger/internal_metrics_config.cb.h"
#include "src/logger/status.h"
#include "src/logger/types.h"
#include "src/pb/observation2.pb.h"

namespace cobalt::logger {

// Logger is the client-facing interface to Cobalt.
//
// LoggerInterface is an abstract interface to Logger that allows Logger to
// be mocked out in tests.
class LoggerInterface {
 public:
  virtual ~LoggerInterface() = default;

  // Old Logger Log* methods used in Cobalt 1.0.
  // TODO(fxb/45463) Delete when all users move to Cobalt 1.1.

  // Logs the fact that an event has occurred.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type EVENT_OCCURRED.
  //
  // |event_code| The index of the event type that occurred. The indexed
  // set of all event types is specified in the Metric definition. Use 0
  // if there is no natural notion of event type.
  //
  virtual Status LogEvent(uint32_t metric_id, uint32_t event_code) = 0;

  // Logs that an event has occurred a given number of times.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type EVENT_COUNT.
  //
  // |event_codes| The event codes for the event that occurred. There must be
  // one for each metric dimension specified in the MetricDefinition. Use the
  // empty vector if no metric dimensions have been defined.
  //
  // |component| Optionally, a component associated with the event may also be
  // logged. Use the empty string if there is no natural notion of component.
  //
  // |period_duration_micros| Optionally, the period of time over which
  // the |count| events occurred may be logged. If this is not relevant the
  // value may be set to 0. Otherwise specify the period duration as a number
  // of microseconds.
  //
  // |count| The number of times the event occurred. One may choose to
  // always set this value to 1 and always set |period_duration_micros| to 0
  // in order to achieve a semantics similar to the LogEventOccurred() method,
  // but with a |component|.
  virtual Status LogEventCount(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                               const std::string& component, int64_t period_duration_micros,
                               uint32_t count) = 0;

  // DEPRECATED. TODO(zmbush): Remove
  Status LogEventCount(uint32_t metric_id, uint32_t event_code, const std::string& component,
                       int64_t period_duration_micros, uint32_t count) {
    return LogEventCount(metric_id, (std::vector<uint32_t>){event_code}, component,
                         period_duration_micros, count);
  }

  // Logs that an event lasted a given amount of time.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type ELAPSED_TIME.
  //
  // |event_codes| The event codes for the event that occurred. There must be
  // one for each metric dimension specified in the MetricDefinition. Use the
  // empty vector if no metric dimensions have been defined.
  //
  // |component| Optionally, a component associated with the event may also be
  // logged. Use the empty string if there is no natural notion of component.
  //
  // |elapsed_micros| The elapsed time of the event, specified as a number
  // of microseconds.
  virtual Status LogElapsedTime(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                                const std::string& component, int64_t elapsed_micros) = 0;

  // DEPRECATED. TODO(zmbush): Remove
  Status LogElapsedTime(uint32_t metric_id, uint32_t event_code, const std::string& component,
                        int64_t elapsed_micros) {
    return LogElapsedTime(metric_id, (std::vector<uint32_t>){event_code}, component,
                          elapsed_micros);
  }

  // Logs a measured average frame rate.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type FRAME_RATE.
  //
  // |event_codes| The event codes for the event that occurred. There must be
  // one for each metric dimension specified in the MetricDefinition. Use the
  // empty vector if no metric dimensions have been defined.
  //
  // |component| Optionally, a component associated with the event may also be
  // logged. Use the empty string if there is no natural notion of component.
  //
  // |fps| The average-frame rate in frames-per-second.
  virtual Status LogFrameRate(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                              const std::string& component, float fps) = 0;

  // DEPRECATED. TODO(zmbush): Remove
  Status LogFrameRate(uint32_t metric_id, uint32_t event_code, const std::string& component,
                      float fps) {
    return LogFrameRate(metric_id, (std::vector<uint32_t>){event_code}, component, fps);
  }

  // Logs a measured memory usage.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type MEMORY_USAGE.
  //
  // |event_codes| The event codes for the event that occurred. There must be
  // one for each metric dimension specified in the MetricDefinition. Use the
  // empty vector if no metric dimensions have been defined.
  //
  // |component| Optionally, a component associated with the event may also be
  // logged. Use the empty string if there is no natural notion of component.
  //
  // |bytes| The memory used, in bytes.
  virtual Status LogMemoryUsage(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                                const std::string& component, int64_t bytes) = 0;

  // DEPRECATED. TODO(zmbush): Remove
  Status LogMemoryUsage(uint32_t metric_id, uint32_t event_code, const std::string& component,
                        int64_t bytes) {
    return LogMemoryUsage(metric_id, (std::vector<uint32_t>){event_code}, component, bytes);
  }

  // Logs a histogram over a set of integer buckets. The meaning of the
  // Metric and the buckets is specified in the Metric definition.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type INT_HISTOGRAM.
  //
  // |event_codes| The event codes for the event that occurred. There must be
  // one for each metric dimension specified in the MetricDefinition. Use the
  // empty vector if no metric dimensions have been defined.
  //
  // |component| Optionally, a component associated with the event may also be
  // logged. Use the empty string if there is no natural notion of component.
  //
  // |histogram| The histogram to log. Each HistogramBucket gives the count
  //  for one bucket of the histogram. The definitions of the buckets is
  //  given in the Metric definition.
  virtual Status LogIntHistogram(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                                 const std::string& component, HistogramPtr histogram) = 0;

  // DEPRECATED. TODO(zmbush): Remove
  Status LogIntHistogram(uint32_t metric_id, uint32_t event_code, const std::string& component,
                         HistogramPtr histogram) {
    return LogIntHistogram(metric_id, (std::vector<uint32_t>){event_code}, component,
                           std::move(histogram));
  }

  // New Logger Log* methods for use in Cobalt 1.1.

  // Logs that an event has occurred a given number of times.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type OCCURRENCE.
  //
  // |count| The number of times the event occurred. One may choose to
  // always set this value to 1 if events are logged every time they occur.
  //
  // |event_codes| The event codes for the event that occurred. There must be
  // one for each metric dimension specified in the MetricDefinition. Use the
  // empty vector if no metric dimensions have been defined.
  virtual Status LogOccurrence(uint32_t metric_id, uint64_t count,
                               const std::vector<uint32_t>& event_codes) = 0;

  // Logs a measured integer value.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type INTEGER.
  //
  // |value| The integer measured.
  //
  // |event_codes| The event codes for the event that occurred. There must be
  // one for each metric dimension specified in the MetricDefinition. Use the
  // empty vector if no metric dimensions have been defined.
  virtual Status LogInteger(uint32_t metric_id, int64_t value,
                            const std::vector<uint32_t>& event_codes) = 0;

  // Logs a histogram over a set of integer buckets. The meaning of the
  // Metric and the buckets is specified in the Metric definition.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type INTEGER_HISTOGRAM.
  //
  // |histogram| The histogram to log. Each HistogramBucket gives the count
  //  for one bucket of the histogram. The definitions of the buckets is
  //  given in the Metric definition.
  //
  // |event_codes| The event codes for the event that occurred. There must be
  // one for each metric dimension specified in the MetricDefinition. Use the
  // empty vector if no metric dimensions have been defined.
  virtual Status LogIntegerHistogram(uint32_t metric_id, HistogramPtr histogram,
                                     const std::vector<uint32_t>& event_codes) = 0;

  // Logs a string value that was observed.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type ELAPSED_TIME.
  //
  // |string_value| The string that was observed.
  //
  // |event_codes| The event codes for the event that occurred. There must be
  // one for each metric dimension specified in the MetricDefinition. Use the
  // empty vector if no metric dimensions have been defined.
  virtual Status LogString(uint32_t metric_id, const std::string& string_value,
                           const std::vector<uint32_t>& event_codes) = 0;

  // Logs a custom event. The structure of the event is defined in a proto file
  // in the project's config folder.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type CUSTOM.
  //
  // |event_values| The event to log. EventValues represent the contents of the
  // proto file that will be collected. Each ValuePart represents a single
  // dimension of the logged event. The conversion to proto is done  serverside,
  // therefore it is the client's responsibility to make sure the EventValues
  // contents match the proto defined.
  virtual Status LogCustomEvent(uint32_t metric_id, EventValuesPtr event_values) = 0;

  // LoggerCalled (cobalt_internal::metrics::logger_calls_made) and
  // (cobalt_internal::metrics::per_project_logger_calls_made) are logged for
  // every call to Logger along with which method was called and the project
  // that called it.
  virtual void RecordLoggerCall(PerProjectLoggerCallsMadeMetricDimensionLoggerMethod method) = 0;

  // Pauses Cobalt's internal metrics collection.
  virtual void PauseInternalLogging() = 0;

  // Resumes Cobalt's internal metrics collection.
  virtual void ResumeInternalLogging() = 0;
};

}  // namespace cobalt::logger

#endif  // COBALT_SRC_LOGGER_LOGGER_INTERFACE_H_

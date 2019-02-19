// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_LOGGER_INTERFACE_H_
#define COBALT_LOGGER_LOGGER_INTERFACE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "logger/encoder.h"
#include "logger/status.h"

namespace cobalt {
namespace logger {

// Logger is the client-facing interface to Cobalt.
//
// LoggerInterface is an abstract interface to Logger that allows Logger to
// be mocked out in tests.
class LoggerInterface {
 public:
  virtual ~LoggerInterface() = default;

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
  // |event_code| The index of the event type that occurred. The indexed
  // set of all event types is specified in the Metric definition. Use 0
  // if there is no natural notion of event type.
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
  virtual Status LogEventCount(uint32_t metric_id, uint32_t event_code,
                               const std::string& component,
                               int64_t period_duration_micros,
                               uint32_t count) = 0;

  // Logs that an event lasted a given amount of time.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type ELAPSED_TIME.
  //
  // |event_code| The index of the event type that occurred. The indexed
  // set of all event types is specified in the Metric definition. Use 0
  // if there is no natural notion of event type.
  //
  // |component| Optionally, a component associated with the event may also be
  // logged. Use the empty string if there is no natural notion of component.
  //
  // |elapsed_micros| The elapsed time of the event, specified as a number
  // of microseconds.
  virtual Status LogElapsedTime(uint32_t metric_id, uint32_t event_code,
                                const std::string& component,
                                int64_t elapsed_micros) = 0;

  // Logs a measured average frame rate.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type FRAME_RATE.
  //
  // |event_code| The index of the event type that occurred. The indexed
  // set of all event types is specified in the Metric definition. Use 0
  // if there is no natural notion of event type.
  //
  // |component| Optionally, a component associated with the event may also be
  // logged. Use the empty string if there is no natural notion of component.
  //
  // |fps| The average-frame rate in frames-per-second.
  virtual Status LogFrameRate(uint32_t metric_id, uint32_t event_code,
                              const std::string& component, float fps) = 0;
  // Logs a measured memory usage.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type MEMORY_USAGE.
  //
  // |event_code| The index of the event type that occurred. The indexed
  // set of all event types is specified in the Metric definition. Use 0
  // if there is no natural notion of event type.
  //
  // |component| Optionally, a component associated with the event may also be
  // logged. Use the empty string if there is no natural notion of component.
  //
  // |bytes| The memory used, in bytes.
  virtual Status LogMemoryUsage(uint32_t metric_id, uint32_t event_code,
                                const std::string& component,
                                int64_t bytes) = 0;

  // Logs a histogram over a set of integer buckets. The meaning of the
  // Metric and the buckets is specified in the Metric definition.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type INT_HISTOGRAM.
  //
  // |event_code| The index of the event type that occurred. The indexed
  // set of all event types is specified in the Metric definition. Use 0
  // if there is no natural notion of event type.
  //
  // |component| Optionally, a component associated with the event may also be
  // logged. Use the empty string if there is no natural notion of component.
  //
  // |histogram| The histogram to log. Each HistogramBucket gives the count
  //  for one bucket of the histogram. The definitions of the buckets is
  //  given in the Metric definition.
  virtual Status LogIntHistogram(uint32_t metric_id, uint32_t event_code,
                                 const std::string& component,
                                 HistogramPtr histogram) = 0;

  // Logs the fact that a given string was used, in a specific context.
  // The semantics of the context and the string is specified in the
  // Metric definition.
  //
  //  This method is intended to be used in the following situation:
  //  * The string s being logged does not contain PII or passwords.
  //  * The set S of all possible strings that may be logged is large.
  //    If the set S is small consider using LogEvent() instead.
  //  * The ultimate data of interest is the statistical distribution of the
  //    most commonly used strings from S over the population of all Fuchsia
  //    devices.
  //
  // |metric_id| ID of the Metric the logged Event will belong to. It must
  // be one of the Metrics from the ProjectContext passed to the constructor,
  // and it must be of type STRING_USED.
  //
  // |str| The human-readable string to log.
  virtual Status LogString(uint32_t metric_id, const std::string& str) = 0;

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
  virtual Status LogCustomEvent(uint32_t metric_id,
                                EventValuesPtr event_values) = 0;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_LOGGER_INTERFACE_H_

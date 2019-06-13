// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/logger.h"

#include <memory>
#include <string>

#include "./event.pb.h"
#include "./logging.h"
#include "./observation2.pb.h"
#include "./tracing.h"
#include "algorithms/rappor/rappor_config_helper.h"
#include "config/encodings.pb.h"
#include "config/id.h"
#include "config/metric_definition.pb.h"
#include "config/report_definition.pb.h"
#include "logger/event_record.h"
#include "util/datetime_util.h"

namespace cobalt {
namespace logger {

using ::cobalt::rappor::RapporConfigHelper;
using ::cobalt::util::ClockInterface;
using ::cobalt::util::SystemClock;
using ::cobalt::util::TimeToDayIndex;
using ::google::protobuf::RepeatedField;
using ::google::protobuf::RepeatedPtrField;

constexpr char TRACE_PREFIX[] = "[COBALT_EVENT_TRACE] ";

typedef LoggerCallsMadeMetricDimensionLoggerMethod LoggerMethod;

// EventLogger is an abstract interface used internally in logger.cc to
// dispatch logging logic based on Metric type. Below we create subclasses
// of EventLogger for each of several Metric types.
class EventLogger {
 public:
  explicit EventLogger(Logger* logger) : logger_(logger) {}

  virtual ~EventLogger() = default;

  // Finds the Metric with the given ID. Expects that this has type
  // |expected_metric_type|. If not logs an error and returns.
  // If so then Logs the Event specified by |event_record| to Cobalt.
  Status Log(uint32_t metric_id,
             MetricDefinition::MetricType expected_metric_type,
             EventRecord* event_record);

 protected:
  const Encoder* encoder() { return logger_->encoder_; }
  EventAggregator* event_aggregator() { return logger_->event_aggregator_; }
  const ProjectContext* project_context() {
    return logger_->project_context_.get();
  }
  Encoder::Result BadReportType(const MetricDefinition& metric,
                                const ReportDefinition& report);

  // Validates the supplied event_codes against the defined metric dimensions
  // in the MetricDefinition.
  virtual Status ValidateEventCodes(const MetricDefinition& metric,
                                    const RepeatedField<uint32_t>& event_codes);

 private:
  // Finishes setting up and then validates |event_record|.
  Status FinalizeEvent(uint32_t metric_id,
                       MetricDefinition::MetricType expected_type,
                       EventRecord* event_record);

  virtual Status ValidateEvent(const EventRecord& event_record);

  // Given an EventRecord and a ReportDefinition, determines whether or not
  // the Event should be used to update a local aggregation and if so passes
  // the Event to the Local Aggregator.
  virtual Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                             EventRecord* event_record);

  // Given an EventRecord and a ReportDefinition, determines whether or not
  // the Event should be used to generate an immediate Observation and if so
  // does generate one and writes it to the Observation Store.
  //
  // |may_invalidate| indicates that the implementation is allowed to invalidate
  // |event_record|. This should be set true only when it is known that
  // |event_record| is no longer needed. Setting this true allows the data in
  // |event_record| to be moved rather than copied.
  Status MaybeGenerateImmediateObservation(const ReportDefinition& report,
                                           bool may_invalidate,
                                           EventRecord* event_record);

  // Given an EventRecord and a ReportDefinition, determines whether or not
  // the Event should be used to generate an immediate Observation and if so
  // does generate one. This method is invoked by
  // MaybeGenerateImmediateObservation().
  //
  // |may_invalidate| indicates that the implementation is allowed to invalidate
  // |event_record|. This should be set true only when it is known that
  // |event_record| is no longer needed. Setting this true allows the data in
  // |event_record| to be moved rather than copied.
  virtual Encoder::Result MaybeEncodeImmediateObservation(
      const ReportDefinition& report, bool may_invalidate,
      EventRecord* event_record);

  std::string TraceEvent(Event* event);
  void TraceLogFailure(const Status& status, EventRecord* event_record,
                       const ReportDefinition& report);
  void TraceLogSuccess(EventRecord* event_record);

  Logger* logger_;
};

// Implementation of EventLogger for metrics of type EVENT_OCCURRED.
class OccurrenceEventLogger : public EventLogger {
 public:
  explicit OccurrenceEventLogger(Logger* logger) : EventLogger(logger) {}
  virtual ~OccurrenceEventLogger() = default;

 private:
  Status ValidateEvent(const EventRecord& event_record) override;
  Encoder::Result MaybeEncodeImmediateObservation(
      const ReportDefinition& report, bool may_invalidate,
      EventRecord* event_record) override;
  Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                     EventRecord* event_record) override;
};

// Implementation of EventLogger for metrics of type EVENT_COUNT.
class CountEventLogger : public EventLogger {
 public:
  explicit CountEventLogger(Logger* logger) : EventLogger(logger) {}
  virtual ~CountEventLogger() = default;

 private:
  Status ValidateEvent(const EventRecord& event_record) override;
  Encoder::Result MaybeEncodeImmediateObservation(
      const ReportDefinition& report, bool may_invalidate,
      EventRecord* event_record) override;
  Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                     EventRecord* event_record) override;
};

// Implementation of EventLogger for all of the numerical performance metric
// types. This is an abstract class. There are subclasses below for each
// metric type.
class IntegerPerformanceEventLogger : public EventLogger {
 protected:
  explicit IntegerPerformanceEventLogger(Logger* logger)
      : EventLogger(logger) {}
  virtual ~IntegerPerformanceEventLogger() = default;

 private:
  Encoder::Result MaybeEncodeImmediateObservation(
      const ReportDefinition& report, bool may_invalidate,
      EventRecord* event_record) override;
  virtual const RepeatedField<uint32_t>& EventCodes(const Event& event) = 0;
  virtual std::string Component(const Event& event) = 0;
  virtual int64_t IntValue(const Event& event) = 0;
};

// Implementation of EventLogger for metrics of type ELAPSED_TIME.
class ElapsedTimeEventLogger : public IntegerPerformanceEventLogger {
 public:
  explicit ElapsedTimeEventLogger(Logger* logger)
      : IntegerPerformanceEventLogger(logger) {}
  virtual ~ElapsedTimeEventLogger() = default;

 private:
  const RepeatedField<uint32_t>& EventCodes(const Event& event) override;
  std::string Component(const Event& event) override;
  int64_t IntValue(const Event& event) override;
  Status ValidateEvent(const EventRecord& event_record) override;
  Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                     EventRecord* event_record) override;
};

// Implementation of EventLogger for metrics of type FRAME_RATE.
class FrameRateEventLogger : public IntegerPerformanceEventLogger {
 public:
  explicit FrameRateEventLogger(Logger* logger)
      : IntegerPerformanceEventLogger(logger) {}
  virtual ~FrameRateEventLogger() = default;

 private:
  const RepeatedField<uint32_t>& EventCodes(const Event& event) override;
  std::string Component(const Event& event) override;
  int64_t IntValue(const Event& event) override;
  Status ValidateEvent(const EventRecord& event_record) override;
  Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                     EventRecord* event_record) override;
};

// Implementation of EventLogger for metrics of type MEMORY_USAGE.
class MemoryUsageEventLogger : public IntegerPerformanceEventLogger {
 public:
  explicit MemoryUsageEventLogger(Logger* logger)
      : IntegerPerformanceEventLogger(logger) {}
  virtual ~MemoryUsageEventLogger() = default;

 private:
  const RepeatedField<uint32_t>& EventCodes(const Event& event) override;
  std::string Component(const Event& event) override;
  int64_t IntValue(const Event& event) override;
  Status ValidateEvent(const EventRecord& event_record) override;
  Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                     EventRecord* event_record) override;
};

// Implementation of EventLogger for metrics of type INT_HISTOGRAM.
class IntHistogramEventLogger : public EventLogger {
 public:
  explicit IntHistogramEventLogger(Logger* logger) : EventLogger(logger) {}
  virtual ~IntHistogramEventLogger() = default;

 private:
  Status ValidateEvent(const EventRecord& event_record) override;
  Encoder::Result MaybeEncodeImmediateObservation(
      const ReportDefinition& report, bool may_invalidate,
      EventRecord* event_record) override;
};

// Implementation of EventLogger for metrics of type STRING_USED.
class StringUsedEventLogger : public EventLogger {
 public:
  explicit StringUsedEventLogger(Logger* logger) : EventLogger(logger) {}
  virtual ~StringUsedEventLogger() = default;

 private:
  Encoder::Result MaybeEncodeImmediateObservation(
      const ReportDefinition& report, bool may_invalidate,
      EventRecord* event_record) override;
};

// Implementation of EventLogger for metrics of type CUSTOM.
class CustomEventLogger : public EventLogger {
 public:
  explicit CustomEventLogger(Logger* logger) : EventLogger(logger) {}
  virtual ~CustomEventLogger() = default;

 private:
  Status ValidateEvent(const EventRecord& event_record) override;
  Encoder::Result MaybeEncodeImmediateObservation(
      const ReportDefinition& report, bool may_invalidate,
      EventRecord* event_record) override;
};

namespace {

template <class EventType>
void CopyEventCodesAndComponent(const std::vector<uint32_t>& event_codes,
                                const std::string& component,
                                EventType* event) {
  for (auto event_code : event_codes) {
    event->add_event_code(event_code);
  }
  event->set_component(component);
}

}  // namespace

//////////////////// Logger method implementations ////////////////////////

Logger::Logger(std::unique_ptr<ProjectContext> project_context,
               const Encoder* encoder, EventAggregator* event_aggregator,
               ObservationWriter* observation_writer,
               LoggerInterface* internal_logger)
    : Logger(std::move(project_context), encoder, event_aggregator,
             observation_writer, nullptr, internal_logger) {}

Logger::Logger(std::unique_ptr<ProjectContext> project_context,
               const Encoder* encoder, EventAggregator* event_aggregator,
               ObservationWriter* observation_writer,
               encoder::SystemDataInterface* system_data,
               LoggerInterface* internal_logger)
    : project_context_(std::move(project_context)),
      encoder_(encoder),
      event_aggregator_(event_aggregator),
      observation_writer_(observation_writer),
      system_data_(system_data),
      clock_(new SystemClock()) {
  CHECK(project_context_);
  CHECK(encoder_);
  CHECK(event_aggregator_);
  CHECK(observation_writer_);
  if (internal_logger) {
    internal_metrics_.reset(new InternalMetricsImpl(internal_logger));
  } else {
    // We were not provided with a metrics logger. We must create one.
    internal_metrics_.reset(new NoOpInternalMetrics());
  }
  if (event_aggregator_->UpdateAggregationConfigs(*project_context_) != kOK) {
    LOG(ERROR) << "Failed to provide aggregation configurations to the "
                  "EventAggregator.";
  }
}

Status Logger::LogEvent(uint32_t metric_id, uint32_t event_code) {
  VLOG(4) << "Logger::LogEvent(" << metric_id << ", " << event_code
          << ") project=" << project_context_->FullyQualifiedName();
  EventRecord event_record;
  internal_metrics_->LoggerCalled(LoggerMethod::LogEvent,
                                  project_context_->project());
  auto* occurrence_event = event_record.event->mutable_occurrence_event();
  occurrence_event->set_event_code(event_code);
  auto event_logger = std::make_unique<OccurrenceEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::EVENT_OCCURRED,
                           &event_record);
}

Status Logger::LogEventCount(uint32_t metric_id,
                             const std::vector<uint32_t>& event_codes,
                             const std::string& component,
                             int64_t period_duration_micros, uint32_t count) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogEventCount,
                                  project_context_->project());
  EventRecord event_record;
  auto* count_event = event_record.event->mutable_count_event();
  CopyEventCodesAndComponent(event_codes, component, count_event);
  count_event->set_period_duration_micros(period_duration_micros);
  count_event->set_count(count);
  auto event_logger = std::make_unique<CountEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::EVENT_COUNT,
                           &event_record);
}

Status Logger::LogElapsedTime(uint32_t metric_id,
                              const std::vector<uint32_t>& event_codes,
                              const std::string& component,
                              int64_t elapsed_micros) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogElapsedTime,
                                  project_context_->project());
  EventRecord event_record;
  auto* elapsed_time_event = event_record.event->mutable_elapsed_time_event();
  CopyEventCodesAndComponent(event_codes, component, elapsed_time_event);
  elapsed_time_event->set_elapsed_micros(elapsed_micros);
  auto event_logger = std::make_unique<ElapsedTimeEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::ELAPSED_TIME,
                           &event_record);
}

Status Logger::LogFrameRate(uint32_t metric_id,
                            const std::vector<uint32_t>& event_codes,
                            const std::string& component, float fps) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogFrameRate,
                                  project_context_->project());
  EventRecord event_record;
  auto* frame_rate_event = event_record.event->mutable_frame_rate_event();
  CopyEventCodesAndComponent(event_codes, component, frame_rate_event);
  frame_rate_event->set_frames_per_1000_seconds(std::round(fps * 1000.0));
  auto event_logger = std::make_unique<FrameRateEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::FRAME_RATE,
                           &event_record);
}

Status Logger::LogMemoryUsage(uint32_t metric_id,
                              const std::vector<uint32_t>& event_codes,
                              const std::string& component, int64_t bytes) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogMemoryUsage,
                                  project_context_->project());
  EventRecord event_record;
  auto* memory_usage_event = event_record.event->mutable_memory_usage_event();
  CopyEventCodesAndComponent(event_codes, component, memory_usage_event);
  memory_usage_event->set_bytes(bytes);
  auto event_logger = std::make_unique<MemoryUsageEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::MEMORY_USAGE,
                           &event_record);
}

Status Logger::LogIntHistogram(uint32_t metric_id,
                               const std::vector<uint32_t>& event_codes,
                               const std::string& component,
                               HistogramPtr histogram) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogIntHistogram,
                                  project_context_->project());
  EventRecord event_record;
  auto* int_histogram_event = event_record.event->mutable_int_histogram_event();
  CopyEventCodesAndComponent(event_codes, component, int_histogram_event);
  int_histogram_event->mutable_buckets()->Swap(histogram.get());
  auto event_logger = std::make_unique<IntHistogramEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::INT_HISTOGRAM,
                           &event_record);
}

Status Logger::LogString(uint32_t metric_id, const std::string& str) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogString,
                                  project_context_->project());
  EventRecord event_record;
  auto* string_used_event = event_record.event->mutable_string_used_event();
  string_used_event->set_str(str);
  auto event_logger = std::make_unique<StringUsedEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::STRING_USED,
                           &event_record);
}

Status Logger::LogCustomEvent(uint32_t metric_id, EventValuesPtr event_values) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogCustomEvent,
                                  project_context_->project());
  EventRecord event_record;
  auto* custom_event = event_record.event->mutable_custom_event();
  custom_event->mutable_values()->swap(*event_values);
  auto event_logger = std::make_unique<CustomEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::CUSTOM, &event_record);
}

void Logger::PauseInternalLogging() { internal_metrics_->PauseLogging(); }
void Logger::ResumeInternalLogging() { internal_metrics_->ResumeLogging(); }

//////////////////// EventLogger method implementations ////////////////////////

std::string EventLogger::TraceEvent(Event* event) {
  std::stringstream ss;
  ss << "Day index: " << event->day_index() << std::endl;
  if (event->has_occurrence_event()) {
    auto e = event->occurrence_event();
    ss << "OccurrenceEvent: " << e.event_code() << std::endl;
  } else if (event->has_count_event()) {
    auto e = event->count_event();
    ss << "CountEvent:" << std::endl;
    ss << "EventCodes:";
    for (const auto& code : e.event_code()) {
      ss << " " << code;
    }
    ss << ", Component: " << e.component()
       << ", PeriodDurationMicros: " << e.period_duration_micros()
       << ", Count: " << e.count() << std::endl;
  } else if (event->has_elapsed_time_event()) {
    auto e = event->elapsed_time_event();
    ss << "ElapsedTimeEvent:" << std::endl;
    ss << "EventCodes:";
    for (const auto& code : e.event_code()) {
      ss << " " << code;
    }
    ss << ", Component: " << e.component()
       << ", ElapsedMicros: " << e.elapsed_micros() << std::endl;
  } else if (event->has_frame_rate_event()) {
    auto e = event->frame_rate_event();
    ss << "FrameRateEvent:" << std::endl;
    ss << "EventCodes:";
    for (const auto& code : e.event_code()) {
      ss << " " << code;
    }
    ss << ", Component: " << e.component()
       << ", FramesPer1000Seconds: " << e.frames_per_1000_seconds()
       << std::endl;
  } else if (event->has_memory_usage_event()) {
    auto e = event->memory_usage_event();
    ss << "MemoryUsageEvent:" << std::endl;
    ss << "EventCodes:";
    for (const auto& code : e.event_code()) {
      ss << " " << code;
    }
    ss << ", Component: " << e.component() << ", Bytes: " << e.bytes()
       << std::endl;
  } else if (event->has_int_histogram_event()) {
    auto e = event->int_histogram_event();
    ss << "IntHistogramEvent:" << std::endl;
    ss << "EventCodes:";
    for (const auto& code : e.event_code()) {
      ss << " " << code;
    }
    ss << ", Component: " << e.component() << std::endl;
    for (const auto& bucket : e.buckets()) {
      ss << "| " << bucket.index() << " = " << bucket.count() << std::endl;
    }
  } else if (event->has_string_used_event()) {
    auto e = event->string_used_event();
    ss << "StringUsedEvent: " << e.str() << std::endl;
  } else if (event->has_custom_event()) {
    auto e = event->custom_event();
    ss << "CustomEvent:";
    if (e.values().empty()) {
      ss << " (Empty)";
    }
    ss << std::endl;
    for (const auto& entry : e.values()) {
      switch (entry.second.data_case()) {
        ss << "| " << entry.first << " = ";
        case CustomDimensionValue::kStringValue:
          ss << entry.second.string_value();
          break;
        case CustomDimensionValue::kIntValue:
          ss << entry.second.int_value();
          break;
        case CustomDimensionValue::kBlobValue:
          ss << "Blob";
          break;
        case CustomDimensionValue::kIndexValue:
          ss << entry.second.index_value();
          break;
        case CustomDimensionValue::kDoubleValue:
          ss << entry.second.double_value();
          break;
        default:
          ss << "No dimension value";
          break;
      }
      ss << std::endl;
    }
  }

  return ss.str();
}

void EventLogger::TraceLogFailure(const Status& status,
                                  EventRecord* event_record,
                                  const ReportDefinition& report) {
  if (!event_record->metric->meta_data().also_log_locally()) {
    return;
  }

  LOG(INFO)
      << TRACE_PREFIX << "("
      << project_context()->RefMetric(event_record->metric).FullyQualifiedName()
      << "): Error (" << status << ")" << std::endl
      << TraceEvent(event_record->event.get())
      << "While trying to send report: " << report.report_name() << std::endl;
}

void EventLogger::TraceLogSuccess(EventRecord* event_record) {
  if (!event_record->metric->meta_data().also_log_locally()) {
    return;
  }

  LOG(INFO)
      << TRACE_PREFIX << "("
      << project_context()->RefMetric(event_record->metric).FullyQualifiedName()
      << "):" << std::endl
      << TraceEvent(event_record->event.get());
}

Status EventLogger::Log(uint32_t metric_id,
                        MetricDefinition::MetricType expected_metric_type,
                        EventRecord* event_record) {
  TRACE_DURATION("cobalt_core", "EventLogger::Log", "metric_id", metric_id);

  if (logger_->system_data_) {
    if (logger_->system_data_->release_stage() >
        event_record->metric->meta_data().max_release_stage()) {
      // Quietly ignore this metric.
      LOG_FIRST_N(INFO, 10)
          << "Not logging metric `"
          << project_context()->FullMetricName(*event_record->metric)
          << "` because its max_release_stage ("
          << event_record->metric->meta_data().max_release_stage()
          << ") is lower than the device's current release_stage: "
          << logger_->system_data_->release_stage();
      return kOK;
    }
  }

  auto status = FinalizeEvent(metric_id, expected_metric_type, event_record);
  if (status != kOK) {
    return status;
  }

  int num_reports = event_record->metric->reports_size();
  int report_index = 0;
  if (event_record->metric->reports_size() == 0) {
    VLOG(1) << "Warning: An event was logged for a metric with no reports "
               "defined: "
            << project_context()->FullMetricName(*event_record->metric);
  }

  for (const auto& report : event_record->metric->reports()) {
    status = MaybeUpdateLocalAggregation(report, event_record);
    if (status != kOK) {
      TraceLogFailure(status, event_record, report);
      return status;
    }

    // If we are processing the final report, then we set may_invalidate
    // to true in order to allow data to be moved out of |event_record|
    // instead of being copied. One example where this is useful is when
    // creating an immediate Observation of type Histogram. In that case
    // we can move the histogram from the Event to the Observation and
    // avoid copying. Since the |event_record| is invalidated, any other
    // operation on the |event_record| must be performed before this for
    // loop.
    bool may_invalidate = ++report_index == num_reports;
    status =
        MaybeGenerateImmediateObservation(report, may_invalidate, event_record);
    if (status != kOK) {
      TraceLogFailure(status, event_record, report);
      return status;
    }
  }

  TraceLogSuccess(event_record);
  return kOK;
}

Status EventLogger::FinalizeEvent(uint32_t metric_id,
                                  MetricDefinition::MetricType expected_type,
                                  EventRecord* event_record) {
  event_record->metric = project_context()->GetMetric(metric_id);
  if (event_record->metric == nullptr) {
    LOG(ERROR) << "There is no metric with ID '" << metric_id << "' registered "
               << "in project '" << project_context()->FullyQualifiedName()
               << "'.";
    return kInvalidArguments;
  }
  if (event_record->metric->metric_type() != expected_type) {
    LOG(ERROR) << "Metric "
               << project_context()->FullMetricName(*event_record->metric)
               << " is not of type " << expected_type << ".";
    return kInvalidArguments;
  }

  // Compute the day_index.
  event_record->event->set_day_index(TimeToDayIndex(
      std::chrono::system_clock::to_time_t(logger_->clock_->now()),
      event_record->metric->time_zone_policy()));

  return ValidateEvent(*event_record);
}

Status EventLogger::ValidateEvent(const EventRecord& event_record) {
  return kOK;
}

Status EventLogger::ValidateEventCodes(
    const MetricDefinition& metric,
    const RepeatedField<uint32_t>& event_codes) {
  // Special case: Users of the version of the Log*() method that takes
  // a single event code as opposed to a vector of event codes use the
  // convention of passing a zero value for the single event code in the
  // case that the metric does not have any metric dimensions defined. We have
  // no way of distinguishing here that case from the case in which the user
  // invoked the other version of the method and explicitly passed a vector of
  // length one containing a single zero event code. Therefore we must accept
  // a single 0 event code when there are no metric dimensions defined.
  // The packing of multiple event codes into a single integer field in an
  // Observation (see config/packed_event_codes.h) does not distinguish
  // between a single event code with value 0 and an empty list of event codes.
  // Therefore the server also cannot tell the difference between these two
  // cases just by looking at an Observation and relies on the metric definition
  // to distinguish them. Consequently there is no harm in us passing the
  // single zero value to the Encoder: It will produce the same Observation
  // whether or not we do.
  if (metric.metric_dimensions_size() == 0 && event_codes.size() == 1 &&
      event_codes.Get(0) == 0) {
    return kOK;
  }
  if (metric.metric_dimensions_size() != event_codes.size()) {
    LOG(ERROR) << "The number of event_codes given, " << event_codes.size()
               << ", does not match the number of metric_dimensions, "
               << metric.metric_dimensions_size() << ", for metric "
               << project_context()->FullMetricName(metric) << ".";
    return kInvalidArguments;
  }
  for (int i = 0; i < event_codes.size(); i++) {
    auto dim = metric.metric_dimensions(i);
    auto code = event_codes.Get(i);

    // This verifies the two possible validation modes for a metric_dimension.
    //
    // 1. If it has a max_event_code, then all we do is verify that the supplied
    //    code is <= that value
    //
    // 2. If no max_event_code is specified, we verify that the supplied code
    //    maps to one of the values in the event_code map.
    if (dim.max_event_code() > 0) {
      if (code > dim.max_event_code()) {
        LOG(ERROR) << "The event_code given for dimension " << i << ", " << code
                   << ", exceeds the max_event_code for that dimension, "
                   << dim.max_event_code() << ", for metric "
                   << project_context()->FullMetricName(metric);
        return kInvalidArguments;
      }
    } else {
      bool valid = false;
      for (const auto& event_code : dim.event_codes()) {
        if (event_code.first == code) {
          valid = true;
          break;
        }
      }

      if (!valid) {
        LOG(ERROR) << "The event_code given for dimension " << i << ", " << code
                   << ", is not a valid event code for that dimension."
                   << ". You must either define this event code in"
                      " the metric_dimension, or set max_event_code >= "
                   << code << ", for metric "
                   << project_context()->FullMetricName(metric);
        return kInvalidArguments;
      }
    }
  }
  return kOK;
}

// The default implementation of MaybeUpdateLocalAggregation does nothing
// and returns OK.
Status EventLogger::MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                                EventRecord* event_record) {
  return kOK;
}

Status EventLogger::MaybeGenerateImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
  TRACE_DURATION("cobalt_core",
                 "EventLogger::MaybeGenerateImmediateObservation");

  auto encoder_result =
      MaybeEncodeImmediateObservation(report, may_invalidate, event_record);
  if (encoder_result.status != kOK) {
    return encoder_result.status;
  }
  if (encoder_result.observation == nullptr) {
    return kOK;
  }
  return logger_->observation_writer_->WriteObservation(
      *encoder_result.observation, std::move(encoder_result.metadata));
}

// The default implementation of MaybeEncodeImmediateObservation does
// nothing and returns OK.
Encoder::Result EventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
  TRACE_DURATION("cobalt_core", "EventLogger::MaybeEncodeImmediateObservation");
  Encoder::Result result;
  result.status = kOK;
  result.observation = nullptr;
  return result;
}

Encoder::Result EventLogger::BadReportType(const MetricDefinition& metric,
                                           const ReportDefinition& report) {
  LOG(ERROR) << "Invalid Cobalt config: Report " << report.report_name()
             << " for metric " << project_context()->FullMetricName(metric)
             << " is not of an appropriate type for the metric type.";
  Encoder::Result encoder_result;
  encoder_result.status = kInvalidConfig;
  return encoder_result;
}

/////////////// OccurrenceEventLogger method implementations ///////////////////

Status OccurrenceEventLogger::ValidateEvent(const EventRecord& event_record) {
  CHECK(event_record.event->has_occurrence_event());
  if (event_record.metric->metric_dimensions_size() != 1) {
    LOG(ERROR) << "The Metric "
               << project_context()->FullMetricName(*event_record.metric)
               << " has the wrong number of metric_dimensions. A metric of "
                  "type EVENT_OCCURRED must have exactly one metric_dimension.";
    return kInvalidConfig;
  }

  const auto& occurrence_event = event_record.event->occurrence_event();
  if (occurrence_event.event_code() >
      event_record.metric->metric_dimensions(0).max_event_code()) {
    LOG(ERROR) << "The event_code " << occurrence_event.event_code()
               << " exceeds "
               << event_record.metric->metric_dimensions(0).max_event_code()
               << ", the max_event_code for Metric "
               << project_context()->FullMetricName(*event_record.metric)
               << ".";
    return kInvalidArguments;
  }
  return kOK;
}

Encoder::Result OccurrenceEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
  TRACE_DURATION("cobalt_core",
                 "OccurrenceEventLogger::MaybeEncodeImmediateObservation");
  const MetricDefinition& metric = *(event_record->metric);
  const Event& event = *(event_record->event);
  CHECK(event.has_occurrence_event());
  const auto& occurrence_event = event.occurrence_event();
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::SIMPLE_OCCURRENCE_COUNT: {
      return encoder()->EncodeBasicRapporObservation(
          project_context()->RefMetric(&metric), &report, event.day_index(),
          occurrence_event.event_code(),
          RapporConfigHelper::BasicRapporNumCategories(metric));
    }
      // Report type UNIQUE_N_DAY_ACTIVES is valid but should not result in
      // generation of an immediate observation.
    case ReportDefinition::UNIQUE_N_DAY_ACTIVES: {
      Encoder::Result result;
      result.status = kOK;
      result.observation = nullptr;
      result.metadata = nullptr;
      return result;
    }

    default:
      return BadReportType(metric, report);
  }
}

Status OccurrenceEventLogger::MaybeUpdateLocalAggregation(
    const ReportDefinition& report, EventRecord* event_record) {
  switch (report.report_type()) {
    case ReportDefinition::UNIQUE_N_DAY_ACTIVES: {
      return event_aggregator()->LogUniqueActivesEvent(report.id(),
                                                       event_record);
    }
    default:
      return kOK;
  }
}

///////////// CountEventLogger method implementations //////////////////////////

Status CountEventLogger::ValidateEvent(const EventRecord& event_record) {
  CHECK(event_record.metric);
  return ValidateEventCodes(*event_record.metric,
                            event_record.event->count_event().event_code());
}

Encoder::Result CountEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
  TRACE_DURATION("cobalt_core",
                 "CountEventLogger::MaybeEncodeImmediateObservation");
  const MetricDefinition& metric = *(event_record->metric);
  const Event& event = *(event_record->event);
  CHECK(event.has_count_event());
  auto* count_event = event_record->event->mutable_count_event();
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::EVENT_COMPONENT_OCCURRENCE_COUNT:
    case ReportDefinition::INT_RANGE_HISTOGRAM:
    case ReportDefinition::NUMERIC_AGGREGATION: {
      std::string component;
      if (may_invalidate) {
        component = std::move(*count_event->mutable_component());
      } else {
        component = count_event->component();
      }
      return encoder()->EncodeIntegerEventObservation(
          project_context()->RefMetric(&metric), &report, event.day_index(),
          count_event->event_code(), std::move(component),
          count_event->count());
    }
    // Report type PER_DEVICE_NUMERIC_STATS is valid but should not result in
    // generation of an immediate observation.
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
      Encoder::Result result;
      result.status = kOK;
      result.observation = nullptr;
      result.metadata = nullptr;
      return result;
    }

    default:
      return BadReportType(metric, report);
  }
}

Status CountEventLogger::MaybeUpdateLocalAggregation(
    const ReportDefinition& report, EventRecord* event_record) {
  switch (report.report_type()) {
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
      return event_aggregator()->LogCountEvent(report.id(), event_record);
    }
    default:
      return kOK;
  }
}

/////////////// IntegerPerformanceEventLogger method implementations
//////////////

Encoder::Result IntegerPerformanceEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
  TRACE_DURATION(
      "cobalt_core",
      "IntegerPerformanceEventLogger::MaybeEncodeImmediateObservation");
  const MetricDefinition& metric = *(event_record->metric);
  const Event& event = *(event_record->event);
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::NUMERIC_AGGREGATION:
    case ReportDefinition::NUMERIC_PERF_RAW_DUMP:
    case ReportDefinition::INT_RANGE_HISTOGRAM: {
      return encoder()->EncodeIntegerEventObservation(
          project_context()->RefMetric(&metric), &report, event.day_index(),
          EventCodes(event), Component(event), IntValue(event));
      break;
    }
    // Report type PER_DEVICE_NUMERIC_STATS is valid but should not result in
    // generation of an immediate observation.
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
      Encoder::Result result;
      result.status = kOK;
      result.observation = nullptr;
      result.metadata = nullptr;
      return result;
    }
    default:
      return BadReportType(metric, report);
  }
}

////////////// ElapsedTimeEventLogger method implementations
//////////////////////

const RepeatedField<uint32_t>& ElapsedTimeEventLogger::EventCodes(
    const Event& event) {
  CHECK(event.has_elapsed_time_event());
  return event.elapsed_time_event().event_code();
}

std::string ElapsedTimeEventLogger::Component(const Event& event) {
  CHECK(event.has_elapsed_time_event());
  return event.elapsed_time_event().component();
}

int64_t ElapsedTimeEventLogger::IntValue(const Event& event) {
  CHECK(event.has_elapsed_time_event());
  return event.elapsed_time_event().elapsed_micros();
}

Status ElapsedTimeEventLogger::ValidateEvent(const EventRecord& event_record) {
  CHECK(event_record.metric);
  return ValidateEventCodes(
      *event_record.metric,
      event_record.event->elapsed_time_event().event_code());
}

Status ElapsedTimeEventLogger::MaybeUpdateLocalAggregation(
    const ReportDefinition& report, EventRecord* event_record) {
  switch (report.report_type()) {
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
      return event_aggregator()->LogElapsedTimeEvent(report.id(), event_record);
    }
    default:
      return kOK;
  }
}

////////////// FrameRateEventLogger method implementations /////////////////

const RepeatedField<uint32_t>& FrameRateEventLogger::EventCodes(
    const Event& event) {
  CHECK(event.has_frame_rate_event());
  return event.frame_rate_event().event_code();
}

std::string FrameRateEventLogger::Component(const Event& event) {
  CHECK(event.has_frame_rate_event());
  return event.frame_rate_event().component();
}

int64_t FrameRateEventLogger::IntValue(const Event& event) {
  CHECK(event.has_frame_rate_event());
  return event.frame_rate_event().frames_per_1000_seconds();
}

Status FrameRateEventLogger::ValidateEvent(const EventRecord& event_record) {
  CHECK(event_record.metric);
  return ValidateEventCodes(
      *event_record.metric,
      event_record.event->frame_rate_event().event_code());
}

Status FrameRateEventLogger::MaybeUpdateLocalAggregation(
    const ReportDefinition& report, EventRecord* event_record) {
  switch (report.report_type()) {
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
      return event_aggregator()->LogFrameRateEvent(report.id(), event_record);
    }
    default:
      return kOK;
  }
}

////////////// MemoryUsageEventLogger method implementations
//////////////////////
const RepeatedField<uint32_t>& MemoryUsageEventLogger::EventCodes(
    const Event& event) {
  CHECK(event.has_memory_usage_event());
  return event.memory_usage_event().event_code();
}

std::string MemoryUsageEventLogger::Component(const Event& event) {
  CHECK(event.has_memory_usage_event());
  return event.memory_usage_event().component();
}

int64_t MemoryUsageEventLogger::IntValue(const Event& event) {
  CHECK(event.has_memory_usage_event());
  return event.memory_usage_event().bytes();
}

Status MemoryUsageEventLogger::ValidateEvent(const EventRecord& event_record) {
  CHECK(event_record.metric);
  return ValidateEventCodes(
      *event_record.metric,
      event_record.event->memory_usage_event().event_code());
}

Status MemoryUsageEventLogger::MaybeUpdateLocalAggregation(
    const ReportDefinition& report, EventRecord* event_record) {
  switch (report.report_type()) {
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
      return event_aggregator()->LogMemoryUsageEvent(report.id(), event_record);
    }
    default:
      return kOK;
  }
}

/////////////// IntHistogramEventLogger method implementations
////////////////////

Status IntHistogramEventLogger::ValidateEvent(const EventRecord& event_record) {
  CHECK(event_record.event->has_int_histogram_event());
  const auto& int_histogram_event = event_record.event->int_histogram_event();
  CHECK(event_record.metric);
  const auto& metric = *(event_record.metric);

  auto status = ValidateEventCodes(metric, int_histogram_event.event_code());
  if (status != kOK) {
    return status;
  }

  if (!metric.has_int_buckets()) {
    LOG(ERROR) << "Invalid Cobalt config: Metric "
               << project_context()->FullMetricName(*event_record.metric)
               << " does not have an |int_buckets| field set.";
    return kInvalidConfig;
  }
  const auto& int_buckets = metric.int_buckets();
  uint32_t num_valid_buckets;
  switch (int_buckets.buckets_case()) {
    case IntegerBuckets::kExponential:
      num_valid_buckets = int_buckets.exponential().num_buckets();
      break;
    case IntegerBuckets::kLinear:
      num_valid_buckets = int_buckets.linear().num_buckets();
      break;
    case IntegerBuckets::BUCKETS_NOT_SET:
      LOG(ERROR) << "Invalid Cobalt config: Metric "
                 << project_context()->FullMetricName(*event_record.metric)
                 << " has an invalid |int_buckets| field. Either exponential "
                    "or linear buckets must be specified.";
      return kInvalidConfig;
  }

  // In addition to the specified num_buckets, there are the underflow and
  // overflow buckets.
  num_valid_buckets += 2;

  size_t num_provided_buckets = int_histogram_event.buckets_size();
  for (auto i = 0u; i < num_provided_buckets; i++) {
    if (int_histogram_event.buckets(i).index() >= num_valid_buckets) {
      LOG(ERROR) << "The provided histogram is invalid. The index value of "
                 << int_histogram_event.buckets(i).index() << " in position "
                 << i << " is out of bounds for Metric "
                 << project_context()->FullMetricName(*event_record.metric)
                 << ".";
      return kInvalidArguments;
    }
  }

  return kOK;
}

Encoder::Result IntHistogramEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
  TRACE_DURATION("cobalt_core",
                 "IntHistogramEventLogger::MaybeEncodeImmediateObservation");
  const MetricDefinition& metric = *(event_record->metric);
  const Event& event = *(event_record->event);
  CHECK(event.has_int_histogram_event());
  auto* int_histogram_event =
      event_record->event->mutable_int_histogram_event();
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::INT_RANGE_HISTOGRAM: {
      HistogramPtr histogram =
          std::make_unique<RepeatedPtrField<HistogramBucket>>();
      if (may_invalidate) {
        // We move the buckets out of |int_histogram_event| thereby
        // invalidating that variable.
        histogram->Swap(int_histogram_event->mutable_buckets());
      } else {
        histogram->CopyFrom(int_histogram_event->buckets());
      }
      return encoder()->EncodeHistogramObservation(
          project_context()->RefMetric(&metric), &report, event.day_index(),
          int_histogram_event->event_code(), int_histogram_event->component(),
          std::move(histogram));
    }

    default:
      return BadReportType(metric, report);
  }
}

/////////////// StringUsedEventLogger method implementations
//////////////////////

Encoder::Result StringUsedEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
  TRACE_DURATION("cobalt_core",
                 "StringUsedEventLogger::MaybeEncodeImmediateObservation");
  const MetricDefinition& metric = *(event_record->metric);
  const Event& event = *(event_record->event);
  CHECK(event.has_string_used_event());
  const auto& string_used_event = event_record->event->string_used_event();
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::HIGH_FREQUENCY_STRING_COUNTS: {
      return encoder()->EncodeRapporObservation(
          project_context()->RefMetric(&metric), &report, event.day_index(),
          string_used_event.str());
    }
    case ReportDefinition::STRING_COUNTS_WITH_THRESHOLD: {
      return encoder()->EncodeForculusObservation(
          project_context()->RefMetric(&metric), &report, event.day_index(),
          string_used_event.str());
    }

    default:
      return BadReportType(metric, report);
  }
}

/////////////// CustomEventLogger method implementations
//////////////////////////

Status CustomEventLogger::ValidateEvent(const EventRecord& event_record) {
  // TODO(ninai) Add proto validation.
  return kOK;
}

Encoder::Result CustomEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
  TRACE_DURATION("cobalt_core",
                 "CustomEventLogger::MaybeEncodeImmediateObservation");
  const MetricDefinition& metric = *(event_record->metric);
  const Event& event = *(event_record->event);
  CHECK(event.has_custom_event());
  auto* custom_event = event_record->event->mutable_custom_event();
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::CUSTOM_RAW_DUMP: {
      EventValuesPtr event_values = std::make_unique<
          google::protobuf::Map<std::string, CustomDimensionValue>>();
      if (may_invalidate) {
        // We move the contents out of |custom_event| thereby invalidating
        // that variable.
        event_values->swap(*(custom_event->mutable_values()));
      } else {
        event_values = std::make_unique<
            google::protobuf::Map<std::string, CustomDimensionValue>>(
            custom_event->values());
      }
      return encoder()->EncodeCustomObservation(
          project_context()->RefMetric(&metric), &report, event.day_index(),
          std::move(event_values));
    }

    default:
      return BadReportType(metric, report);
  }
}

}  // namespace logger
}  // namespace cobalt

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/logger.h"

#include <memory>
#include <string>

#include "./event.pb.h"
#include "./logging.h"
#include "./observation2.pb.h"
#include "algorithms/rappor/rappor_config_helper.h"
#include "config/encodings.pb.h"
#include "config/id.h"
#include "config/metric_definition.pb.h"
#include "config/report_definition.pb.h"
#include "logger/project_context.h"
#include "util/clock.h"
#include "util/datetime_util.h"

namespace cobalt {
namespace logger {

using ::cobalt::rappor::RapporConfigHelper;
using ::cobalt::util::ClockInterface;
using ::cobalt::util::SystemClock;
using ::cobalt::util::TimeToDayIndex;
using ::google::protobuf::RepeatedPtrField;

namespace {

struct EventRecord {
  const MetricDefinition* metric;
  std::unique_ptr<Event> event = std::make_unique<Event>();
};

}  // namespace

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
  const ProjectContext* project_context() { return logger_->project_context_; }
  Encoder::Result BadReportType(const MetricDefinition& metric,
                                const ReportDefinition& report);

 private:
  // Sets up |event_record| with initial data.
  Status InitializeEvent(uint32_t metric_id,
                         MetricDefinition::MetricType expected_type,
                         EventRecord* event_record);

  virtual Status ValidateEvent(const EventRecord& event_record);

  // Given an EventRecord and a ReportDefinition, determines whether or not
  // the Event should be used to update a local aggregation and if so passes
  // the Event to the Local Aggregator.
  Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
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
};

// Implementation of EventLogger for metrics of type EVENT_COUNT.
class CountEventLogger : public EventLogger {
 public:
  explicit CountEventLogger(Logger* logger) : EventLogger(logger) {}
  virtual ~CountEventLogger() = default;

 private:
  Encoder::Result MaybeEncodeImmediateObservation(
      const ReportDefinition& report, bool may_invalidate,
      EventRecord* event_record) override;
};

// Implementation of EventLogger for all of the numerical performance metric
// types. This is an abstract class. There are subclasses below for each metric
// type.
class IntegerPerformanceEventLogger : public EventLogger {
 protected:
  explicit IntegerPerformanceEventLogger(Logger* logger)
      : EventLogger(logger) {}
  virtual ~IntegerPerformanceEventLogger() = default;

 private:
  Encoder::Result MaybeEncodeImmediateObservation(
      const ReportDefinition& report, bool may_invalidate,
      EventRecord* event_record) override;
  virtual uint32_t EventCode(const Event& event) = 0;
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
  uint32_t EventCode(const Event& event) override;
  std::string Component(const Event& event) override;
  int64_t IntValue(const Event& event) override;
};

// Implementation of EventLogger for metrics of type FRAME_RATE.
class FrameRateEventLogger : public IntegerPerformanceEventLogger {
 public:
  explicit FrameRateEventLogger(Logger* logger)
      : IntegerPerformanceEventLogger(logger) {}
  virtual ~FrameRateEventLogger() = default;

 private:
  uint32_t EventCode(const Event& event) override;
  std::string Component(const Event& event) override;
  int64_t IntValue(const Event& event) override;
};

// Implementation of EventLogger for metrics of type MEMORY_USAGE.
class MemoryUsageEventLogger : public IntegerPerformanceEventLogger {
 public:
  explicit MemoryUsageEventLogger(Logger* logger)
      : IntegerPerformanceEventLogger(logger) {}
  virtual ~MemoryUsageEventLogger() = default;

 private:
  uint32_t EventCode(const Event& event) override;
  std::string Component(const Event& event) override;
  int64_t IntValue(const Event& event) override;
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

//////////////////// Logger method implementations ////////////////////////

Logger::Logger(const Encoder* encoder, ObservationWriter* observation_writer,
               const ProjectContext* project, LoggerInterface* internal_logger)
    : encoder_(encoder),
      observation_writer_(observation_writer),
      project_context_(project),
      clock_(new SystemClock()) {
  CHECK(project);

  if (internal_logger) {
    internal_metrics_.reset(new InternalMetricsImpl(internal_logger));
  } else {
    // We were not provided with a metrics logger. We must create one.
    internal_metrics_.reset(new NoOpInternalMetrics());
  }
}

Status Logger::LogEvent(uint32_t metric_id, uint32_t event_code) {
  EventRecord event_record;
  internal_metrics_->LoggerCalled(LoggerCallsMadeEventCode::LogEvent);
  auto* occurrence_event = event_record.event->mutable_occurrence_event();
  occurrence_event->set_event_code(event_code);
  auto event_logger = std::make_unique<OccurrenceEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::EVENT_OCCURRED,
                           &event_record);
}

Status Logger::LogEventCount(uint32_t metric_id, uint32_t event_code,
                             const std::string& component,
                             int64_t period_duration_micros, uint32_t count) {
  internal_metrics_->LoggerCalled(LoggerCallsMadeEventCode::LogEventCount);
  EventRecord event_record;
  auto* count_event = event_record.event->mutable_count_event();
  count_event->set_event_code(event_code);
  count_event->set_component(component);
  count_event->set_period_duration_micros(period_duration_micros);
  count_event->set_count(count);
  auto event_logger = std::make_unique<CountEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::EVENT_COUNT,
                           &event_record);
}

Status Logger::LogElapsedTime(uint32_t metric_id, uint32_t event_code,
                              const std::string& component,
                              int64_t elapsed_micros) {
  internal_metrics_->LoggerCalled(LoggerCallsMadeEventCode::LogElapsedTime);
  EventRecord event_record;
  auto* elapsed_time_event = event_record.event->mutable_elapsed_time_event();
  elapsed_time_event->set_event_code(event_code);
  elapsed_time_event->set_component(component);
  elapsed_time_event->set_elapsed_micros(elapsed_micros);
  auto event_logger = std::make_unique<ElapsedTimeEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::ELAPSED_TIME,
                           &event_record);
}

Status Logger::LogFrameRate(uint32_t metric_id, uint32_t event_code,
                            const std::string& component, float fps) {
  internal_metrics_->LoggerCalled(LoggerCallsMadeEventCode::LogFrameRate);
  EventRecord event_record;
  auto* frame_rate_event = event_record.event->mutable_frame_rate_event();
  frame_rate_event->set_event_code(event_code);
  frame_rate_event->set_component(component);
  frame_rate_event->set_frames_per_1000_seconds(std::round(fps * 1000.0));
  auto event_logger = std::make_unique<FrameRateEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::FRAME_RATE,
                           &event_record);
}

Status Logger::LogMemoryUsage(uint32_t metric_id, uint32_t event_code,
                              const std::string& component, int64_t bytes) {
  internal_metrics_->LoggerCalled(LoggerCallsMadeEventCode::LogMemoryUsage);
  EventRecord event_record;
  auto* memory_usage_event = event_record.event->mutable_memory_usage_event();
  memory_usage_event->set_event_code(event_code);
  memory_usage_event->set_component(component);
  memory_usage_event->set_bytes(bytes);
  auto event_logger = std::make_unique<MemoryUsageEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::MEMORY_USAGE,
                           &event_record);
}

Status Logger::LogIntHistogram(uint32_t metric_id, uint32_t event_code,
                               const std::string& component,
                               HistogramPtr histogram) {
  internal_metrics_->LoggerCalled(LoggerCallsMadeEventCode::LogIntHistogram);
  EventRecord event_record;
  auto* int_histogram_event = event_record.event->mutable_int_histogram_event();
  int_histogram_event->set_event_code(event_code);
  int_histogram_event->set_component(component);
  int_histogram_event->mutable_buckets()->Swap(histogram.get());
  auto event_logger = std::make_unique<IntHistogramEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::INT_HISTOGRAM,
                           &event_record);
}

Status Logger::LogString(uint32_t metric_id, const std::string& str) {
  internal_metrics_->LoggerCalled(LoggerCallsMadeEventCode::LogString);
  EventRecord event_record;
  auto* string_used_event = event_record.event->mutable_string_used_event();
  string_used_event->set_str(str);
  auto event_logger = std::make_unique<StringUsedEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::STRING_USED,
                           &event_record);
}

Status Logger::LogCustomEvent(uint32_t metric_id, EventValuesPtr event_values) {
  internal_metrics_->LoggerCalled(LoggerCallsMadeEventCode::LogCustomEvent);
  EventRecord event_record;
  auto* custom_event = event_record.event->mutable_custom_event();
  custom_event->mutable_values()->swap(*event_values);
  auto event_logger = std::make_unique<CustomEventLogger>(this);
  return event_logger->Log(metric_id, MetricDefinition::CUSTOM, &event_record);
}

//////////////////// EventLogger method implementations ////////////////////////

Status EventLogger::Log(uint32_t metric_id,
                        MetricDefinition::MetricType expected_metric_type,
                        EventRecord* event_record) {
  // TODO(rudominer) Check the ReleaseStage of the Metric against the
  // ReleaseStage of the project.
  auto status = InitializeEvent(metric_id, expected_metric_type, event_record);
  if (status != kOK) {
    return status;
  }

  int num_reports = event_record->metric->reports_size();
  int report_index = 0;
  if (event_record->metric->reports_size() == 0) {
    VLOG(1) << "Warning: An event was logged for a metric with no reports "
               "defined. Metric ["
            << MetricDebugString(*event_record->metric) << "] in project "
            << project_context()->DebugString() << ".";
  }

  for (const auto& report : event_record->metric->reports()) {
    status = MaybeUpdateLocalAggregation(report, event_record);
    if (status != kOK) {
      return status;
    }

    // If we are processing the final report, then we set may_invalidate to
    // true in order to allow data to be moved out of |event_record| instead
    // of being copied. One example where this is useful is when creating
    // an immediate Observation of type Histogram. In that case we can move
    // the histogram from the Event to the Observation and avoid copying.
    // Since the |event_record| is invalidated, any other operation on the
    // |event_record| must be performed before this for loop.
    bool may_invalidate = ++report_index == num_reports;
    status =
        MaybeGenerateImmediateObservation(report, may_invalidate, event_record);
    if (status != kOK) {
      return status;
    }
  }

  return kOK;
}

Status EventLogger::InitializeEvent(uint32_t metric_id,
                                    MetricDefinition::MetricType expected_type,
                                    EventRecord* event_record) {
  event_record->metric = project_context()->GetMetric(metric_id);
  if (event_record->metric == nullptr) {
    LOG(ERROR) << "There is no metric with ID '" << metric_id << "' registered "
               << "in project '" << project_context()->DebugString() << "'.";
    return kInvalidArguments;
  }
  if (event_record->metric->metric_type() != expected_type) {
    LOG(ERROR) << "Metric '" << MetricDebugString(*event_record->metric)
               << "' in project '" << project_context()->DebugString()
               << "' is not of type " << expected_type << ".";
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

// The default implementation of MaybeUpdateLocalAggregation does nothing
// and returns OK.
Status EventLogger::MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                                EventRecord* event_record) {
  // TODO(pesk) Implement this method in subclasses of EventLoger
  // corresponding to Metric types for which there exist Report types for
  // which which you want to perform local aggregation.
  return kOK;
}

Status EventLogger::MaybeGenerateImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
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
  Encoder::Result result;
  result.status = kOK;
  result.observation = nullptr;
  return result;
}

Encoder::Result EventLogger::BadReportType(const MetricDefinition& metric,
                                           const ReportDefinition& report) {
  LOG(ERROR) << "Invalid Cobalt config: Report " << report.report_name()
             << " for metric " << MetricDebugString(metric) << " in project "
             << project_context()->DebugString()
             << " is not of an appropriate type for the metric type.";
  Encoder::Result encoder_result;
  encoder_result.status = kInvalidConfig;
  return encoder_result;
}

/////////////// OccurrenceEventLogger method implementations ///////////////////

Status OccurrenceEventLogger::ValidateEvent(const EventRecord& event_record) {
  CHECK(event_record.event->has_occurrence_event());
  const auto& occurrence_event = event_record.event->occurrence_event();
  if (occurrence_event.event_code() > event_record.metric->max_event_code()) {
    LOG(ERROR) << "The event_code " << occurrence_event.event_code()
               << " exceeds " << event_record.metric->max_event_code()
               << ", the max_event_code for Metric "
               << MetricDebugString(*event_record.metric) << " in project "
               << project_context()->DebugString() << ".";
    return kInvalidArguments;
  }
  return kOK;
}

Encoder::Result OccurrenceEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
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

    default:
      return BadReportType(metric, report);
  }
}

/////////////// CountEventLogger method implementations ////////////////////////

Encoder::Result CountEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
  const MetricDefinition& metric = *(event_record->metric);
  const Event& event = *(event_record->event);
  CHECK(event.has_count_event());
  const auto& count_event = event.count_event();
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::EVENT_COMPONENT_OCCURRENCE_COUNT: {
      return encoder()->EncodeIntegerEventObservation(
          project_context()->RefMetric(&metric), &report, event.day_index(),
          count_event.event_code(), count_event.component(),
          count_event.count());
    }

    default:
      return BadReportType(metric, report);
  }
}

/////////////// IntegerPerformanceEventLogger method implementations ///////////

Encoder::Result IntegerPerformanceEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
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
          EventCode(event), Component(event), IntValue(event));
      break;
    }

    default:
      return BadReportType(metric, report);
  }
}

////////////// ElapsedTimeEventLogger method implementations ///////////////////

uint32_t ElapsedTimeEventLogger::EventCode(const Event& event) {
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

////////////// FrameRateEventLogger method implementations /////////////////

uint32_t FrameRateEventLogger::EventCode(const Event& event) {
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

////////////// MemoryUsageEventLogger method implementations ///////////////////
uint32_t MemoryUsageEventLogger::EventCode(const Event& event) {
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

/////////////// IntHistogramEventLogger method implementations /////////////////

Status IntHistogramEventLogger::ValidateEvent(const EventRecord& event_record) {
  CHECK(event_record.event->has_int_histogram_event());
  const auto& int_histogram_event = event_record.event->int_histogram_event();
  const auto& metric = *(event_record.metric);
  if (!metric.has_int_buckets()) {
    LOG(ERROR) << "Invalid Cobalt config: Metric " << MetricDebugString(metric)
               << " in project " << project_context()->DebugString()
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
                 << MetricDebugString(metric) << " in project "
                 << project_context()->DebugString()
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
                 << MetricDebugString(*event_record.metric) << " in project "
                 << project_context()->DebugString() << ".";
      return kInvalidArguments;
    }
  }

  return kOK;
}

Encoder::Result IntHistogramEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
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

/////////////// StringUsedEventLogger method implementations ///////////////////
Encoder::Result StringUsedEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
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

/////////////// CustomEventLogger method implementations ///////////////////////
Status CustomEventLogger::ValidateEvent(const EventRecord& event_record) {
  // TODO(ninai) Add proto validation.
  return kOK;
}

Encoder::Result CustomEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate,
    EventRecord* event_record) {
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

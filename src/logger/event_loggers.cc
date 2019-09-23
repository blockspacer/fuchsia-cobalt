// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logger/event_loggers.h"

#include <memory>
#include <string>

#include "src/algorithms/rappor/rappor_config_helper.h"
#include "src/lib/util/datetime_util.h"
#include "src/logger/event_record.h"
#include "src/logging.h"
#include "src/pb/event.pb.h"
#include "src/pb/observation2.pb.h"
#include "src/registry/encodings.pb.h"
#include "src/registry/id.h"
#include "src/registry/metric_definition.pb.h"
#include "src/registry/report_definition.pb.h"
#include "src/tracing.h"

namespace cobalt::logger::internal {

using ::cobalt::rappor::RapporConfigHelper;
using ::cobalt::util::TimeToDayIndex;
using ::cobalt::util::TimeToHourIndex;
using ::google::protobuf::RepeatedField;
using ::google::protobuf::RepeatedPtrField;

constexpr char TRACE_PREFIX[] = "[COBALT_EVENT_TRACE] ";

std::unique_ptr<EventLogger> EventLogger::Create(MetricDefinition::MetricType metric_type,
                                                 const Encoder* encoder,
                                                 EventAggregator* event_aggregator,
                                                 const ObservationWriter* observation_writer,
                                                 const encoder::SystemDataInterface* system_data) {
  switch (metric_type) {
    case MetricDefinition::EVENT_OCCURRED: {
      return std::make_unique<internal::OccurrenceEventLogger>(encoder, event_aggregator,
                                                               observation_writer, system_data);
    }
    case MetricDefinition::EVENT_COUNT: {
      return std::make_unique<internal::CountEventLogger>(encoder, event_aggregator,
                                                          observation_writer, system_data);
    }
    case MetricDefinition::ELAPSED_TIME: {
      return std::make_unique<internal::ElapsedTimeEventLogger>(encoder, event_aggregator,
                                                                observation_writer, system_data);
    }
    case MetricDefinition::FRAME_RATE: {
      return std::make_unique<internal::FrameRateEventLogger>(encoder, event_aggregator,
                                                              observation_writer, system_data);
    }
    case MetricDefinition::MEMORY_USAGE: {
      return std::make_unique<internal::MemoryUsageEventLogger>(encoder, event_aggregator,
                                                                observation_writer, system_data);
    }
    case MetricDefinition::INT_HISTOGRAM: {
      return std::make_unique<internal::IntHistogramEventLogger>(encoder, event_aggregator,
                                                                 observation_writer, system_data);
    }
    case MetricDefinition::STRING_USED: {
      return std::make_unique<internal::StringUsedEventLogger>(encoder, event_aggregator,
                                                               observation_writer, system_data);
    }
    case MetricDefinition::CUSTOM: {
      return std::make_unique<internal::CustomEventLogger>(encoder, event_aggregator,
                                                           observation_writer, system_data);
    }
    default: {
      LOG(ERROR) << "Failed to process a metric type of " << metric_type;
      return nullptr;
    }
  }
}

std::string EventLogger::TraceEvent(const EventRecord& event_record) {
  if (!event_record.metric()->meta_data().also_log_locally()) {
    return "";
  }

  auto event = event_record.event();

  std::stringstream ss;
  ss << "Day index: " << event->day_index() << std::endl;
  if (event->has_occurrence_event()) {
    const auto& e = event->occurrence_event();
    ss << "OccurrenceEvent: " << e.event_code() << std::endl;
  } else if (event->has_count_event()) {
    const auto& e = event->count_event();
    ss << "CountEvent:" << std::endl;
    ss << "EventCodes:";
    for (const auto& code : e.event_code()) {
      ss << " " << code;
    }
    ss << ", Component: " << e.component()
       << ", PeriodDurationMicros: " << e.period_duration_micros() << ", Count: " << e.count()
       << std::endl;
  } else if (event->has_elapsed_time_event()) {
    const auto& e = event->elapsed_time_event();
    ss << "ElapsedTimeEvent:" << std::endl;
    ss << "EventCodes:";
    for (const auto& code : e.event_code()) {
      ss << " " << code;
    }
    ss << ", Component: " << e.component() << ", ElapsedMicros: " << e.elapsed_micros()
       << std::endl;
  } else if (event->has_frame_rate_event()) {
    const auto& e = event->frame_rate_event();
    ss << "FrameRateEvent:" << std::endl;
    ss << "EventCodes:";
    for (const auto& code : e.event_code()) {
      ss << " " << code;
    }
    ss << ", Component: " << e.component()
       << ", FramesPer1000Seconds: " << e.frames_per_1000_seconds() << std::endl;
  } else if (event->has_memory_usage_event()) {
    const auto& e = event->memory_usage_event();
    ss << "MemoryUsageEvent:" << std::endl;
    ss << "EventCodes:";
    for (const auto& code : e.event_code()) {
      ss << " " << code;
    }
    ss << ", Component: " << e.component() << ", Bytes: " << e.bytes() << std::endl;
  } else if (event->has_int_histogram_event()) {
    const auto& e = event->int_histogram_event();
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
    const auto& e = event->string_used_event();
    ss << "StringUsedEvent: " << e.str() << std::endl;
  } else if (event->has_custom_event()) {
    const auto& e = event->custom_event();
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

void EventLogger::TraceLogFailure(const Status& status, const EventRecord& event_record,
                                  const std::string& trace, const ReportDefinition& report) {
  if (!event_record.metric()->meta_data().also_log_locally()) {
    return;
  }

  LOG(INFO) << TRACE_PREFIX << "("
            << event_record.project_context()->RefMetric(event_record.metric()).FullyQualifiedName()
            << "): Error (" << status << ")" << std::endl
            << trace << "While trying to send report: " << report.report_name() << std::endl;
}

void EventLogger::TraceLogSuccess(const EventRecord& event_record, const std::string& trace) {
  if (!event_record.metric()->meta_data().also_log_locally()) {
    return;
  }

  LOG(INFO) << TRACE_PREFIX << "("
            << event_record.project_context()->RefMetric(event_record.metric()).FullyQualifiedName()
            << "):" << std::endl
            << trace;
}

Status EventLogger::Log(std::unique_ptr<EventRecord> event_record,
                        const std::chrono::system_clock::time_point& event_timestamp) {
  TRACE_DURATION("cobalt_core", "EventLogger::Log", "metric_id", event_record->metric()->id());

  FinalizeEvent(event_record.get(), event_timestamp);

  if (system_data_) {
    if (system_data_->release_stage() > event_record->metric()->meta_data().max_release_stage()) {
      // Quietly ignore this metric.
      LOG_FIRST_N(INFO, 10) << "Not logging metric `"
                            << event_record->project_context()->FullMetricName(
                                   *event_record->metric())
                            << "` because its max_release_stage ("
                            << event_record->metric()->meta_data().max_release_stage()
                            << ") is lower than the device's current release_stage: "
                            << system_data_->release_stage();
      return kOK;
    }
  }

  int num_reports = event_record->metric()->reports_size();
  int report_index = 0;
  if (event_record->metric()->reports_size() == 0) {
    VLOG(1) << "Warning: An event was logged for a metric with no reports "
               "defined: "
            << event_record->project_context()->FullMetricName(*event_record->metric());
  }

  // Store the trace before attempting to log the event. This way, if parts of
  // the event are moved out of the object, the resulting trace will still have
  // useful information.
  auto trace = TraceEvent(*event_record);

  for (const auto& report : event_record->metric()->reports()) {
    auto status = MaybeUpdateLocalAggregation(report, *event_record);
    if (status != kOK) {
      TraceLogFailure(status, *event_record, trace, report);
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
    status = MaybeGenerateImmediateObservation(report, may_invalidate, event_record.get());
    if (status != kOK) {
      TraceLogFailure(status, *event_record, trace, report);
      return status;
    }
  }

  TraceLogSuccess(*event_record, trace);
  return kOK;
}

Status EventLogger::PrepareAndValidateEvent(uint32_t metric_id,
                                            MetricDefinition::MetricType expected_type,
                                            EventRecord* event_record) {
  if (event_record->metric() == nullptr) {
    LOG(ERROR) << "There is no metric with ID '" << metric_id << "' registered "
               << "in project '" << event_record->project_context()->FullyQualifiedName() << "'.";
    return kInvalidArguments;
  }
  if (event_record->metric()->metric_type() != expected_type) {
    LOG(ERROR) << "Metric "
               << event_record->project_context()->FullMetricName(*event_record->metric())
               << " is not of type " << expected_type << ".";
    return kInvalidArguments;
  }
  return ValidateEvent(*event_record);
}

void EventLogger::FinalizeEvent(EventRecord* event_record,
                                const std::chrono::system_clock::time_point& event_timestamp) {
  // Compute the day_index and hour index.
  auto now = std::chrono::system_clock::to_time_t(event_timestamp);
  event_record->event()->set_day_index(
      TimeToDayIndex(now, event_record->metric()->time_zone_policy()));
  event_record->event()->set_hour_index(
      TimeToHourIndex(now, event_record->metric()->time_zone_policy()));
}

Status EventLogger::ValidateEvent(const EventRecord& /*event_record*/) { return kOK; }

Status EventLogger::ValidateEventCodes(const MetricDefinition& metric,
                                       const RepeatedField<uint32_t>& event_codes,
                                       const std::string& full_metric_name) {
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
  if (metric.metric_dimensions_size() == 0 && event_codes.size() == 1 && event_codes.Get(0) == 0) {
    return kOK;
  }
  // When new dimensions are added to a metric, they can only be appended, not deleted or inserted.
  // Because of this, and because metric definitions may change before the matching code does, we
  // want to accept events where fewer than the expected number of event_codes have been provided.
  if (event_codes.size() > metric.metric_dimensions_size()) {
    LOG(ERROR) << "The number of event_codes given, " << event_codes.size()
               << ", is more than the number of metric_dimensions, "
               << metric.metric_dimensions_size() << ", for metric " << full_metric_name << ".";
    return kInvalidArguments;
  }
  for (int i = 0; i < event_codes.size(); i++) {
    const auto& dim = metric.metric_dimensions(i);
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
                   << ", exceeds the max_event_code for that dimension, " << dim.max_event_code()
                   << ", for metric " << full_metric_name;
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
                   << code << ", for metric " << full_metric_name;
        return kInvalidArguments;
      }
    }
  }
  return kOK;
}

// The default implementation of MaybeUpdateLocalAggregation does nothing
// and returns OK.
Status EventLogger::MaybeUpdateLocalAggregation(const ReportDefinition& /*report*/,
                                                const EventRecord& /*event_record*/) {
  return kOK;
}

Status EventLogger::MaybeGenerateImmediateObservation(const ReportDefinition& report,
                                                      bool may_invalidate,
                                                      EventRecord* event_record) {
  TRACE_DURATION("cobalt_core", "EventLogger::MaybeGenerateImmediateObservation");

  auto encoder_result = MaybeEncodeImmediateObservation(report, may_invalidate, event_record);
  if (encoder_result.status != kOK) {
    return encoder_result.status;
  }
  if (encoder_result.observation == nullptr) {
    return kOK;
  }
  return observation_writer_->WriteObservation(*encoder_result.observation,
                                               std::move(encoder_result.metadata));
}

// The default implementation of MaybeEncodeImmediateObservation does
// nothing and returns OK.
Encoder::Result EventLogger::MaybeEncodeImmediateObservation(const ReportDefinition& /*report*/,
                                                             bool /*may_invalidate*/,
                                                             EventRecord* /*event_record*/) {
  TRACE_DURATION("cobalt_core", "EventLogger::MaybeEncodeImmediateObservation");
  Encoder::Result result;
  result.status = kOK;
  result.observation = nullptr;
  return result;
}

Encoder::Result EventLogger::BadReportType(const std::string& full_metric_name,
                                           const ReportDefinition& report) {
  LOG(ERROR) << "Invalid Cobalt config: Report " << report.report_name() << " for metric "
             << full_metric_name << " is not of an appropriate type for the metric type.";
  Encoder::Result encoder_result;
  encoder_result.status = kInvalidConfig;
  return encoder_result;
}

/////////////// OccurrenceEventLogger method implementations ///////////////////

Status OccurrenceEventLogger::ValidateEvent(const EventRecord& event_record) {
  CHECK(event_record.event()->has_occurrence_event());
  if (event_record.metric()->metric_dimensions_size() != 1) {
    LOG(ERROR) << "The Metric "
               << event_record.project_context()->FullMetricName(*event_record.metric())
               << " has the wrong number of metric_dimensions. A metric of "
                  "type EVENT_OCCURRED must have exactly one metric_dimension.";
    return kInvalidConfig;
  }

  const auto& occurrence_event = event_record.event()->occurrence_event();
  if (occurrence_event.event_code() >
      event_record.metric()->metric_dimensions(0).max_event_code()) {
    LOG(ERROR) << "The event_code " << occurrence_event.event_code() << " exceeds "
               << event_record.metric()->metric_dimensions(0).max_event_code()
               << ", the max_event_code for Metric "
               << event_record.project_context()->FullMetricName(*event_record.metric()) << ".";
    return kInvalidArguments;
  }
  return kOK;
}

Encoder::Result OccurrenceEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool /*may_invalidate*/, EventRecord* event_record) {
  TRACE_DURATION("cobalt_core", "OccurrenceEventLogger::MaybeEncodeImmediateObservation");
  const MetricDefinition& metric = *(event_record->metric());
  const Event& event = *(event_record->event());
  CHECK(event.has_occurrence_event());
  const auto& occurrence_event = event.occurrence_event();
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::SIMPLE_OCCURRENCE_COUNT: {
      return encoder()->EncodeBasicRapporObservation(
          event_record->project_context()->RefMetric(&metric), &report, event.day_index(),
          occurrence_event.event_code(), RapporConfigHelper::BasicRapporNumCategories(metric));
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
      return BadReportType(event_record->project_context()->FullMetricName(metric), report);
  }
}

Status OccurrenceEventLogger::MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                                          const EventRecord& event_record) {
  switch (report.report_type()) {
    case ReportDefinition::UNIQUE_N_DAY_ACTIVES: {
      return event_aggregator()->LogUniqueActivesEvent(report.id(), event_record);
    }
    default:
      return kOK;
  }
}

///////////// CountEventLogger method implementations //////////////////////////

Status CountEventLogger::ValidateEvent(const EventRecord& event_record) {
  CHECK(event_record.metric());
  const auto& metric = *(event_record.metric());
  return ValidateEventCodes(metric, event_record.event()->count_event().event_code(),
                            event_record.project_context()->FullMetricName(metric));
}

Encoder::Result CountEventLogger::MaybeEncodeImmediateObservation(const ReportDefinition& report,
                                                                  bool /*may_invalidate*/,
                                                                  EventRecord* event_record) {
  TRACE_DURATION("cobalt_core", "CountEventLogger::MaybeEncodeImmediateObservation");
  const MetricDefinition& metric = *(event_record->metric());
  const Event& event = *(event_record->event());
  CHECK(event.has_count_event());
  auto* count_event = event_record->event()->mutable_count_event();
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::EVENT_COMPONENT_OCCURRENCE_COUNT:
    case ReportDefinition::INT_RANGE_HISTOGRAM:
    case ReportDefinition::NUMERIC_AGGREGATION: {
      return encoder()->EncodeIntegerEventObservation(
          event_record->project_context()->RefMetric(&metric), &report, event.day_index(),
          count_event->event_code(), count_event->component(), count_event->count());
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
      return BadReportType(event_record->project_context()->FullMetricName(metric), report);
  }
}

Status CountEventLogger::MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                                     const EventRecord& event_record) {
  switch (report.report_type()) {
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
      return event_aggregator()->LogCountEvent(report.id(), event_record);
    }
    default:
      return kOK;
  }
}

///////////// IntegerPerformanceEventLogger method implementations /////////////

Encoder::Result IntegerPerformanceEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool /*may_invalidate*/, EventRecord* event_record) {
  TRACE_DURATION("cobalt_core", "IntegerPerformanceEventLogger::MaybeEncodeImmediateObservation");
  const MetricDefinition& metric = *(event_record->metric());
  const Event& event = *(event_record->event());
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::NUMERIC_AGGREGATION:
    case ReportDefinition::NUMERIC_PERF_RAW_DUMP:
    case ReportDefinition::INT_RANGE_HISTOGRAM:
      return encoder()->EncodeIntegerEventObservation(
          event_record->project_context()->RefMetric(&metric), &report, event.day_index(),
          EventCodes(event), Component(event), IntValue(event));
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
      return BadReportType(event_record->project_context()->FullMetricName(metric), report);
  }
}

////////////// ElapsedTimeEventLogger method implementations ///////////////////

const RepeatedField<uint32_t>& ElapsedTimeEventLogger::EventCodes(const Event& event) {
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
  CHECK(event_record.metric());
  const auto& metric = *(event_record.metric());
  return ValidateEventCodes(metric, event_record.event()->elapsed_time_event().event_code(),
                            event_record.project_context()->FullMetricName(metric));
}

Status ElapsedTimeEventLogger::MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                                           const EventRecord& event_record) {
  switch (report.report_type()) {
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
      return event_aggregator()->LogElapsedTimeEvent(report.id(), event_record);
    }
    default:
      return kOK;
  }
}

//////////////// FrameRateEventLogger method implementations ///////////////////

const RepeatedField<uint32_t>& FrameRateEventLogger::EventCodes(const Event& event) {
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
  CHECK(event_record.metric());
  const auto& metric = *(event_record.metric());
  return ValidateEventCodes(metric, event_record.event()->frame_rate_event().event_code(),
                            event_record.project_context()->FullMetricName(metric));
}

Status FrameRateEventLogger::MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                                         const EventRecord& event_record) {
  switch (report.report_type()) {
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
      return event_aggregator()->LogFrameRateEvent(report.id(), event_record);
    }
    default:
      return kOK;
  }
}

////////////// MemoryUsageEventLogger method implementations ///////////////////
const RepeatedField<uint32_t>& MemoryUsageEventLogger::EventCodes(const Event& event) {
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
  CHECK(event_record.metric());
  const auto& metric = *(event_record.metric());
  return ValidateEventCodes(metric, event_record.event()->memory_usage_event().event_code(),
                            event_record.project_context()->FullMetricName(metric));
}

Status MemoryUsageEventLogger::MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                                           const EventRecord& event_record) {
  switch (report.report_type()) {
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
      return event_aggregator()->LogMemoryUsageEvent(report.id(), event_record);
    }
    default:
      return kOK;
  }
}

///////////// IntHistogramEventLogger method implementations ///////////////////

Status IntHistogramEventLogger::ValidateEvent(const EventRecord& event_record) {
  CHECK(event_record.event()->has_int_histogram_event());
  const auto& int_histogram_event = event_record.event()->int_histogram_event();
  CHECK(event_record.metric());
  const auto& metric = *(event_record.metric());

  auto status = ValidateEventCodes(metric, int_histogram_event.event_code(),
                                   event_record.project_context()->FullMetricName(metric));
  if (status != kOK) {
    return status;
  }

  if (!metric.has_int_buckets()) {
    LOG(ERROR) << "Invalid Cobalt config: Metric "
               << event_record.project_context()->FullMetricName(metric)
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
                 << event_record.project_context()->FullMetricName(metric)
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
                 << int_histogram_event.buckets(i).index() << " in position " << i
                 << " is out of bounds for Metric "
                 << event_record.project_context()->FullMetricName(metric) << ".";
      return kInvalidArguments;
    }
  }

  return kOK;
}

Encoder::Result IntHistogramEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool may_invalidate, EventRecord* event_record) {
  TRACE_DURATION("cobalt_core", "IntHistogramEventLogger::MaybeEncodeImmediateObservation");
  const MetricDefinition& metric = *(event_record->metric());
  const Event& event = *(event_record->event());
  CHECK(event.has_int_histogram_event());
  auto* int_histogram_event = event_record->event()->mutable_int_histogram_event();
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::INT_RANGE_HISTOGRAM: {
      HistogramPtr histogram = std::make_unique<RepeatedPtrField<HistogramBucket>>();
      if (may_invalidate) {
        // We move the buckets out of |int_histogram_event| thereby
        // invalidating that variable.
        histogram->Swap(int_histogram_event->mutable_buckets());
      } else {
        histogram->CopyFrom(int_histogram_event->buckets());
      }
      return encoder()->EncodeHistogramObservation(
          event_record->project_context()->RefMetric(&metric), &report, event.day_index(),
          int_histogram_event->event_code(), int_histogram_event->component(),
          std::move(histogram));
    }

    default:
      return BadReportType(event_record->project_context()->FullMetricName(metric), report);
  }
}

/////////////// StringUsedEventLogger method implementations ///////////////////

Encoder::Result StringUsedEventLogger::MaybeEncodeImmediateObservation(
    const ReportDefinition& report, bool /*may_invalidate*/, EventRecord* event_record) {
  TRACE_DURATION("cobalt_core", "StringUsedEventLogger::MaybeEncodeImmediateObservation");
  const MetricDefinition& metric = *(event_record->metric());
  const Event& event = *(event_record->event());
  CHECK(event.has_string_used_event());
  const auto& string_used_event = event_record->event()->string_used_event();
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::HIGH_FREQUENCY_STRING_COUNTS: {
      return encoder()->EncodeRapporObservation(event_record->project_context()->RefMetric(&metric),
                                                &report, event.day_index(),
                                                string_used_event.str());
    }
    case ReportDefinition::STRING_COUNTS_WITH_THRESHOLD: {
      return encoder()->EncodeForculusObservation(
          event_record->project_context()->RefMetric(&metric), &report, event.day_index(),
          string_used_event.str());
    }

    default:
      return BadReportType(event_record->project_context()->FullMetricName(metric), report);
  }
}

/////////////// CustomEventLogger method implementations ///////////////////////

Status CustomEventLogger::ValidateEvent(const EventRecord& /*event_record*/) {
  // TODO(ninai) Add proto validation.
  return kOK;
}

Encoder::Result CustomEventLogger::MaybeEncodeImmediateObservation(const ReportDefinition& report,
                                                                   bool may_invalidate,
                                                                   EventRecord* event_record) {
  TRACE_DURATION("cobalt_core", "CustomEventLogger::MaybeEncodeImmediateObservation");
  const MetricDefinition& metric = *(event_record->metric());
  const Event& event = *(event_record->event());
  CHECK(event.has_custom_event());
  auto* custom_event = event_record->event()->mutable_custom_event();
  switch (report.report_type()) {
    // Each report type has its own logic for generating immediate
    // observations.
    case ReportDefinition::CUSTOM_RAW_DUMP: {
      EventValuesPtr event_values =
          std::make_unique<google::protobuf::Map<std::string, CustomDimensionValue>>();
      if (may_invalidate) {
        // We move the contents out of |custom_event| thereby invalidating
        // that variable.
        event_values->swap(*(custom_event->mutable_values()));
      } else {
        event_values = std::make_unique<google::protobuf::Map<std::string, CustomDimensionValue>>(
            custom_event->values());
      }
      return encoder()->EncodeCustomObservation(event_record->project_context()->RefMetric(&metric),
                                                &report, event.day_index(),
                                                std::move(event_values));
    }

    default:
      return BadReportType(event_record->project_context()->FullMetricName(metric), report);
  }
}

}  // namespace cobalt::logger::internal

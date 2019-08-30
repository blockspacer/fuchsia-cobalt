// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logger/logger.h"

#include <memory>
#include <string>

#include "src/logger/event_loggers.h"
#include "src/logger/event_record.h"
#include "src/logging.h"
#include "src/registry/encodings.pb.h"
#include "src/registry/id.h"
#include "src/registry/metric_definition.pb.h"
#include "src/registry/report_definition.pb.h"

namespace cobalt {
namespace logger {

using ::cobalt::util::SystemClock;

using LoggerMethod = LoggerCallsMadeMetricDimensionLoggerMethod;

namespace {

template <class EventType>
void CopyEventCodesAndComponent(const std::vector<uint32_t>& event_codes,
                                const std::string& component, EventType* event) {
  for (auto event_code : event_codes) {
    event->add_event_code(event_code);
  }
  event->set_component(component);
}

}  // namespace

Logger::Logger(std::unique_ptr<ProjectContext> project_context, const Encoder* encoder,
               EventAggregator* event_aggregator, ObservationWriter* observation_writer,
               encoder::SystemDataInterface* system_data, LoggerInterface* internal_logger)
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
    internal_metrics_ = std::make_unique<InternalMetricsImpl>(internal_logger);
  } else {
    // We were not provided with a metrics logger. We must create one.
    internal_metrics_ = std::make_unique<NoOpInternalMetrics>();
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
  internal_metrics_->LoggerCalled(LoggerMethod::LogEvent, project_context_->project());
  auto* occurrence_event = event_record.event->mutable_occurrence_event();
  occurrence_event->set_event_code(event_code);
  auto event_logger = std::make_unique<internal::OccurrenceEventLogger>(
      project_context_.get(), encoder_, event_aggregator_, observation_writer_, system_data_,
      clock_.get());
  return event_logger->Log(metric_id, MetricDefinition::EVENT_OCCURRED, &event_record);
}

Status Logger::LogEventCount(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                             const std::string& component, int64_t period_duration_micros,
                             uint32_t count) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogEventCount, project_context_->project());
  EventRecord event_record;
  auto* count_event = event_record.event->mutable_count_event();
  CopyEventCodesAndComponent(event_codes, component, count_event);
  count_event->set_period_duration_micros(period_duration_micros);
  count_event->set_count(count);
  auto event_logger = std::make_unique<internal::CountEventLogger>(
      project_context_.get(), encoder_, event_aggregator_, observation_writer_, system_data_,
      clock_.get());
  return event_logger->Log(metric_id, MetricDefinition::EVENT_COUNT, &event_record);
}

Status Logger::LogElapsedTime(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                              const std::string& component, int64_t elapsed_micros) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogElapsedTime, project_context_->project());
  EventRecord event_record;
  auto* elapsed_time_event = event_record.event->mutable_elapsed_time_event();
  CopyEventCodesAndComponent(event_codes, component, elapsed_time_event);
  elapsed_time_event->set_elapsed_micros(elapsed_micros);
  auto event_logger = std::make_unique<internal::ElapsedTimeEventLogger>(
      project_context_.get(), encoder_, event_aggregator_, observation_writer_, system_data_,
      clock_.get());
  return event_logger->Log(metric_id, MetricDefinition::ELAPSED_TIME, &event_record);
}

Status Logger::LogFrameRate(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                            const std::string& component, float fps) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogFrameRate, project_context_->project());
  EventRecord event_record;
  auto* frame_rate_event = event_record.event->mutable_frame_rate_event();
  CopyEventCodesAndComponent(event_codes, component, frame_rate_event);
  // NOLINTNEXTLINE readability-magic-numbers
  frame_rate_event->set_frames_per_1000_seconds(std::round(fps * 1000.0));
  auto event_logger = std::make_unique<internal::FrameRateEventLogger>(
      project_context_.get(), encoder_, event_aggregator_, observation_writer_, system_data_,
      clock_.get());
  return event_logger->Log(metric_id, MetricDefinition::FRAME_RATE, &event_record);
}

Status Logger::LogMemoryUsage(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                              const std::string& component, int64_t bytes) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogMemoryUsage, project_context_->project());
  EventRecord event_record;
  auto* memory_usage_event = event_record.event->mutable_memory_usage_event();
  CopyEventCodesAndComponent(event_codes, component, memory_usage_event);
  memory_usage_event->set_bytes(bytes);
  auto event_logger = std::make_unique<internal::MemoryUsageEventLogger>(
      project_context_.get(), encoder_, event_aggregator_, observation_writer_, system_data_,
      clock_.get());
  return event_logger->Log(metric_id, MetricDefinition::MEMORY_USAGE, &event_record);
}

Status Logger::LogIntHistogram(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                               const std::string& component, HistogramPtr histogram) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogIntHistogram, project_context_->project());
  EventRecord event_record;
  auto* int_histogram_event = event_record.event->mutable_int_histogram_event();
  CopyEventCodesAndComponent(event_codes, component, int_histogram_event);
  int_histogram_event->mutable_buckets()->Swap(histogram.get());
  auto event_logger = std::make_unique<internal::IntHistogramEventLogger>(
      project_context_.get(), encoder_, event_aggregator_, observation_writer_, system_data_,
      clock_.get());
  return event_logger->Log(metric_id, MetricDefinition::INT_HISTOGRAM, &event_record);
}

Status Logger::LogString(uint32_t metric_id, const std::string& str) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogString, project_context_->project());
  EventRecord event_record;
  auto* string_used_event = event_record.event->mutable_string_used_event();
  string_used_event->set_str(str);
  auto event_logger = std::make_unique<internal::StringUsedEventLogger>(
      project_context_.get(), encoder_, event_aggregator_, observation_writer_, system_data_,
      clock_.get());
  return event_logger->Log(metric_id, MetricDefinition::STRING_USED, &event_record);
}

Status Logger::LogCustomEvent(uint32_t metric_id, EventValuesPtr event_values) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogCustomEvent, project_context_->project());
  EventRecord event_record;
  auto* custom_event = event_record.event->mutable_custom_event();
  custom_event->mutable_values()->swap(*event_values);
  auto event_logger = std::make_unique<internal::CustomEventLogger>(
      project_context_.get(), encoder_, event_aggregator_, observation_writer_, system_data_,
      clock_.get());
  return event_logger->Log(metric_id, MetricDefinition::CUSTOM, &event_record);
}

void Logger::PauseInternalLogging() { internal_metrics_->PauseLogging(); }
void Logger::ResumeInternalLogging() { internal_metrics_->ResumeLogging(); }

}  // namespace logger

}  // namespace cobalt

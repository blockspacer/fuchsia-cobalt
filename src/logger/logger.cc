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

namespace cobalt::logger {

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
    : Logger(std::move(project_context), encoder, event_aggregator, observation_writer, system_data,
             nullptr, std::weak_ptr<UndatedEventManager>(), internal_logger) {}

Logger::Logger(std::unique_ptr<ProjectContext> project_context, const Encoder* encoder,
               EventAggregator* event_aggregator, ObservationWriter* observation_writer,
               encoder::SystemDataInterface* system_data,
               util::ValidatedClockInterface* validated_clock,
               std::weak_ptr<UndatedEventManager> undated_event_manager,
               LoggerInterface* internal_logger)
    : project_context_(std::move(project_context)),
      encoder_(encoder),
      event_aggregator_(event_aggregator),
      observation_writer_(observation_writer),
      system_data_(system_data),
      validated_clock_(validated_clock),
      undated_event_manager_(std::move(undated_event_manager)) {
  CHECK(project_context_);
  CHECK(encoder_);
  CHECK(event_aggregator_);
  CHECK(observation_writer_);
  if (!validated_clock_) {
    local_validated_clock_ =
        std::make_unique<util::AlwaysAccurateClock>(std::make_unique<util::SystemClock>());
    validated_clock_ = local_validated_clock_.get();
  }
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
  auto event_record = std::make_unique<EventRecord>(project_context_, metric_id);
  internal_metrics_->LoggerCalled(LoggerMethod::LogEvent, project_context_->project());
  auto* occurrence_event = event_record->event()->mutable_occurrence_event();
  occurrence_event->set_event_code(event_code);
  return Log(metric_id, MetricDefinition::EVENT_OCCURRED, std::move(event_record));
}

Status Logger::LogEventCount(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                             const std::string& component, int64_t period_duration_micros,
                             uint32_t count) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogEventCount, project_context_->project());
  auto event_record = std::make_unique<EventRecord>(project_context_, metric_id);
  auto* count_event = event_record->event()->mutable_count_event();
  CopyEventCodesAndComponent(event_codes, component, count_event);
  count_event->set_period_duration_micros(period_duration_micros);
  count_event->set_count(count);
  return Log(metric_id, MetricDefinition::EVENT_COUNT, std::move(event_record));
}

Status Logger::LogElapsedTime(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                              const std::string& component, int64_t elapsed_micros) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogElapsedTime, project_context_->project());
  auto event_record = std::make_unique<EventRecord>(project_context_, metric_id);
  auto* elapsed_time_event = event_record->event()->mutable_elapsed_time_event();
  CopyEventCodesAndComponent(event_codes, component, elapsed_time_event);
  elapsed_time_event->set_elapsed_micros(elapsed_micros);
  return Log(metric_id, MetricDefinition::ELAPSED_TIME, std::move(event_record));
}

Status Logger::LogFrameRate(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                            const std::string& component, float fps) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogFrameRate, project_context_->project());
  auto event_record = std::make_unique<EventRecord>(project_context_, metric_id);
  auto* frame_rate_event = event_record->event()->mutable_frame_rate_event();
  CopyEventCodesAndComponent(event_codes, component, frame_rate_event);
  // NOLINTNEXTLINE readability-magic-numbers
  frame_rate_event->set_frames_per_1000_seconds(std::round(fps * 1000.0));
  return Log(metric_id, MetricDefinition::FRAME_RATE, std::move(event_record));
}

Status Logger::LogMemoryUsage(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                              const std::string& component, int64_t bytes) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogMemoryUsage, project_context_->project());
  auto event_record = std::make_unique<EventRecord>(project_context_, metric_id);
  auto* memory_usage_event = event_record->event()->mutable_memory_usage_event();
  CopyEventCodesAndComponent(event_codes, component, memory_usage_event);
  memory_usage_event->set_bytes(bytes);
  return Log(metric_id, MetricDefinition::MEMORY_USAGE, std::move(event_record));
}

Status Logger::LogIntHistogram(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                               const std::string& component, HistogramPtr histogram) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogIntHistogram, project_context_->project());
  auto event_record = std::make_unique<EventRecord>(project_context_, metric_id);
  auto* int_histogram_event = event_record->event()->mutable_int_histogram_event();
  CopyEventCodesAndComponent(event_codes, component, int_histogram_event);
  int_histogram_event->mutable_buckets()->Swap(histogram.get());
  return Log(metric_id, MetricDefinition::INT_HISTOGRAM, std::move(event_record));
}

Status Logger::LogString(uint32_t metric_id, const std::string& str) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogString, project_context_->project());
  auto event_record = std::make_unique<EventRecord>(project_context_, metric_id);
  auto* string_used_event = event_record->event()->mutable_string_used_event();
  string_used_event->set_str(str);
  return Log(metric_id, MetricDefinition::STRING_USED, std::move(event_record));
}

Status Logger::LogCustomEvent(uint32_t metric_id, EventValuesPtr event_values) {
  internal_metrics_->LoggerCalled(LoggerMethod::LogCustomEvent, project_context_->project());
  auto event_record = std::make_unique<EventRecord>(project_context_, metric_id);
  auto* custom_event = event_record->event()->mutable_custom_event();
  custom_event->mutable_values()->swap(*event_values);
  return Log(metric_id, MetricDefinition::CUSTOM, std::move(event_record));
}

Status Logger::Log(uint32_t metric_id, MetricDefinition::MetricType metric_type,
                   std::unique_ptr<EventRecord> event_record) {
  auto event_logger = internal::EventLogger::Create(metric_type, encoder_, event_aggregator_,
                                                    observation_writer_, system_data_);
  Status validation_result =
      event_logger->PrepareAndValidateEvent(metric_id, metric_type, event_record.get());
  if (validation_result != kOK) {
    return validation_result;
  }

  auto now = validated_clock_->now();
  if (!now) {
    // Missing system time means that the clock is not valid, so save the event until it is.
    auto undated_event_manager = undated_event_manager_.lock();
    if (undated_event_manager) {
      return undated_event_manager->Save(std::move(event_record));
    }
    // A missing UndatedEventManager is handled by retrying the clock, which should now be valid.

    // If we fall through to here, then a race condition has occurred in which the clock that we
    // thought was not validated, has become validated. Retrying the clock should succeed.
    now = validated_clock_->now();
    if (!now) {
      // This should never happen, if it does then it's a bug.
      LOG(ERROR) << "Clock is invalid but there is no UndatedEventManager that will save the "
                    "event, dropping event for metric: "
                 << metric_id;
      return Status::kOther;
    }
  }

  return event_logger->Log(std::move(event_record), *now);
}

void Logger::PauseInternalLogging() { internal_metrics_->PauseLogging(); }
void Logger::ResumeInternalLogging() { internal_metrics_->ResumeLogging(); }

}  // namespace cobalt::logger

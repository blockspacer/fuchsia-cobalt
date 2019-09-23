// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOGGER_EVENT_RECORD_H_
#define COBALT_SRC_LOGGER_EVENT_RECORD_H_

#include <memory>

#include "src/pb/event.pb.h"
#include "src/registry/metric_definition.pb.h"

namespace cobalt::logger {

// A container for an Event proto message and the ProjectContext and the metric within it for which
// that Event should be logged.
class EventRecord {
 public:
  EventRecord(std::shared_ptr<const ProjectContext> project_context, uint32_t metric_id)
      : project_context_(std::move(project_context)), event_(std::make_unique<Event>()) {
    metric_ = project_context_->GetMetric(metric_id);
  }
  ~EventRecord() = default;

  // Get the ProjectContext associated with this Event.
  [[nodiscard]] const ProjectContext* project_context() const { return project_context_.get(); }

  // Get the Metric within the ProjectContext that this Event is for.
  [[nodiscard]] const MetricDefinition* metric() const { return metric_; }

  // Get the Event that is to be logged.
  [[nodiscard]] Event* event() const { return event_.get(); }

 private:
  const std::shared_ptr<const ProjectContext> project_context_;
  const MetricDefinition* metric_;
  const std::unique_ptr<Event> event_;
};

}  // namespace cobalt::logger

#endif  // COBALT_SRC_LOGGER_EVENT_RECORD_H_

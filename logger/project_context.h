// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_PROJECT_CONTEXT_H_
#define COBALT_LOGGER_PROJECT_CONTEXT_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "config/metric_definition.pb.h"
#include "config/project.pb.h"
#include "logger/status.h"

namespace cobalt {
namespace logger {

std::string MetricDebugString(const MetricDefinition& metric);

// A reference object that gives access to the names and IDs of a Metric and
// its owning Project and Customer. One way to obtain a MetricRef is the method
// ProjectContext::RefMetric().
class MetricRef {
 public:
  // A MetricRef is constructed from a Project and a MetricDefinition. The
  // Project and MetricDefinition must remain valid as long as the MetricRef is
  // being used.
  MetricRef(const Project* project, const MetricDefinition* metric_definition);

  const Project& project() const;

  std::string ProjectDebugString() const;

  uint32_t metric_id() const;

  const std::string& metric_name() const;

  // Returns the string <customer_name>.<project_name>.<metric_name>
  std::string FullyQualifiedName() const;

 private:
  friend class ProjectContext;
  const Project* project_;
  const MetricDefinition* metric_definition_;
};

// ProjectContext stores the Cobalt configuration for a single Cobalt project.
class ProjectContext {
 public:
  ProjectContext(uint32_t customer_id, uint32_t project_id,
                 std::string customer_name, std::string project_name,
                 std::unique_ptr<MetricDefinitions> metric_definitions,
                 ReleaseStage release_stage = GA);

  const MetricDefinition* GetMetric(const std::string& metric_name) const;
  const MetricDefinition* GetMetric(const uint32_t metric_id) const;
  // Makes a MetricRef that wraps this ProjectContext's Project and the given
  // metric_definition (which should have been obtained via GetMetric()).
  // The Project and MetricDefinition must remain valid as long as the returned
  // MetricRef is being used.
  const MetricRef RefMetric(const MetricDefinition* metric_definition) const;

  const Project& project() const { return project_; }

  const std::string DebugString() const;

 private:
  Project project_;
  const std::unique_ptr<MetricDefinitions> metric_definitions_;
  std::map<const std::string, const MetricDefinition*> metrics_by_name_;
  std::map<const uint32_t, const MetricDefinition*> metrics_by_id_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_PROJECT_CONTEXT_H_

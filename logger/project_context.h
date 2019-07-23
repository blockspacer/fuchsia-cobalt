// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_PROJECT_CONTEXT_H_
#define COBALT_LOGGER_PROJECT_CONTEXT_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "config/cobalt_registry.pb.h"
#include "config/metric_definition.pb.h"
#include "config/project.pb.h"
#include "config/project_configs.h"
#include "logger/status.h"
#include "third_party/statusor/statusor.h"

namespace cobalt {
namespace logger {

// A pair (metric ID, report ID).
using MetricReportId = std::pair<uint32_t, uint32_t>;

// A reference object that gives access to the names and IDs of a Metric and
// its owning Project and Customer. One way to obtain a MetricRef is the method
// ProjectContext::RefMetric().
class MetricRef {
 public:
  // A MetricRef is constructed from a Project and a MetricDefinition. The
  // Project and MetricDefinition must remain valid as long as the MetricRef is
  // being used.
  MetricRef(const Project* project, const MetricDefinition* metric_definition);

  [[nodiscard]] const Project& project() const;

  [[nodiscard]] std::string ProjectDebugString() const;

  [[nodiscard]] uint32_t metric_id() const;

  [[nodiscard]] const std::string& metric_name() const;

  // Returns the string <customer_name>.<project_name>.<metric_name>
  [[nodiscard]] std::string FullyQualifiedName() const;

 private:
  friend class ProjectContext;
  const Project* project_;
  const MetricDefinition* metric_definition_;
};

// ProjectContext stores the metrics registration data for a single Cobalt
// project and makes it conveniently available on the client.
//
// The underlying data comes from a |ProjectConfig| proto message which
// represents a single project within the Cobalt Registry.  An instance of
// ProjectContext does not copy the data from a |ProjectConfig|, but rather
// maintains a pointer to an instance of |ProjectConfig|. An instance of
// ProjectContext may or may not own its underlying |ProjectConfig|, depending
// on which constructor is used.
class ProjectContext {
 public:
  // Constructs an instance of ProjectContext that does own the
  // underlying |ProjectConfig|.
  //
  // |customer_id| The id of the customer this project is for.
  //
  // |customer_name| The name of the customer this project is for.
  //
  // |project_config| The |ProjectConfig| containing the project data.
  //
  // |release_stage| The declared release stage of the unit of code that will
  // use this ProjectContext. Used to determine which metrics from the
  // given |ProjectConfig| are allowed to be used. Optional.
  ProjectContext(uint32_t customer_id, const std::string& customer_name,
                 std::unique_ptr<ProjectConfig> project_config, ReleaseStage release_stage = GA);

  // Constructs an instance of ProjectContext that does not own the
  // underlying |ProjectConfig|.
  //
  // |customer_id| The id of the customer this project is for.
  //
  // |customer_name| The name of the customer this project is for.
  //
  // |project_config| The |ProjectConfig| containing the project data.
  //
  // |release_stage| The declared release stage of the unit of code that will
  // use this ProjectContext. Used to determine which metrics from the
  // given |ProjectConfig| are allowed to be used. Optional.
  ProjectContext(uint32_t customer_id, const std::string& customer_name,
                 const ProjectConfig* project_config, ReleaseStage release_stage = GA);

  // Returns the MetricDefinition for the metric with the given name, or
  // nullptr if there is no such metric.
  const MetricDefinition* GetMetric(const std::string& metric_name) const;

  // Returns the MetricDefinition for the metric with the given ID, or
  // nullptr if there is no such metric.
  const MetricDefinition* GetMetric(uint32_t metric_id) const;

  // Makes a MetricRef that wraps this ProjectContext's Project and the given
  // metric_definition (which should have been obtained via GetMetric()).
  // The Project and MetricDefinition must remain valid as long as the returned
  // MetricRef is being used.
  MetricRef RefMetric(const MetricDefinition* metric_definition) const;

  // Gives access to the metadata for the project.
  const Project& project() const { return project_; }

  // Gives access to the list of all MetricDefinitions for the project.
  const google::protobuf::RepeatedPtrField<MetricDefinition>& metrics() const {
    return project_config_->metrics();
  }

  std::string DebugString() const;

  // Returns the string <customer_name>(<id>).<project_name>(<id>)
  std::string static FullyQualifiedName(const Project& project);

  // Returns the string
  // <customer_name>(<id>).<project_name>(<id>).<metric_name>(<id>)
  std::string static FullMetricName(const Project& project, const MetricDefinition& metric);

  // Returns the string <customer_name>(<id>).<project_name>(<id>)
  std::string FullyQualifiedName() const;

  // Returns the string
  // <customer_name>(<id>).<project_name>(<id>).<metric_name>(<id>)
  std::string FullMetricName(const MetricDefinition& metric_definition) const;

 private:
  // Constructs an instance of ProjectContext that may or may not own the
  // underlying |ProjectConfig|.
  //
  // |customer_id| The id of the customer this project is for.
  //
  // |customer_name| The name of the customer this project is for.
  //
  // |owned_project_config| Exactly one of |owned_project_config| or
  // |project_config| must be not null or we will CHECK fail.
  //
  // |project_config| Exactly one of |owned_project_config| or
  // |project_config| must be not null or we will CHECK fail.
  //
  // |release_stage| The declared release stage of the unit of code that will
  // use this ProjectContext. Used to determine which metrics from the
  // given |ProjectConfig| are allowed to be used. Optional.
  ProjectContext(uint32_t customer_id, const std::string& customer_name,
                 const ProjectConfig* project_config,
                 std::unique_ptr<ProjectConfig> owned_project_config,
                 ReleaseStage release_stage = GA);

  Project project_;

  // This pointer is always non-null and points either to
  // maybe_null_project_config_ or to an instance of ProjectConfig
  // not owned by this object. Access the underlying ProjectConfig through
  // this pointer and never through maybe_null_project_config_;
  const ProjectConfig* project_config_;

  // An instance of ProjectContext may or may not own the underlying
  // ProjectConfig, so this may or may not be null. Never access this
  // variable directly after it is initialized in the constructor. Instead
  // access |project_config_|.
  const std::unique_ptr<ProjectConfig> maybe_null_project_config_;

  std::map<const std::string, const MetricDefinition*> metrics_by_name_;
  std::map<const uint32_t, const MetricDefinition*> metrics_by_id_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_PROJECT_CONTEXT_H_

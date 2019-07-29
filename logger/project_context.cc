// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/project_context.h"

#include <sstream>

#include "./logging.h"

namespace cobalt {
namespace logger {

namespace {

void PopulateProject(uint32_t customer_id, uint32_t project_id, const std::string& customer_name,
                     const std::string& project_name, ReleaseStage release_stage,
                     Project* project) {
  project->set_customer_id(customer_id);
  project->set_project_id(project_id);
  project->set_customer_name(customer_name);
  project->set_project_name(project_name);
  project->set_release_stage(release_stage);
}

}  // namespace

MetricRef::MetricRef(const Project* project, const MetricDefinition* metric_definition)
    : project_(project), metric_definition_(metric_definition) {}

const Project& MetricRef::project() const { return *project_; }

std::string MetricRef::ProjectDebugString() const {
#ifdef PROTO_LITE
  return project_->project_name();
#else
  return project_->DebugString();
#endif
}

uint32_t MetricRef::metric_id() const { return metric_definition_->id(); }

const std::string& MetricRef::metric_name() const { return metric_definition_->metric_name(); }

std::string MetricRef::FullyQualifiedName() const {
  return ProjectContext::FullMetricName(*project_, *metric_definition_);
}

ProjectContext::ProjectContext(uint32_t customer_id, const std::string& customer_name,
                               std::unique_ptr<ProjectConfig> project_config,
                               ReleaseStage release_stage)
    : ProjectContext(customer_id, customer_name, nullptr, std::move(project_config),
                     release_stage) {}

ProjectContext::ProjectContext(uint32_t customer_id, const std::string& customer_name,
                               const ProjectConfig* project_config, ReleaseStage release_stage)
    : ProjectContext(customer_id, customer_name, project_config, nullptr, release_stage) {}

ProjectContext::ProjectContext(uint32_t customer_id, const std::string& customer_name,
                               const ProjectConfig* project_config,
                               std::unique_ptr<ProjectConfig> owned_project_config,
                               ReleaseStage release_stage)
    : project_config_(project_config), maybe_null_project_config_(std::move(owned_project_config)) {
  CHECK(project_config_ || maybe_null_project_config_);
  CHECK(!(project_config_ && maybe_null_project_config_));
  if (!project_config_) {
    project_config_ = maybe_null_project_config_.get();
  }
  PopulateProject(customer_id, project_config_->project_id(), customer_name,
                  project_config_->project_name(), release_stage, &project_);
  for (const auto& metric : project_config_->metrics()) {
    if (metric.customer_id() == project_.customer_id() &&
        metric.project_id() == project_.project_id()) {
      metrics_by_name_[metric.metric_name()] = &metric;
      metrics_by_id_[metric.id()] = &metric;
    } else {
      LOG(ERROR) << "ProjectContext constructor found a MetricDefinition "
                    "for the wrong project. Expected customer "
                 << project_.customer_name() << " (id=" << project_.customer_id() << "), project "
                 << project_.project_name() << " (id=" << project_.project_id()
                 << "). Found customer_id=" << metric.customer_id()
                 << " project_id=" << metric.project_id();
    }
  }
}

const MetricDefinition* ProjectContext::GetMetric(const uint32_t metric_id) const {
  auto iter = metrics_by_id_.find(metric_id);
  if (iter == metrics_by_id_.end()) {
    return nullptr;
  }
  return iter->second;
}

const MetricDefinition* ProjectContext::GetMetric(const std::string& metric_name) const {
  auto iter = metrics_by_name_.find(metric_name);
  if (iter == metrics_by_name_.end()) {
    return nullptr;
  }
  return iter->second;
}

MetricRef ProjectContext::RefMetric(const MetricDefinition* metric_definition) const {
  return MetricRef(&project_, metric_definition);
}

std::string ProjectContext::DebugString() const {
#ifdef PROTO_LITE
  return project_.project_name();
#else
  return project_.DebugString();
#endif
}

std::string ProjectContext::FullyQualifiedName(const Project& project) {
  std::ostringstream stream;
  stream << project.customer_name() << "(" << project.customer_id() << ")"
         << "." << project.project_name() << "(" << project.project_id() << ")";
  return stream.str();
}

std::string ProjectContext::FullMetricName(const Project& project, const MetricDefinition& metric) {
  std::ostringstream stream;
  stream << FullyQualifiedName(project) << "." << metric.metric_name() << "(" << metric.id() << ")";
  return stream.str();
}

std::string ProjectContext::FullyQualifiedName() const { return FullyQualifiedName(project_); }

std::string ProjectContext::FullMetricName(const MetricDefinition& metric_definition) const {
  return FullMetricName(project_, metric_definition);
}

}  // namespace logger
}  // namespace cobalt

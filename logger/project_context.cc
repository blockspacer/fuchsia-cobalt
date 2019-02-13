// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/project_context.h"

#include <sstream>

#include "./logging.h"

namespace cobalt {
namespace logger {

using statusor::StatusOr;
using util::StatusCode;

std::string MetricDebugString(const MetricDefinition& metric) {
  std::ostringstream stream;
  stream << metric.metric_name() << " (" << metric.id() << ")";
  return stream.str();
}

MetricRef::MetricRef(const Project* project,
                     const MetricDefinition* metric_definition)
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

const std::string& MetricRef::metric_name() const {
  return metric_definition_->metric_name();
}

std::string MetricRef::FullyQualifiedName() const {
  std::ostringstream stream;
  stream << project_->customer_name() << "." << project_->project_name() << "."
         << metric_definition_->metric_name();
  return stream.str();
}

namespace {

void PopulateProject(uint32_t customer_id, uint32_t project_id,
                     const std::string& customer_name,
                     const std::string& project_name,
                     ReleaseStage release_stage, Project* project) {
  project->set_customer_id(customer_id);
  project->set_project_id(project_id);
  project->set_customer_name(std::move(customer_name));
  project->set_project_name(std::move(project_name));
  project->set_release_stage(release_stage);
}

}  // namespace

ProjectContext::ProjectContext(
    uint32_t customer_id, uint32_t project_id, std::string customer_name,
    std::string project_name,
    std::unique_ptr<MetricDefinitions> metric_definitions,
    ReleaseStage release_stage)
    : metric_definitions_(std::move(metric_definitions)) {
  CHECK(metric_definitions_);
  PopulateProject(customer_id, project_id, customer_name, project_name,
                  release_stage, &project_);
  for (const auto& metric : metric_definitions_->metric()) {
    if (metric.customer_id() == project_.customer_id() &&
        metric.project_id() == project_.project_id()) {
      metrics_by_name_[metric.metric_name()] = &metric;
      metrics_by_id_[metric.id()] = &metric;
    } else {
      LOG(ERROR) << "ProjectContext constructor found a MetricDefinition "
                    "for the wrong project. Expected customer "
                 << project_.customer_name()
                 << " (id=" << project_.customer_id() << "), project "
                 << project_.project_name() << " (id=" << project_.project_id()
                 << "). Found customer_id=" << metric.customer_id()
                 << " project_id=" << metric.project_id();
    }
  }
}

StatusOr<std::unique_ptr<ProjectContext>>
ProjectContext::ConstructWithProjectConfigs(
    const std::string& customer_name, const std::string& project_name,
    std::shared_ptr<config::ProjectConfigs> project_configs,
    ReleaseStage release_stage) {
  if (!project_configs) {
    return util::Status(StatusCode::INVALID_ARGUMENT,
                        "The project_configs argument was null.");
  }
  auto customer_cfg = project_configs->GetCustomerConfig(customer_name);
  if (!customer_cfg) {
    return util::Status(StatusCode::INVALID_ARGUMENT,
                        "Could not find a customer named " + customer_name +
                            " in the provided ProjectConfigs.");
  }
  auto project_cfg =
      project_configs->GetProjectConfig(customer_name, project_name);
  if (!project_cfg) {
    return util::Status(StatusCode::INVALID_ARGUMENT,
                        "Could not find a project named " + project_name +
                            " for the customer named " + customer_name +
                            " in the provided ProjectConfigs.");
  }
  return std::unique_ptr<ProjectContext>(new ProjectContext(
      customer_cfg->customer_id(), project_cfg->project_id(), customer_name,
      project_name, project_configs, release_stage));
}

ProjectContext::ProjectContext(
    uint32_t customer_id, uint32_t project_id, const std::string& customer_name,
    const std::string& project_name,
    std::shared_ptr<config::ProjectConfigs> project_configs,
    ReleaseStage release_stage)
    : metric_definitions_(new MetricDefinitions()),
      project_configs_(project_configs) {
  PopulateProject(customer_id, project_id, customer_name, project_name,
                  release_stage, &project_);
  auto project_cfg =
      project_configs->GetProjectConfig(customer_name, project_name);

  CHECK(project_cfg) << "Customer " << customer_name << " with project "
                     << project_name << " was not found.";

  for (const auto& metric : project_cfg->metrics()) {
    if (metric.customer_id() == project_.customer_id() &&
        metric.project_id() == project_.project_id()) {
      metrics_by_name_[metric.metric_name()] = &metric;
      metrics_by_id_[metric.id()] = &metric;
    } else {
      LOG(ERROR) << "ProjectContext constructor found a MetricDefinition "
                    "for the wrong project. Expected customer "
                 << project_.customer_name()
                 << " (id=" << project_.customer_id() << "), project "
                 << project_.project_name() << " (id=" << project_.project_id()
                 << "). Found customer_id=" << metric.customer_id()
                 << " project_id=" << metric.project_id();
    }
    metric_definitions_->add_metric()->CopyFrom(metric);
  }
}

const MetricDefinition* ProjectContext::GetMetric(
    const uint32_t metric_id) const {
  auto iter = metrics_by_id_.find(metric_id);
  if (iter == metrics_by_id_.end()) {
    return nullptr;
  }
  return iter->second;
}

const MetricDefinition* ProjectContext::GetMetric(
    const std::string& metric_name) const {
  auto iter = metrics_by_name_.find(metric_name);
  if (iter == metrics_by_name_.end()) {
    return nullptr;
  }
  return iter->second;
}

const MetricRef ProjectContext::RefMetric(
    const MetricDefinition* metric_definition) const {
  return MetricRef(&project_, metric_definition);
}
std::string ProjectContext::DebugString() const {
#ifdef PROTO_LITE
  return project_.project_name();
#else
  return project_.DebugString();
#endif
}

std::string ProjectContext::FullyQualifiedName() const {
  std::ostringstream stream;
  stream << project_.customer_name() << "." << project_.project_name();
  return stream.str();
}

}  // namespace logger
}  // namespace cobalt

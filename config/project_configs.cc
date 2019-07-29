// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config/project_configs.h"

#include "./logging.h"
#include "util/crypto_util/base64.h"

namespace cobalt {
namespace config {

std::unique_ptr<ProjectConfigs> ProjectConfigs::CreateFromCobaltRegistryBase64(
    const std::string& cobalt_registry_base64) {
  std::string cobalt_registry_bytes;
  if (!crypto::Base64Decode(cobalt_registry_base64, &cobalt_registry_bytes)) {
    LOG(ERROR) << "Unable to parse the provided string as base-64";
    return nullptr;
  }
  return CreateFromCobaltRegistryBytes(cobalt_registry_bytes);
}

std::unique_ptr<ProjectConfigs> ProjectConfigs::CreateFromCobaltRegistryBytes(
    const std::string& cobalt_registry_bytes) {
  auto cobalt_registry = std::make_unique<CobaltRegistry>();
  if (!cobalt_registry->ParseFromString(cobalt_registry_bytes)) {
    LOG(ERROR) << "Unable to parse a CobaltRegistry from the provided bytes.";
    return nullptr;
  }
  return CreateFromCobaltRegistryProto(std::move(cobalt_registry));
}

std::unique_ptr<ProjectConfigs> ProjectConfigs::CreateFromCobaltRegistryProto(
    std::unique_ptr<CobaltRegistry> cobalt_registry) {
  return std::make_unique<ProjectConfigs>(std::move(cobalt_registry));
}

ProjectConfigs::ProjectConfigs(std::unique_ptr<CobaltRegistry> cobalt_registry)
    : cobalt_registry_(std::move(cobalt_registry)) {
  is_empty_ = cobalt_registry_->customers_size() == 0;
  is_single_project_ = false;
  if (cobalt_registry_->customers_size() == 1) {
    auto customer = cobalt_registry_->customers(0);
    if (customer.projects_size() == 1) {
      const auto& project = customer.projects(0);
      is_single_project_ = true;
      single_customer_id_ = customer.customer_id();
      single_customer_name_ = customer.customer_name();
      single_project_id_ = project.project_id();
      single_project_name_ = project.project_name();
    }
  }
  for (const auto& customer : cobalt_registry_->customers()) {
    customers_by_id_[customer.customer_id()] = &customer;
    customers_by_name_[customer.customer_name()] = &customer;
    for (const auto& project : customer.projects()) {
      projects_by_id_[std::make_tuple(customer.customer_id(), project.project_id())] = &project;
      projects_by_name_[std::make_tuple(customer.customer_name(), project.project_name())] =
          &project;
      for (const auto& metric : project.metrics()) {
        metrics_by_id_[std::make_tuple(customer.customer_id(), project.project_id(), metric.id())] =
            &metric;
        for (const auto& report : metric.reports()) {
          reports_by_id_[std::make_tuple(customer.customer_id(), project.project_id(), metric.id(),
                                         report.id())] = &report;
        }
      }
    }
  }
}

std::unique_ptr<ProjectConfig> ProjectConfigs::TakeSingleProjectConfig() {
  if (!is_single_project_) {
    return nullptr;
  }
  is_empty_ = true;
  is_single_project_ = false;
  projects_by_name_.clear();
  projects_by_id_.clear();
  metrics_by_id_.clear();
  reports_by_id_.clear();
  auto project_config = std::make_unique<ProjectConfig>();
  CHECK(!cobalt_registry_->customers().empty());
  CHECK(!cobalt_registry_->customers(0).projects().empty());
  project_config->Swap(cobalt_registry_->mutable_customers(0)->mutable_projects(0));
  cobalt_registry_->mutable_customers(0)->mutable_projects()->Clear();
  return project_config;
}

const CustomerConfig* ProjectConfigs::GetCustomerConfig(const std::string& customer_name) const {
  auto iter = customers_by_name_.find(customer_name);
  if (iter == customers_by_name_.end()) {
    return nullptr;
  }
  return iter->second;
}
const CustomerConfig* ProjectConfigs::GetCustomerConfig(uint32_t customer_id) const {
  auto iter = customers_by_id_.find(customer_id);
  if (iter == customers_by_id_.end()) {
    return nullptr;
  }
  return iter->second;
}

const ProjectConfig* ProjectConfigs::GetProjectConfig(const std::string& customer_name,
                                                      const std::string& project_name) const {
  auto iter = projects_by_name_.find(std::make_tuple(customer_name, project_name));
  if (iter == projects_by_name_.end()) {
    return nullptr;
  }
  return iter->second;
}
const ProjectConfig* ProjectConfigs::GetProjectConfig(uint32_t customer_id,
                                                      uint32_t project_id) const {
  auto iter = projects_by_id_.find(std::make_tuple(customer_id, project_id));
  if (iter == projects_by_id_.end()) {
    return nullptr;
  }
  return iter->second;
}

const MetricDefinition* ProjectConfigs::GetMetricDefinition(uint32_t customer_id,
                                                            uint32_t project_id,
                                                            uint32_t metric_id) const {
  auto iter = metrics_by_id_.find(std::make_tuple(customer_id, project_id, metric_id));
  if (iter == metrics_by_id_.end()) {
    return nullptr;
  }
  return iter->second;
}

const ReportDefinition* ProjectConfigs::GetReportDefinition(uint32_t customer_id,
                                                            uint32_t project_id, uint32_t metric_id,
                                                            uint32_t report_id) const {
  auto iter = reports_by_id_.find(std::make_tuple(customer_id, project_id, metric_id, report_id));
  if (iter == reports_by_id_.end()) {
    return nullptr;
  }
  return iter->second;
}

}  // namespace config
}  // namespace cobalt

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logger/project_context_factory.h"

#include <utility>

#include "src/lib/crypto_util/base64.h"
#include "src/logging.h"
#include "src/registry/metric_definition.pb.h"
#include "src/registry/project.pb.h"
#include "src/registry/project_configs.h"

namespace cobalt::logger {

namespace {

// Always returns a valid pointer to a CobaltRegistry. If the bytes could not
// be parsed then the CobaltRegistry is empty.
std::unique_ptr<CobaltRegistry> ParseRegistryBytes(const std::string& cobalt_registry_bytes) {
  // Attempt to deserialize a CobaltRegistry.
  auto cobalt_registry = std::make_unique<CobaltRegistry>();
  if (!cobalt_registry->ParseFromString(cobalt_registry_bytes)) {
    LOG(ERROR) << "Unable to parse a CobaltRegistry from the provided bytes.";
  }
  return cobalt_registry;
}
}  // namespace

std::unique_ptr<ProjectContextFactory> ProjectContextFactory::CreateFromCobaltRegistryBase64(
    const std::string& cobalt_registry_base64) {
  std::string cobalt_registry_bytes;
  if (!crypto::Base64Decode(cobalt_registry_base64, &cobalt_registry_bytes)) {
    LOG(ERROR) << "Unable to parse the provided string as base-64";
    return nullptr;
  }
  return std::make_unique<ProjectContextFactory>(cobalt_registry_bytes);
}

ProjectContextFactory::ProjectContextFactory(const std::string& cobalt_registry_bytes)
    : ProjectContextFactory(ParseRegistryBytes(cobalt_registry_bytes)) {}

ProjectContextFactory::ProjectContextFactory(std::unique_ptr<CobaltRegistry> cobalt_registry) {
  CHECK(cobalt_registry);
  project_configs_ =
      config::ProjectConfigs::CreateFromCobaltRegistryProto(std::move(cobalt_registry));
}

std::unique_ptr<ProjectContext> ProjectContextFactory::NewProjectContext(
    const std::string& customer_name, const std::string& project_name) {
  if (project_configs_ == nullptr) {
    return nullptr;
  }
  const auto* customer_config = project_configs_->GetCustomerConfig(customer_name);
  if (!customer_config) {
    return nullptr;
  }
  const auto* project_config = project_configs_->GetProjectConfig(customer_name, project_name);
  if (!project_config) {
    return nullptr;
  }
  return std::make_unique<ProjectContext>(customer_config->customer_id(), customer_name,
                                          project_config);
}

std::unique_ptr<ProjectContext> ProjectContextFactory::NewProjectContext(uint32_t customer_id,
                                                                         uint32_t project_id) {
  if (project_configs_ == nullptr) {
    return nullptr;
  }
  const auto* customer_config = project_configs_->GetCustomerConfig(customer_id);
  if (!customer_config) {
    return nullptr;
  }
  const auto* project_config = project_configs_->GetProjectConfig(customer_id, project_id);
  if (!project_config) {
    return nullptr;
  }
  return std::make_unique<ProjectContext>(customer_id, customer_config->customer_name(),
                                          project_config);
}

std::unique_ptr<ProjectContext> ProjectContextFactory::TakeSingleProjectContext() {
  if (!is_single_project()) {
    return nullptr;
  }
  auto project_context = std::make_unique<ProjectContext>(
      project_configs_->single_customer_id(), project_configs_->single_customer_name(),
      project_configs_->TakeSingleProjectConfig());
  project_configs_.reset();
  return project_context;
}

}  // namespace cobalt::logger

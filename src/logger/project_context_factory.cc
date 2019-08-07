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

namespace cobalt {
namespace logger {

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

  // Create a second CobaltRegistry and separate the Cobalt 1.0 data
  // from the Cobalt 0.1 data.
  auto legacy_cobalt_registry = std::make_unique<CobaltRegistry>();
  legacy_cobalt_registry->mutable_metric_configs()->Swap(cobalt_registry->mutable_metric_configs());
  legacy_cobalt_registry->mutable_encoding_configs()->Swap(
      cobalt_registry->mutable_encoding_configs());

  // If there is any Cobalt 1.0 data create a Cobalt 1.0 ProjectConfigs
  // object to wrap it.
  if (cobalt_registry->customers_size() > 0) {
    project_configs_ =
        config::ProjectConfigs::CreateFromCobaltRegistryProto(std::move(cobalt_registry));
  }

  // If there is any Cobalt 0.1 data create a Cobalt 0.1 ClientConfig object
  // to wrap it.
  if (legacy_cobalt_registry->metric_configs_size() > 0) {
    client_config_ =
        config::ClientConfig::CreateFromCobaltRegistryProto(std::move(legacy_cobalt_registry));
  }
}

std::unique_ptr<ProjectContext> ProjectContextFactory::NewProjectContext(
    const std::string& customer_name, const std::string& project_name, ReleaseStage release_stage) {
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
                                          project_config, release_stage);
}

std::unique_ptr<ProjectContext> ProjectContextFactory::TakeSingleProjectContext(
    ReleaseStage release_stage) {
  if (!is_single_project()) {
    return nullptr;
  }
  auto project_context = std::make_unique<ProjectContext>(
      project_configs_->single_customer_id(), project_configs_->single_customer_name(),
      project_configs_->TakeSingleProjectConfig(), release_stage);
  project_configs_.reset();
  return project_context;
}

std::unique_ptr<encoder::ProjectContext> ProjectContextFactory::NewLegacyProjectContext(
    uint32_t customer_id, uint32_t project_id) {
  if (client_config_ == nullptr) {
    return nullptr;
  }
  return std::make_unique<encoder::ProjectContext>(customer_id, project_id, client_config_);
}

std::unique_ptr<encoder::ProjectContext> ProjectContextFactory::NewSingleLegacyProjectContext() {
  if (!is_single_legacy_project()) {
    return nullptr;
  }
  return NewLegacyProjectContext(client_config_->single_customer_id(),
                                 client_config_->single_project_id());
}

}  // namespace logger
}  // namespace cobalt

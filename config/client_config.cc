// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config/client_config.h"

#include <memory>
#include <string>
#include <utility>

#include "./logging.h"
#include "config/cobalt_registry.pb.h"
#include "config/encoding_config.h"
#include "config/encodings.pb.h"
#include "config/metric_config.h"
#include "config/metrics.pb.h"
#include "util/crypto_util/base64.h"

namespace cobalt {
namespace config {

namespace {
std::string ErrorMessage(Status status) {
  switch (status) {
    case kOK:
      return "No error";

    case kFileOpenError:
      return "Unable to open file: ";

    case kParsingError:
      return "Error while parsing file: ";

    case kDuplicateRegistration:
      return "Duplicate ID found in file: ";

    default:
      return "Unknown problem with: ";
  }
}
}  // namespace

std::unique_ptr<ClientConfig> ClientConfig::CreateFromCobaltRegistryBase64(
    const std::string& cobalt_registry_base64) {
  std::string cobalt_registry_bytes;
  if (!crypto::Base64Decode(cobalt_registry_base64, &cobalt_registry_bytes)) {
    LOG(ERROR) << "Unable to parse the provided string as base-64";
    return nullptr;
  }
  return CreateFromCobaltRegistryBytes(cobalt_registry_bytes);
}

std::unique_ptr<ClientConfig> ClientConfig::CreateFromCobaltRegistryBytes(
    const std::string& cobalt_registry_bytes) {
  auto cobalt_registry = std::make_unique<CobaltRegistry>();
  if (!cobalt_registry->ParseFromString(cobalt_registry_bytes)) {
    LOG(ERROR) << "Unable to parse a CobaltRegistry from the provided bytes.";
    return nullptr;
  }
  return CreateFromCobaltRegistryProto(std::move(cobalt_registry));
}

std::pair<std::unique_ptr<ClientConfig>, uint32_t>
ClientConfig::CreateFromCobaltProjectRegistryBytes(
    const std::string& cobalt_registry_bytes) {
  auto client_config = CreateFromCobaltRegistryBytes(cobalt_registry_bytes);
  if (!client_config) {
    return std::make_pair(nullptr, 0);
  }
  if (client_config->is_empty()) {
    LOG(ERROR) << "No project data found in the provided CobaltRegistry.";
    return std::make_pair(nullptr, 0);
  }
  if (!client_config->is_single_project()) {
    LOG(ERROR) << "More than one project found in the provided CobaltRegistry.";
    return std::make_pair(nullptr, 0);
  }

  return std::make_pair(std::move(client_config),
                        client_config->single_project_id());
}

std::unique_ptr<ClientConfig> ClientConfig::CreateFromCobaltRegistryProto(
    std::unique_ptr<CobaltRegistry> cobalt_registry) {
  return CreateFromCobaltRegistry(cobalt_registry.get());
}

// DEPRECATED: As soon as we are able to delete this method, move its logic
// into CreateFromCobaltRegistryProto() above instead.
std::unique_ptr<ClientConfig> ClientConfig::CreateFromCobaltRegistry(
    CobaltRegistry* cobalt_registry) {
  RegisteredEncodings registered_encodings;
  registered_encodings.mutable_element()->Swap(
      cobalt_registry->mutable_encoding_configs());
  auto encoding_registry_and_status =
      EncodingRegistry::TakeFrom(&registered_encodings, nullptr);
  if (encoding_registry_and_status.second != config::kOK) {
    LOG(ERROR) << "Invalid EncodingConfigs. "
               << ErrorMessage(encoding_registry_and_status.second);
    return std::unique_ptr<ClientConfig>(nullptr);
  }

  RegisteredMetrics registered_metrics;
  registered_metrics.mutable_element()->Swap(
      cobalt_registry->mutable_metric_configs());
  auto metrics_registry_and_status =
      MetricRegistry::TakeFrom(&registered_metrics, nullptr);
  if (metrics_registry_and_status.second != config::kOK) {
    LOG(ERROR) << "Error getting Metrics from registry. "
               << ErrorMessage(metrics_registry_and_status.second);
    return std::unique_ptr<ClientConfig>(nullptr);
  }

  auto client_config = std::unique_ptr<ClientConfig>(
      new ClientConfig(std::move(encoding_registry_and_status.first),
                       std::move(metrics_registry_and_status.first)));

  // Deprecated: Delete this block once TakeCustomerConfig() has no more uses.
  size_t num_customers = cobalt_registry->customers_size();
  if (num_customers == 0) {
    // There is no Cobalt 1.0 data. We are done.
    return client_config;
  }
  // Since there is Cobalt 1.0 data, any previous computation regarding
  // whether or not there is a single project based on the Cobalt 0.1 data
  // only is invalid. Initialize is_single_project to false.
  client_config->is_single_project_ = false;
  if (!client_config->is_empty_) {
    // There is both Cobalt 0.1 and Cobalt 1.0 data.
    return client_config;
  }
  client_config->is_empty_ = false;
  if (num_customers > 1) {
    // There is more than one Cobalt 1.0 customer.
    return client_config;
  }
  auto* single_customer = cobalt_registry->mutable_customers(0);
  if (single_customer->projects_size() != 1) {
    // The first Cobalt 1.0 customer does not have exactly one project.
    return client_config;
  }
  auto single_project = single_customer->projects(0);
  client_config->is_single_project_ = true;
  client_config->single_customer_id_ = single_customer->customer_id();
  client_config->single_project_id_ = single_project.project_id();
  client_config->single_customer_config_.reset(new CustomerConfig());
  client_config->single_customer_config_->Swap(single_customer);

  return client_config;
}

const EncodingConfig* ClientConfig::EncodingConfig(
    uint32_t customer_id, uint32_t project_id, uint32_t encoding_config_id) {
  return encoding_configs_->Get(customer_id, project_id, encoding_config_id);
}

const Metric* ClientConfig::Metric(uint32_t customer_id, uint32_t project_id,
                                   uint32_t metric_id) {
  return metrics_->Get(customer_id, project_id, metric_id);
}

const Metric* ClientConfig::Metric(uint32_t customer_id, uint32_t project_id,
                                   const std::string& metric_name) {
  return metrics_->Get(customer_id, project_id, metric_name);
}

ClientConfig::ClientConfig(
    std::shared_ptr<config::EncodingRegistry> encoding_configs,
    std::shared_ptr<config::MetricRegistry> metrics)
    : encoding_configs_(std::move(encoding_configs)),
      metrics_(std::move(metrics)) {
  CHECK(metrics_);
  CHECK(encoding_configs_);
  is_empty_ = encoding_configs_->size() == 0 && metrics_->size() == 0;
  DetermineIfSingleProject();
}

void ClientConfig::DetermineIfSingleProject() {
  is_single_project_ = false;
  if (is_empty_) {
    return;
  }

  bool first = true;
  for (const class EncodingConfig& encoding_config : *encoding_configs_) {
    if (first) {
      first = false;
      is_single_project_ = true;
      single_customer_id_ = encoding_config.customer_id();
      single_project_id_ = encoding_config.project_id();
    } else if (encoding_config.customer_id() != single_customer_id_ ||
               encoding_config.project_id() != single_project_id_) {
      is_single_project_ = false;
      return;
    }
  }
  for (const class Metric& metric : *metrics_) {
    if (first) {
      first = false;
      is_single_project_ = true;
      single_customer_id_ = metric.customer_id();
      single_project_id_ = metric.project_id();
    } else if (metric.customer_id() != single_customer_id_ ||
               metric.project_id() != single_project_id_) {
      is_single_project_ = false;
      return;
    }
  }
}

}  // namespace config
}  // namespace cobalt

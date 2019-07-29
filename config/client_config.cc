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

std::unique_ptr<ClientConfig> ClientConfig::CreateFromCobaltRegistryProto(
    std::unique_ptr<CobaltRegistry> cobalt_registry) {
  RegisteredEncodings registered_encodings;
  registered_encodings.mutable_element()->Swap(cobalt_registry->mutable_encoding_configs());
  auto encoding_registry_and_status = EncodingRegistry::TakeFrom(&registered_encodings, nullptr);
  if (encoding_registry_and_status.second != config::kOK) {
    LOG(ERROR) << "Invalid EncodingConfigs. " << ErrorMessage(encoding_registry_and_status.second);
    return std::unique_ptr<ClientConfig>(nullptr);
  }

  RegisteredMetrics registered_metrics;
  registered_metrics.mutable_element()->Swap(cobalt_registry->mutable_metric_configs());
  auto metrics_registry_and_status = MetricRegistry::TakeFrom(&registered_metrics, nullptr);
  if (metrics_registry_and_status.second != config::kOK) {
    LOG(ERROR) << "Error getting Metrics from registry. "
               << ErrorMessage(metrics_registry_and_status.second);
    return std::unique_ptr<ClientConfig>(nullptr);
  }

  return std::make_unique<ClientConfig>(std::move(encoding_registry_and_status.first),
                                        std::move(metrics_registry_and_status.first));
}

const EncodingConfig* ClientConfig::EncodingConfig(uint32_t customer_id, uint32_t project_id,
                                                   uint32_t encoding_config_id) {
  return encoding_configs_->Get(customer_id, project_id, encoding_config_id);
}

const Metric* ClientConfig::Metric(uint32_t customer_id, uint32_t project_id, uint32_t metric_id) {
  return metrics_->Get(customer_id, project_id, metric_id);
}

const Metric* ClientConfig::Metric(uint32_t customer_id, uint32_t project_id,
                                   const std::string& metric_name) {
  return metrics_->Get(customer_id, project_id, metric_name);
}

ClientConfig::ClientConfig(std::shared_ptr<config::EncodingRegistry> encoding_configs,
                           std::shared_ptr<config::MetricRegistry> metrics)
    : encoding_configs_(std::move(encoding_configs)), metrics_(std::move(metrics)) {
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

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <string>
#include <utility>

#include "config/validation/valid_cobalt_config.h"
#include "third_party/tensorflow_statusor/statusor.h"

namespace cobalt {
namespace config {
namespace validation {

StatusOr<ValidCobaltRegistry> ValidCobaltRegistry::GetValidCobaltRegistry(
    std::unique_ptr<CobaltRegistry> cfg) {
  ValidCobaltRegistry config(std::move(cfg));

  if (config.config_->encoding_configs_size() == 0 &&
      config.config_->metric_configs_size() == 0 &&
      config.config_->report_configs_size() == 0) {
    return util::Status(util::StatusCode::INVALID_ARGUMENT,
                        "The config is empty. This is probably not desired.");
  }

  return config;
}

ValidCobaltRegistry::ValidCobaltRegistry(std::unique_ptr<CobaltRegistry> cfg)
    : config_(std::move(cfg)) {}

}  // namespace validation
}  // namespace config
}  // namespace cobalt

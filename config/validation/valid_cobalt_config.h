// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CONFIG_VALIDATION_VALID_COBALT_CONFIG_H_
#define COBALT_CONFIG_VALIDATION_VALID_COBALT_CONFIG_H_

#include <memory>

#include "config/cobalt_registry.pb.h"
#include "third_party/statusor/statusor.h"

namespace cobalt {
namespace config {
namespace validation {

using statusor::StatusOr;

// This represents a validated CobaltRegistry object. If the StatusOr returned
// from GetValidCobaltRegistry is a ValidCobaltRegistry then the provided
// CobaltRegistry is guaranteed to be valid.
class ValidCobaltRegistry {
 public:
  // GetValidCobaltRegistry attempts to construct a ValidCobaltRegistry object
  // using the supplied CobaltRegistry (|cfg|). If it runs into any validation
  // errors, it returns a util::Status with the validation error, otherwise it
  // returns the ValidCobaltRegistry object.
  static StatusOr<ValidCobaltRegistry> GetValidCobaltRegistry(
      std::unique_ptr<CobaltRegistry> cfg);

  const std::unique_ptr<CobaltRegistry> &config() const { return config_; }

 private:
  explicit ValidCobaltRegistry(std::unique_ptr<CobaltRegistry> cfg);

  std::unique_ptr<CobaltRegistry> config_;
};

}  // namespace validation
}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_VALIDATION_VALID_COBALT_CONFIG_H_

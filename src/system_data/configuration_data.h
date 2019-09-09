// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_SYSTEM_DATA_CONFIGURATION_DATA_H_
#define COBALT_SRC_SYSTEM_DATA_CONFIGURATION_DATA_H_

#include <iostream>
#include <string>

namespace cobalt::config {

// The environment that the Cobalt system should talk to.
enum Environment {
  // The default prod environment.
  PROD = 0,

  // The devel environment.
  DEVEL = 1
};

// Convert the Environment enum to a string (for logging).
const char* EnvironmentString(const Environment& environment);

// Streams EnvironmentString(environment) to `os`.
std::ostream& operator<<(std::ostream& os, Environment environment);

// Encapsulation of the configuration data used by Cobalt.
class ConfigurationData {
 public:
  explicit ConfigurationData(Environment environment) : environment_(environment) {}
  ~ConfigurationData() = default;

  // Get the environment that the Cobalt system should talk to.
  [[nodiscard]] Environment GetEnvironment() const { return environment_; }

  // Get a string of the environment that the Cobalt system should talk to (for logging).
  [[nodiscard]] const char* GetEnvironmentString() const { return EnvironmentString(environment_); }

  // Get the Clearcut Log Source ID that Cobalt should write its logs to.
  [[nodiscard]] int32_t GetLogSourceId() const;

 private:
  const Environment environment_;
};

// The current default configuration if no environment/config is specified.
static const ConfigurationData defaultConfigurationData(Environment::DEVEL);

}  // namespace cobalt::config

#endif  // COBALT_SRC_SYSTEM_DATA_CONFIGURATION_DATA_H_

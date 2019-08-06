// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_REGISTRY_ANALYZER_CONFIG_MANAGER_H_
#define COBALT_SRC_REGISTRY_ANALYZER_CONFIG_MANAGER_H_

#include <memory>
#include <mutex>
#include <string>

#include "src/registry/analyzer_config.h"

namespace cobalt {
namespace config {

// AnalyzerConfigManager vends shared pointers to an AnalyzerConfig.
// The purpose of this class is to be able to update the configuration data
// pointers to which it vends.
class AnalyzerConfigManager {
 public:
  // Get a pointer to the current analyzer config. Do not cache.
  std::shared_ptr<AnalyzerConfig> GetCurrent();

  // Updates the cached configuration from the external Git repository specified
  // in the constructor. This may block for up to |timeout_seconds| seconds.
  // Returns true if the update operation succeeded. Otherwise the previous
  // cached configuration is maintained.
  bool Update(unsigned int timeout_seconds);

  static std::shared_ptr<AnalyzerConfigManager> CreateFromFlagsOrDie();

  // This constructor is to be used when parameters related to updating the
  // configuration are unnecessary because you don't intend to update the
  // config. (such as in tests.)
  explicit AnalyzerConfigManager(std::shared_ptr<AnalyzerConfig> config);

 private:
  // Constructor.
  // |config| is the initial configuration to be held.
  // |cobalt_registry_proto_path| is the path on disk where the serialized
  // CobaltRegistry is to be stored.
  // |config_update_repository_url| is the url for a git repository containing
  // cobalt configuration information. It is parsed using config_parser. See
  // the documentation for config_parser to understand the format of the repo.
  // |config_parser_bin_path| is the path to the config_parser binary.
  AnalyzerConfigManager(std::shared_ptr<AnalyzerConfig> config,
                        std::string cobalt_registry_proto_path,
                        std::string config_update_repository_url,
                        std::string config_parser_bin_path);

  // Reads the configuration from a file containing a serialized CobaltRegistry.
  static std::unique_ptr<AnalyzerConfig> ReadConfigFromSerializedCobaltRegistryFile(
      std::string config_path);

  std::shared_ptr<AnalyzerConfig> ptr_;
  // This mutex protects ptr_. A thread dereferencing or updating ptr_ should
  // grab a lock on m_ first.
  std::mutex m_;
  const std::string cobalt_registry_proto_path_;
  const std::string update_repository_path_;
  const std::string config_parser_bin_path_;
};

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_SRC_REGISTRY_ANALYZER_CONFIG_MANAGER_H_

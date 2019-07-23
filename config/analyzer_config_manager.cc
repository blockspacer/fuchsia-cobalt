// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config/analyzer_config_manager.h"

#include <errno.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "config/analyzer_config.h"
#include "config/cobalt_registry.pb.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "util/log_based_metrics.h"

namespace cobalt {
namespace config {

DEFINE_string(cobalt_registry_proto_path, "",
              "Location on disk of the serialized CobaltRegistry proto from "
              "which the Report Master's configuration is to be read.");
DEFINE_string(config_update_repository_url, "",
              "URL to a git repository containing a cobalt configuration in "
              "its master branch. If this flag is set, the configuration of "
              "report master will be updated by pulling from the specified "
              "repository before scheduled reports are run. "
              "(e.g. \"https://cobalt-analytics.googlesource.com/config/\")");
DEFINE_string(config_parser_bin_path, "/usr/local/bin/config_parser",
              "Location on disk of the configuration parser.");

// Stackdriver metric constants
namespace {
constexpr char kUpdateFailure[] = "analyzer-config-manager-update-failure";
constexpr char kReadConfigFromCobaltRegistryFileFailure[] =
    "analyzer-config-manager-read-config-from-cobalt-config-file-failure";
}  // namespace

AnalyzerConfigManager::AnalyzerConfigManager(std::shared_ptr<AnalyzerConfig> config,
                                             std::string cobalt_registry_proto_path,
                                             std::string config_update_repository_url,
                                             std::string config_parser_bin_path)
    : cobalt_registry_proto_path_(cobalt_registry_proto_path),
      update_repository_path_(config_update_repository_url),
      config_parser_bin_path_(config_parser_bin_path) {
  ptr_ = config;
}

AnalyzerConfigManager::AnalyzerConfigManager(std::shared_ptr<AnalyzerConfig> config) {
  ptr_ = config;
}

std::shared_ptr<AnalyzerConfig> AnalyzerConfigManager::GetCurrent() {
  std::lock_guard<std::mutex> lock(m_);
  return ptr_;
}

std::shared_ptr<AnalyzerConfigManager> AnalyzerConfigManager::CreateFromFlagsOrDie() {
  if (FLAGS_cobalt_registry_proto_path.empty()) {
    auto config = AnalyzerConfig::CreateFromFlagsOrDie();
    return std::shared_ptr<AnalyzerConfigManager>(new AnalyzerConfigManager(std::move(config)));
  }

  // If a file containing a serialized CobaltRegistry is specified, we load the
  // initial configuration from that file.
  auto config = ReadConfigFromSerializedCobaltRegistryFile(FLAGS_cobalt_registry_proto_path);
  if (!config) {
    LOG(FATAL) << "Could not load the initial configuration.";
  }
  LOG(INFO) << "Initial configuration loaded.";

  auto manager = std::shared_ptr<AnalyzerConfigManager>(
      new AnalyzerConfigManager(std::move(config), FLAGS_cobalt_registry_proto_path,
                                FLAGS_config_update_repository_url, FLAGS_config_parser_bin_path));
  manager->Update(120);
  return manager;
}

bool AnalyzerConfigManager::Update(unsigned int timeout_seconds) {
  // If no repository to get updates from was specified, skip the update.
  if (update_repository_path_.empty()) {
    return false;
  }

  LOG(INFO) << "Updating configuration from " << update_repository_path_;

  std::string timeout_seconds_str = std::to_string(timeout_seconds);
  const char* argv[] = {
      config_parser_bin_path_.c_str(),     "-repo_url",
      update_repository_path_.c_str(),     "-output_file",
      cobalt_registry_proto_path_.c_str(), "-git_timeout",
      timeout_seconds_str.c_str(),         nullptr,
  };
  pid_t pid;
  posix_spawnattr_t spawnattr;
  posix_spawnattr_init(&spawnattr);

  errno = 0;
  // The variable "environ" is the current environment found in unistd.h.
  int status = posix_spawn(&pid, config_parser_bin_path_.c_str(), nullptr, &spawnattr,
                           const_cast<char* const*>(argv), environ);

  if (0 != status) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kUpdateFailure)
        << "Error spawning config_parser at " << config_parser_bin_path_
        << " with error: " << strerror(errno);
    return false;
  }
  LOG(INFO) << "Spawned " << config_parser_bin_path_;

  // Catch state changes of config_parser process until it terminates.
  errno = 0;
  if (waitpid(pid, &status, WUNTRACED) != pid) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kUpdateFailure)
        << "Error waiting for config_parser: " << strerror(errno);
    return false;
  }

  if (!WIFEXITED(status) || 0 != WEXITSTATUS(status)) {
    if (WIFSIGNALED(status)) {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kUpdateFailure)
          << "config_parser terminated by signal " << WTERMSIG(status);
      return false;
    } else if (WIFEXITED(status)) {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kUpdateFailure)
          << "config_parser exited with signal " << WEXITSTATUS(status);
      return false;
    } else if (WIFSTOPPED(status)) {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kUpdateFailure)
          << "config_parser was stopped by signal " << WSTOPSIG(status);
      return false;
    } else {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kUpdateFailure)
          << "Error while waiting for config_parser.";
      return false;
    }
  }
  LOG(INFO) << "Done getting updated configuration from " << update_repository_path_;

  auto config = ReadConfigFromSerializedCobaltRegistryFile(cobalt_registry_proto_path_);
  std::lock_guard<std::mutex> lock(m_);
  ptr_.reset(config.release());

  LOG(INFO) << "Configuration updated.";
  return true;
}

std::unique_ptr<AnalyzerConfig> AnalyzerConfigManager::ReadConfigFromSerializedCobaltRegistryFile(
    std::string config_path) {
  std::ifstream config_file_stream;
  config_file_stream.open(config_path);
  if (!config_file_stream) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kReadConfigFromCobaltRegistryFileFailure)
        << "Could not open config proto: " << config_path;
    return nullptr;
  }

  CobaltRegistry cobalt_config;
  if (!cobalt_config.ParseFromIstream(&config_file_stream)) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kReadConfigFromCobaltRegistryFileFailure)
        << "Could not parse config proto: " << config_path;
    return nullptr;
  }

  auto config = AnalyzerConfig::CreateFromCobaltRegistryProto(&cobalt_config);
  if (!config) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kReadConfigFromCobaltRegistryFileFailure)
        << "Error creating AnalyzerConfig: " << config_path;
    return nullptr;
  }

  return config;
}

}  // namespace config
}  // namespace cobalt

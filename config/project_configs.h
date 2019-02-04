// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CONFIG_PROJECT_CONFIGS_H_
#define COBALT_CONFIG_PROJECT_CONFIGS_H_

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "config/cobalt_registry.pb.h"

namespace cobalt {
namespace config {

// ProjectConfigs wraps a CobaltRegistry and offers convenient and efficient
// methods for looking up a project.
class ProjectConfigs {
 public:
  // Constructs and returns an instance of ProjectConfigs by first parsing
  // a CobaltRegistry proto message from |cobalt_registry_base64|, which should
  // contain the Base64 encoding of the bytes of the binary serialization of
  // such a message.
  static std::unique_ptr<ProjectConfigs> CreateFromCobaltRegistryBase64(
      const std::string& cobalt_registry_base64);

  // Constructs and returns an instance of ProjectConfigs by first parsing
  // a CobaltRegistry proto message from |cobalt_config_bytes|, which should
  // contain the bytes of the binary serialization of such a message.
  static std::unique_ptr<ProjectConfigs> CreateFromCobaltRegistryBytes(
      const std::string& cobalt_config_bytes);

  // Alias of CreateFromCobaltRegistryBytes. (DEPRECATED)
  // TODO(azani): Delete when all callsites are updated.
  static std::unique_ptr<ProjectConfigs> CreateFromCobaltConfigBytes(
      const std::string& cobalt_config_bytes);

  // Constructs and returns and instance of ProjectConfigs from |cobalt_config|.
  static std::unique_ptr<ProjectConfigs> CreateFromCobaltRegistryProto(
      std::unique_ptr<CobaltRegistry> cobalt_config);

  // Constructs a ProjectConfigs that wraps the given |cobalt_config|.
  explicit ProjectConfigs(std::unique_ptr<CobaltRegistry> cobalt_config);

  // Returns the CustomerConfig for the customer with the given name, or
  // nullptr if there is no such customer.
  const CustomerConfig* GetCustomerConfig(
      const std::string& customer_name) const;

  // Returns the CustomerConfig for the customer with the given ID, or
  // nullptr if there is no such customer.
  const CustomerConfig* GetCustomerConfig(uint32_t customer_id) const;

  // Returns the ProjectConfig for the project with the given
  // (customer_name, project_name), or nullptr if there is no such project.
  const ProjectConfig* GetProjectConfig(const std::string& customer_name,
                                        const std::string& project_name) const;

  // Returns the ProjectConfig for the project with the given
  // (customer_id, project_id), or nullptr if there is no such project.
  const ProjectConfig* GetProjectConfig(uint32_t customer_id,
                                        uint32_t project_id) const;

  // Returns the MetricDefinition for the metric with the given
  // (customer_id, project_id, metric_id), or nullptr if no such metric exists.
  const MetricDefinition* GetMetricDefinition(uint32_t customer_id,
                                              uint32_t project_id,
                                              uint32_t metric_id) const;

  // Returns the ReportDefinition for the metric with the given
  // (customer_id, project_id, metric_id, report_id), or nullptr if no such
  // report exists.
  const ReportDefinition* GetReportDefinition(uint32_t customer_id,
                                              uint32_t project_id,
                                              uint32_t metric_id,
                                              uint32_t report_id) const;

 private:
  std::unique_ptr<CobaltRegistry> cobalt_config_;

  std::map<std::string, const CustomerConfig*> customers_by_name_;

  std::map<uint32_t, const CustomerConfig*> customers_by_id_;

  std::map<std::tuple<std::string, std::string>, const ProjectConfig*>
      projects_by_name_;

  std::map<std::tuple<uint32_t, uint32_t>, const ProjectConfig*>
      projects_by_id_;

  std::map<std::tuple<uint32_t, uint32_t, uint32_t>, const MetricDefinition*>
      metrics_by_id_;

  std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>,
           const ReportDefinition*>
      reports_by_id_;
};

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_PROJECT_CONFIGS_H_

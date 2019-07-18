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

// ProjectConfigs provides a convenient interface over a |CobaltRegistry|
// that is intended to be used on Cobalt's client-side.
//
// A CobaltRegistry can be in one of three states:
//
// (1) It can contain data for a single Cobalt 0.1 project.
// (2) It can contain data for a single Cobalt 1.0 project.
// (3) It can contain data for multiple Cobalt projects. In this case it
//     may contain data for some Cobalt 0.1 projects and some Cobalt 1.0
//     projects.
//
// This class is part of Cobalt 1.0. and as such it ignores the Cobalt 0.1
// data in a |CobaltRegistry| and gives access only to the Cobalt 1.0 data.
// So an instance of this class can be in one of three states corresponding
// to the three states above:
//
// (1) It can be empty
// (2) It can contain data for a single Cobalt 1.0 project.
// (3) It can contain data for multiple Cobalt 1.0 projects.
//
// See the class |ClientConfig| for the Cobalt 0.1 analogue of this class.
class ProjectConfigs {
 public:
  // Constructs and returns an instance of ProjectConfigs by first parsing
  // a CobaltRegistry proto message from |cobalt_registry_base64|, which should
  // contain the Base64 encoding of the bytes of the binary serialization of
  // such a message.
  //
  // Returns nullptr to indicate failure.
  static std::unique_ptr<ProjectConfigs> CreateFromCobaltRegistryBase64(
      const std::string& cobalt_registry_base64);

  // Constructs and returns an instance of ProjectConfigs by first parsing
  // a CobaltRegistry proto message from |cobalt_registry_bytes|, which should
  // contain the bytes of the binary serialization of such a message.
  //
  // Returns nullptr to indicate failure.
  static std::unique_ptr<ProjectConfigs> CreateFromCobaltRegistryBytes(
      const std::string& cobalt_registry_bytes);

  // Constructs and returns and instance of ProjectConfigs that contains the
  // data from |cobalt_registry|.
  static std::unique_ptr<ProjectConfigs> CreateFromCobaltRegistryProto(
      std::unique_ptr<CobaltRegistry> cobalt_registry);

  // Constructs a ProjectConfigs that contains the data from |cobalt_registry|.
  explicit ProjectConfigs(std::unique_ptr<CobaltRegistry> cobalt_registry);

  // Returns the CustomerConfig for the customer with the given name, or
  // nullptr if there is no such customer.
  [[nodiscard]] const CustomerConfig* GetCustomerConfig(
      const std::string& customer_name) const;

  // Returns the CustomerConfig for the customer with the given ID, or
  // nullptr if there is no such customer.
  [[nodiscard]] const CustomerConfig* GetCustomerConfig(
      uint32_t customer_id) const;

  // Returns the ProjectConfig for the project with the given
  // (customer_name, project_name), or nullptr if there is no such project.
  [[nodiscard]] const ProjectConfig* GetProjectConfig(
      const std::string& customer_name, const std::string& project_name) const;

  // Returns the ProjectConfig for the project with the given
  // (customer_id, project_id), or nullptr if there is no such project.
  [[nodiscard]] const ProjectConfig* GetProjectConfig(
      uint32_t customer_id, uint32_t project_id) const;

  // Returns the MetricDefinition for the metric with the given
  // (customer_id, project_id, metric_id), or nullptr if no such metric exists.
  [[nodiscard]] const MetricDefinition* GetMetricDefinition(
      uint32_t customer_id, uint32_t project_id, uint32_t metric_id) const;

  // Returns the ReportDefinition for the metric with the given
  // (customer_id, project_id, metric_id, report_id), or nullptr if no such
  // report exists.
  [[nodiscard]] const ReportDefinition* GetReportDefinition(
      uint32_t customer_id, uint32_t project_id, uint32_t metric_id,
      uint32_t report_id) const;

  // Returns whether or not this instance of ProjectConfigs contains data for
  // exactly one project.
  bool is_single_project() { return is_single_project_; }

  // Returns whether or not this instance of ProjectConfigs contains no
  // project data.
  bool is_empty() { return is_empty_; }

  // If is_single_project() is true then this returns the customer ID of
  // the single project. Otherwise the return value is undefined.
  uint32_t single_customer_id() { return single_customer_id_; }

  // If is_single_project() is true then this returns the customer name of
  // the single project. Otherwise the return value is undefined.
  std::string single_customer_name() { return single_customer_name_; }

  // If is_single_project() is true then this returns the project ID of
  // the single project. Otherwise the return value is undefined.
  uint32_t single_project_id() { return single_project_id_; }

  // If is_single_project() is true then this returns the project name of
  // the single project. Otherwise the return value is undefined.
  std::string single_project_name() { return single_project_name_; }

  // If is_single_project() is true then this removes and returns the single
  // ProjectConfig from the data owned by this object leaving this object
  // empty. Otherwise returns nullptr.
  std::unique_ptr<ProjectConfig> TakeSingleProjectConfig();

 private:
  std::unique_ptr<CobaltRegistry> cobalt_registry_;

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

  bool is_single_project_;
  bool is_empty_;
  uint32_t single_customer_id_ = 0;
  std::string single_customer_name_;
  uint32_t single_project_id_ = 0;
  std::string single_project_name_;
};

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_PROJECT_CONFIGS_H_

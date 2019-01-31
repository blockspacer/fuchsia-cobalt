// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config/project_configs.h"

#include <sstream>

#include "./logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/crypto_util/base64.h"

namespace cobalt {
namespace config {

namespace {

const size_t kNumReportsPerMetric = 3;
const size_t kNumMetricsPerProject = 5;
const size_t kNumCustomers = 2;

std::string NameForId(uint32_t id) {
  std::ostringstream stream;
  stream << "Name" << id;
  return stream.str();
}

// We create 3n projects for customer n.
size_t NumProjectsForCustomer(uint32_t customer_id) { return 3 * customer_id; }

void SetupReport(uint32_t report_id, ReportDefinition* report) {
  report->set_id(report_id);
  report->set_report_name(NameForId(report_id));
}

void SetupMetric(uint32_t metric_id, MetricDefinition* metric) {
  metric->set_id(metric_id);
  metric->set_metric_name(NameForId(metric_id));
  for (size_t i = 1u; i < kNumReportsPerMetric; i++) {
    SetupReport(i, metric->add_reports());
  }
}

void SetupProject(uint32_t project_id, ProjectConfig* project) {
  project->set_project_id(project_id);
  project->set_project_name(NameForId(project_id));
  for (size_t i = 1u; i <= kNumMetricsPerProject; i++) {
    SetupMetric(i, project->add_metrics());
  }
}

void SetupCustomer(uint32_t customer_id, size_t num_projects,
                   CustomerConfig* customer) {
  customer->set_customer_id(customer_id);
  customer->set_customer_name(NameForId(customer_id));
  for (auto i = 1u; i <= num_projects; i++) {
    SetupProject(i, customer->add_projects());
  }
}

std::unique_ptr<CobaltRegistry> NewTestConfig() {
  auto cobalt_config = std::make_unique<CobaltRegistry>();
  for (size_t i = 1u; i <= kNumCustomers; i++) {
    SetupCustomer(i, NumProjectsForCustomer(i), cobalt_config->add_customers());
  }
  return cobalt_config;
}

}  // namespace

class ProjectConfigsTest : public ::testing::Test {
 protected:
  // Checks that |*customer_config| is as expected.
  bool CheckCustomer(uint32_t expected_customer_id,
                     const CustomerConfig* customer_config) {
    EXPECT_NE(nullptr, customer_config);
    if (customer_config == nullptr) {
      return false;
    }

    auto customer_id = customer_config->customer_id();
    EXPECT_EQ(expected_customer_id, customer_id);
    if (expected_customer_id != customer_id) {
      return false;
    }
    EXPECT_EQ(NameForId(expected_customer_id),
              customer_config->customer_name());
    if (NameForId(expected_customer_id) != customer_config->customer_name()) {
      return false;
    }
    size_t expected_num_projects = NumProjectsForCustomer(customer_id);
    size_t num_projects = customer_config->projects_size();
    EXPECT_EQ(expected_num_projects, num_projects);
    return num_projects == expected_num_projects;
  }

  // Checks that |*project_config| is as expected.
  bool CheckProject(uint32_t expected_project_id,
                    const ProjectConfig* project_config) {
    EXPECT_NE(nullptr, project_config);
    if (project_config == nullptr) {
      return false;
    }

    EXPECT_EQ(expected_project_id, project_config->project_id());
    if (expected_project_id != project_config->project_id()) {
      return false;
    }
    EXPECT_EQ(NameForId(expected_project_id), project_config->project_name());
    if (NameForId(expected_project_id) != project_config->project_name()) {
      return false;
    }
    size_t num_metrics = project_config->metrics_size();
    EXPECT_EQ(kNumMetricsPerProject, num_metrics);
    return num_metrics == kNumMetricsPerProject;
  }

  // Checks that |project_configs| is as expected.
  bool CheckProjectConfigs(const ProjectConfigs& project_configs) {
    for (uint32_t customer_id = 1; customer_id <= kNumCustomers;
         customer_id++) {
      std::string expected_customer_name = NameForId(customer_id);
      size_t expected_num_projects = NumProjectsForCustomer(customer_id);

      // Check getting the customer by name.
      bool success = CheckCustomer(
          customer_id,
          project_configs.GetCustomerConfig(expected_customer_name));
      EXPECT_TRUE(success);
      if (!success) {
        return false;
      }

      // Check getting the customer by ID.
      success = CheckCustomer(customer_id,
                              project_configs.GetCustomerConfig(customer_id));
      EXPECT_TRUE(success);
      if (!success) {
        return false;
      }

      for (uint32_t project_id = 1; project_id <= expected_num_projects;
           project_id++) {
        std::string project_name = NameForId(project_id);

        // Check getting the project by name.
        bool success =
            CheckProject(project_id, project_configs.GetProjectConfig(
                                         expected_customer_name, project_name));
        EXPECT_TRUE(success);
        if (!success) {
          return false;
        }

        // Check getting the project by ID.
        success = CheckProject(project_id, project_configs.GetProjectConfig(
                                               customer_id, project_id));
        EXPECT_TRUE(success);
        if (!success) {
          return false;
        }

        // Check using an invalid project name
        auto* project = project_configs.GetProjectConfig(expected_customer_name,
                                                         "InvalidName");
        EXPECT_EQ(nullptr, project);
        if (project != nullptr) {
          return false;
        }

        // Check using an invalid project_id.
        project = project_configs.GetProjectConfig(
            customer_id, expected_num_projects + project_id);
        EXPECT_EQ(nullptr, project);
        if (project != nullptr) {
          return false;
        }
      }
    }
    return true;
  }
};

// Test GetCustomerConfig by id.
TEST_F(ProjectConfigsTest, GetCustomerConfigById) {
  ProjectConfigs project_configs(NewTestConfig());
  const CustomerConfig* customer;

  customer = project_configs.GetCustomerConfig(1);
  EXPECT_NE(customer, nullptr);
  EXPECT_EQ(customer->customer_id(), 1u);

  customer = project_configs.GetCustomerConfig(2);
  EXPECT_NE(customer, nullptr);
  EXPECT_EQ(customer->customer_id(), 2u);

  // Customer does not exist.
  customer = project_configs.GetCustomerConfig(20);
  EXPECT_EQ(customer, nullptr);
}

// Test GetProjectConfig by id.
TEST_F(ProjectConfigsTest, GetProjectConfigById) {
  ProjectConfigs project_configs(NewTestConfig());
  const ProjectConfig* project;

  project = project_configs.GetProjectConfig(1, 1);
  EXPECT_NE(project, nullptr);
  EXPECT_EQ(project->project_id(), 1u);

  project = project_configs.GetProjectConfig(1, 2);
  EXPECT_NE(project, nullptr);
  EXPECT_EQ(project->project_id(), 2u);

  // Customer does not exist.
  project = project_configs.GetProjectConfig(20, 2);
  EXPECT_EQ(project, nullptr);

  // Customer exists, project does not exist.
  project = project_configs.GetProjectConfig(1, 20);
  EXPECT_EQ(project, nullptr);
}

// Test GetMetricDefintion.
TEST_F(ProjectConfigsTest, GetMetricDefinitionById) {
  ProjectConfigs project_configs(NewTestConfig());
  const MetricDefinition* metric;

  metric = project_configs.GetMetricDefinition(1, 1, 1);
  EXPECT_NE(metric, nullptr);
  EXPECT_EQ(metric->id(), 1u);

  metric = project_configs.GetMetricDefinition(1, 1, 2);
  EXPECT_NE(metric, nullptr);
  EXPECT_EQ(metric->id(), 2u);

  // Customer does not exist.
  metric = project_configs.GetMetricDefinition(20, 1, 2);
  EXPECT_EQ(metric, nullptr);

  // Customer exists, project does not exist.
  metric = project_configs.GetMetricDefinition(1, 20, 2);
  EXPECT_EQ(metric, nullptr);

  // Customer exists, project exists, metric does not exist.
  metric = project_configs.GetMetricDefinition(1, 1, 20);
  EXPECT_EQ(metric, nullptr);
}

// Test GetReportDefinition.
TEST_F(ProjectConfigsTest, GetReportDefinitionById) {
  ProjectConfigs project_configs(NewTestConfig());
  const ReportDefinition* report;

  report = project_configs.GetReportDefinition(1, 1, 1, 1);
  EXPECT_NE(report, nullptr);
  EXPECT_EQ(report->id(), 1u);

  report = project_configs.GetReportDefinition(1, 1, 1, 2);
  EXPECT_NE(report, nullptr);
  EXPECT_EQ(report->id(), 2u);

  // Customer does not exist.
  report = project_configs.GetReportDefinition(20, 1, 2, 2);
  EXPECT_EQ(report, nullptr);

  // Customer exists, project does not exist.
  report = project_configs.GetReportDefinition(1, 20, 2, 2);
  EXPECT_EQ(report, nullptr);

  // Customer exists, project exists, metric does not exist.
  report = project_configs.GetReportDefinition(1, 1, 20, 2);
  EXPECT_EQ(report, nullptr);

  // Customer exists, project exists, metric exist, report does not exist.
  report = project_configs.GetReportDefinition(1, 1, 1, 20);
  EXPECT_EQ(report, nullptr);
}

// Tests using a ProjectConfigs constructed directly from a
// CobaltRegistry
TEST_F(ProjectConfigsTest, ConstructForCobaltRegistry) {
  ProjectConfigs project_configs(NewTestConfig());
  EXPECT_TRUE(CheckProjectConfigs(project_configs));
}

// Tests using a ProjectConfigs obtained via CreateFromCobaltRegistryBytes().
TEST_F(ProjectConfigsTest, CreateFromCobaltRegistryBytes) {
  auto cobalt_config = NewTestConfig();
  std::string bytes;
  cobalt_config->SerializeToString(&bytes);
  auto project_configs = ProjectConfigs::CreateFromCobaltRegistryBytes(bytes);
  EXPECT_TRUE(CheckProjectConfigs(*project_configs));
}

// Tests using a ProjectConfigs obtained via CreateFromCobaltRegistryBase64().
TEST_F(ProjectConfigsTest, CreateFromCobaltRegistryBase64) {
  auto cobalt_config = NewTestConfig();
  std::string bytes;
  cobalt_config->SerializeToString(&bytes);
  std::string cobalt_registry_base64;
  crypto::Base64Encode(bytes, &cobalt_registry_base64);
  auto project_configs =
      ProjectConfigs::CreateFromCobaltRegistryBase64(cobalt_registry_base64);
  EXPECT_TRUE(CheckProjectConfigs(*project_configs));
}

}  // namespace config
}  // namespace cobalt

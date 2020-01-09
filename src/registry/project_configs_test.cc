// Copyright 2018 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/registry/project_configs.h"

#include <sstream>

#include "src/logging.h"
#include "third_party/abseil-cpp/absl/strings/escaping.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::config {

namespace {

constexpr size_t kNumReportsPerMetric = 3;
constexpr size_t kNumMetricsPerProject = 5;
constexpr size_t kNumCustomers = 2;
constexpr uint32_t kNonExistent = 20;

std::string NameForId(const std::string& prefix, uint32_t id) {
  std::ostringstream stream;
  stream << prefix << "Name" << id;
  return stream.str();
}

std::string CustomerNameForId(uint32_t id) { return NameForId("Customer-", id); }
std::string ProjectNameForId(uint32_t id) { return NameForId("Project-", id); }
std::string ReportNameForId(uint32_t id) { return NameForId("Report-", id); }
std::string MetricNameForId(uint32_t id) { return NameForId("Metric-", id); }

// We create 3n projects for customer n.
size_t NumProjectsForCustomer(uint32_t customer_id) { return 3 * customer_id; }

void SetupReport(uint32_t report_id, ReportDefinition* report) {
  report->set_id(report_id);
  report->set_report_name(ReportNameForId(report_id));
}

void SetupMetric(uint32_t metric_id, MetricDefinition* metric) {
  metric->set_id(metric_id);
  metric->set_metric_name(MetricNameForId(metric_id));
  for (size_t i = 1u; i < kNumReportsPerMetric; i++) {
    SetupReport(i, metric->add_reports());
  }
}

void SetupProject(uint32_t project_id, ProjectConfig* project) {
  project->set_project_id(project_id);
  project->set_project_name(ProjectNameForId(project_id));
  for (size_t i = 1u; i <= kNumMetricsPerProject; i++) {
    SetupMetric(i, project->add_metrics());
  }
}

void SetupCustomer(uint32_t customer_id, size_t num_projects, CustomerConfig* customer) {
  customer->set_customer_id(customer_id);
  customer->set_customer_name(CustomerNameForId(customer_id));
  for (auto i = 1u; i <= num_projects; i++) {
    SetupProject(i, customer->add_projects());
  }
}

std::unique_ptr<CobaltRegistry> NewTestRegistry(
    size_t num_customers, const std::function<size_t(uint32_t)>& num_projects_for_customer_fn) {
  auto cobalt_registry = std::make_unique<CobaltRegistry>();
  for (size_t i = 1u; i <= num_customers; i++) {
    SetupCustomer(i, num_projects_for_customer_fn(i), cobalt_registry->add_customers());
  }
  return cobalt_registry;
}

std::unique_ptr<CobaltRegistry> NewTestRegistry() {
  return NewTestRegistry(kNumCustomers, NumProjectsForCustomer);
}

// Constructs a ProjectConfigs that wraps a CobaltRegistry with the
// specified number of customers and projects-per-customer.
std::unique_ptr<ProjectConfigs> NewProjectConfigs(size_t num_customers,
                                                  size_t num_projects_per_customer) {
  return std::make_unique<ProjectConfigs>(NewTestRegistry(
      num_customers,
      [num_projects_per_customer](uint32_t /*unused*/) { return num_projects_per_customer; }));
}

}  // namespace

class ProjectConfigsTest : public ::testing::Test {
 protected:
  // Checks that |*customer_config| is as expected.
  bool CheckCustomer(uint32_t expected_customer_id, const CustomerConfig* customer_config) {
    EXPECT_NE(nullptr, customer_config);
    if (customer_config == nullptr) {
      return false;
    }

    auto customer_id = customer_config->customer_id();
    EXPECT_EQ(expected_customer_id, customer_id);
    if (expected_customer_id != customer_id) {
      return false;
    }
    EXPECT_EQ(CustomerNameForId(expected_customer_id), customer_config->customer_name());
    if (CustomerNameForId(expected_customer_id) != customer_config->customer_name()) {
      return false;
    }
    size_t expected_num_projects = NumProjectsForCustomer(customer_id);
    size_t num_projects = customer_config->projects_size();
    EXPECT_EQ(expected_num_projects, num_projects);
    return num_projects == expected_num_projects;
  }

  // Checks that |*project_config| is as expected.
  bool CheckProject(uint32_t expected_project_id, const ProjectConfig* project_config) {
    EXPECT_NE(nullptr, project_config);
    if (project_config == nullptr) {
      return false;
    }

    EXPECT_EQ(expected_project_id, project_config->project_id());
    if (expected_project_id != project_config->project_id()) {
      return false;
    }
    EXPECT_EQ(ProjectNameForId(expected_project_id), project_config->project_name());
    if (ProjectNameForId(expected_project_id) != project_config->project_name()) {
      return false;
    }
    size_t num_metrics = project_config->metrics_size();
    EXPECT_EQ(kNumMetricsPerProject, num_metrics);
    return num_metrics == kNumMetricsPerProject;
  }

  // Checks that |project_configs| is as expected.
  bool CheckProjectConfigs(const ProjectConfigs& project_configs) {
    for (uint32_t customer_id = 1; customer_id <= kNumCustomers; customer_id++) {
      std::string expected_customer_name = CustomerNameForId(customer_id);
      size_t expected_num_projects = NumProjectsForCustomer(customer_id);

      // Check getting the customer by name.
      bool success =
          CheckCustomer(customer_id, project_configs.GetCustomerConfig(expected_customer_name));
      EXPECT_TRUE(success);
      if (!success) {
        return false;
      }

      // Check getting the customer by ID.
      success = CheckCustomer(customer_id, project_configs.GetCustomerConfig(customer_id));
      EXPECT_TRUE(success);
      if (!success) {
        return false;
      }

      for (uint32_t project_id = 1; project_id <= expected_num_projects; project_id++) {
        std::string project_name = ProjectNameForId(project_id);

        // Check getting the project by name.
        bool success = CheckProject(
            project_id, project_configs.GetProjectConfig(expected_customer_name, project_name));
        EXPECT_TRUE(success);
        if (!success) {
          return false;
        }

        // Check getting the project by ID.
        success =
            CheckProject(project_id, project_configs.GetProjectConfig(customer_id, project_id));
        EXPECT_TRUE(success);
        if (!success) {
          return false;
        }

        // Check using an invalid project name
        auto* project = project_configs.GetProjectConfig(expected_customer_name, "InvalidName");
        EXPECT_EQ(nullptr, project);
        if (project != nullptr) {
          return false;
        }

        // Check using an invalid project_id.
        project = project_configs.GetProjectConfig(customer_id, expected_num_projects + project_id);
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
  ProjectConfigs project_configs(NewTestRegistry());
  const CustomerConfig* customer;

  customer = project_configs.GetCustomerConfig(1);
  EXPECT_NE(customer, nullptr);
  EXPECT_EQ(customer->customer_id(), 1u);

  customer = project_configs.GetCustomerConfig(2);
  EXPECT_NE(customer, nullptr);
  EXPECT_EQ(customer->customer_id(), 2u);

  // Customer does not exist.
  customer = project_configs.GetCustomerConfig(kNonExistent);
  EXPECT_EQ(customer, nullptr);
}

// Test GetProjectConfig by id.
TEST_F(ProjectConfigsTest, GetProjectConfigById) {
  ProjectConfigs project_configs(NewTestRegistry());
  const ProjectConfig* project;

  project = project_configs.GetProjectConfig(1, 1);
  EXPECT_NE(project, nullptr);
  EXPECT_EQ(project->project_id(), 1u);

  project = project_configs.GetProjectConfig(1, 2);
  EXPECT_NE(project, nullptr);
  EXPECT_EQ(project->project_id(), 2u);

  // Customer does not exist.
  project = project_configs.GetProjectConfig(kNonExistent, 2);
  EXPECT_EQ(project, nullptr);

  // Customer exists, project does not exist.
  project = project_configs.GetProjectConfig(1, kNonExistent);
  EXPECT_EQ(project, nullptr);
}

// Test GetMetricDefintion.
TEST_F(ProjectConfigsTest, GetMetricDefinitionById) {
  ProjectConfigs project_configs(NewTestRegistry());
  const MetricDefinition* metric;

  metric = project_configs.GetMetricDefinition(1, 1, 1);
  EXPECT_NE(metric, nullptr);
  EXPECT_EQ(metric->id(), 1u);

  metric = project_configs.GetMetricDefinition(1, 1, 2);
  EXPECT_NE(metric, nullptr);
  EXPECT_EQ(metric->id(), 2u);

  // Customer does not exist.
  metric = project_configs.GetMetricDefinition(kNonExistent, 1, 2);
  EXPECT_EQ(metric, nullptr);

  // Customer exists, project does not exist.
  metric = project_configs.GetMetricDefinition(1, kNonExistent, 2);
  EXPECT_EQ(metric, nullptr);

  // Customer exists, project exists, metric does not exist.
  metric = project_configs.GetMetricDefinition(1, 1, kNonExistent);
  EXPECT_EQ(metric, nullptr);
}

// Test GetReportDefinition.
TEST_F(ProjectConfigsTest, GetReportDefinitionById) {
  ProjectConfigs project_configs(NewTestRegistry());
  const ReportDefinition* report;

  report = project_configs.GetReportDefinition(1, 1, 1, 1);
  EXPECT_NE(report, nullptr);
  EXPECT_EQ(report->id(), 1u);

  report = project_configs.GetReportDefinition(1, 1, 1, 2);
  EXPECT_NE(report, nullptr);
  EXPECT_EQ(report->id(), 2u);

  // Customer does not exist.
  report = project_configs.GetReportDefinition(kNonExistent, 1, 2, 2);
  EXPECT_EQ(report, nullptr);

  // Customer exists, project does not exist.
  report = project_configs.GetReportDefinition(1, kNonExistent, 2, 2);
  EXPECT_EQ(report, nullptr);

  // Customer exists, project exists, metric does not exist.
  report = project_configs.GetReportDefinition(1, 1, kNonExistent, 2);
  EXPECT_EQ(report, nullptr);

  // Customer exists, project exists, metric exist, report does not exist.
  report = project_configs.GetReportDefinition(1, 1, 1, kNonExistent);
  EXPECT_EQ(report, nullptr);
}

// Tests using a ProjectConfigs constructed directly from a
// CobaltRegistry
TEST_F(ProjectConfigsTest, ConstructForCobaltRegistry) {
  ProjectConfigs project_configs(NewTestRegistry());
  EXPECT_TRUE(CheckProjectConfigs(project_configs));
}

// Tests using a ProjectConfigs obtained via CreateFromCobaltRegistryBytes().
TEST_F(ProjectConfigsTest, CreateFromCobaltRegistryBytes) {
  auto cobalt_registry = NewTestRegistry();
  std::string bytes;
  cobalt_registry->SerializeToString(&bytes);
  auto project_configs = ProjectConfigs::CreateFromCobaltRegistryBytes(bytes);
  EXPECT_TRUE(CheckProjectConfigs(*project_configs));
}

// Tests using a ProjectConfigs obtained via CreateFromCobaltRegistryBase64().
TEST_F(ProjectConfigsTest, CreateFromCobaltRegistryBase64) {
  auto cobalt_registry = NewTestRegistry();
  std::string bytes;
  cobalt_registry->SerializeToString(&bytes);
  std::string cobalt_registry_base64;
  absl::Base64Escape(bytes, &cobalt_registry_base64);
  auto project_configs = ProjectConfigs::CreateFromCobaltRegistryBase64(cobalt_registry_base64);
  EXPECT_TRUE(CheckProjectConfigs(*project_configs));
}

// Tests the logic that determines whether or not a ProjectConfigs is empty
// or contains a single project.
TEST_F(ProjectConfigsTest, IsSingleProject) {
  // An ProjectConfigs constructed from an empty CobaltRegistry is empty but
  // is not a single project.
  auto project_configs = NewProjectConfigs(0, 0);
  EXPECT_FALSE(project_configs->is_single_project());
  EXPECT_TRUE(project_configs->is_empty());
  EXPECT_TRUE(project_configs->TakeSingleProjectConfig() == nullptr);

  // A ProjectConfigs constructed from a CobaltRegistry with 1 customer with
  // no projects is not empty and is not a single project.
  project_configs = NewProjectConfigs(1, 0);
  EXPECT_FALSE(project_configs->is_single_project());
  EXPECT_FALSE(project_configs->is_empty());
  EXPECT_TRUE(project_configs->TakeSingleProjectConfig() == nullptr);

  // A ProjectConfigs constructed from a CobaltRegistry with 1 customer with 1
  // project is not empty and is a single project.
  project_configs = NewProjectConfigs(1, 1);
  EXPECT_TRUE(project_configs->is_single_project());
  EXPECT_FALSE(project_configs->is_empty());
  EXPECT_EQ(1u, project_configs->single_customer_id());
  EXPECT_EQ("Customer-Name1", project_configs->single_customer_name());
  EXPECT_EQ(1u, project_configs->single_project_id());
  EXPECT_EQ("Project-Name1", project_configs->single_project_name());
  // Test TakeSingleProjectConfg() in the case that there is a single project.
  EXPECT_NE(project_configs->GetProjectConfig("Customer-Name1", "Project-Name1"), nullptr);
  auto project_config = project_configs->TakeSingleProjectConfig();
  EXPECT_TRUE(project_config != nullptr);
  EXPECT_EQ("Project-Name1", project_config->project_name());
  EXPECT_FALSE(project_configs->is_single_project());
  EXPECT_TRUE(project_configs->is_empty());
  EXPECT_EQ(project_configs->GetProjectConfig("Customer-Name1", "Project-Name1"), nullptr);

  // A ProjectConfigs constructed from a CobaltRegistry with 1 customer with 2
  // projects is not empty and is not a single project.
  project_configs = NewProjectConfigs(1, 2);
  EXPECT_FALSE(project_configs->is_single_project());
  EXPECT_FALSE(project_configs->is_empty());
  EXPECT_TRUE(project_configs->TakeSingleProjectConfig() == nullptr);

  // A ProjectConfigs constructed from a CobaltRegistry with 2 customers with
  // 1 project each is not empty and is not a single project.
  project_configs = NewProjectConfigs(2, 1);
  EXPECT_FALSE(project_configs->is_single_project());
  EXPECT_FALSE(project_configs->is_empty());
  EXPECT_TRUE(project_configs->TakeSingleProjectConfig() == nullptr);
}

}  // namespace cobalt::config

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/project_context.h"

#include <google/protobuf/text_format.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./gtest.h"
#include "./logging.h"
#include "./observation2.pb.h"
#include "config/project_configs.h"

using cobalt::config::ProjectConfigs;

namespace cobalt {
namespace logger {

namespace {
const char kCustomerA[] = "CustomerA";
const uint32_t kCustomerAId = 123;
const char kProjectA1[] = "ProjectA1";
const char kMetricA1a[] = "MetricA1a";
const uint32_t kMetricA1aId = 1;

const char kCobaltRegistry[] = R"(
customers {
  customer_name: "CustomerA"
  customer_id: 123

  projects: {
    project_name: "ProjectA1"
    project_id: 234
    metrics: {
      metric_name: "MetricA1a"
      customer_id: 123
      project_id: 234
      id: 1
    }
    metrics: {
      metric_name: "MetricA1b"
      customer_id: 123
      project_id: 234
      id: 2
    }
  }

  projects: {
    project_name: "ProjectA2"
    project_id: 345;
    metrics: {
      metric_name: "MetricA2a"
      customer_id: 123
      project_id: 345
      id: 1
    }
  }
}

customers {
  customer_name: "CustomerB"
  customer_id: 234
  projects: {
    project_name: "ProjectB1"
    project_id: 345;
    metrics: {
      metric_name: "MetricB1a"
      customer_id: 123
      project_id: 234
      id: 1
    }
  }
}

)";

bool PopulateCobaltRegistry(CobaltRegistry* cobalt_config) {
  google::protobuf::TextFormat::Parser parser;
  return parser.ParseFromString(kCobaltRegistry, cobalt_config);
}

}  // namespace

class ProjectContextTest : public ::testing::Test {
 protected:
  void SetUp() {
    // We create a ProjectConfigs by first creating a CobaltRegistry from
    // the ASCII proto above.
    auto cobalt_config = std::make_unique<CobaltRegistry>();
    ASSERT_TRUE(PopulateCobaltRegistry(cobalt_config.get()));
    auto project_configs =
        ProjectConfigs::CreateFromCobaltRegistryProto(std::move(cobalt_config));
    project_configs_.reset(project_configs.release());
  }

  // Check that |metric_definition| contains the correct data given that
  // it is supposed to be for MetricA1a.
  void CheckMetricA1a(const MetricDefinition& metric_definition) {
    EXPECT_EQ(kMetricA1a, metric_definition.metric_name());
    EXPECT_EQ(kMetricA1aId, metric_definition.id());
  }

  // Check that |project_context| contains the correct data given that it is
  // supposed to be for ProjectA1.
  void CheckProjectContextA1(const ProjectContext& project_context) {
    auto debug_string = project_context.DebugString();
    EXPECT_TRUE(debug_string.find(kCustomerA) != std::string::npos);
    EXPECT_TRUE(debug_string.find(kProjectA1) != std::string::npos);
    EXPECT_EQ(std::string(kCustomerA) + "." + kProjectA1,
              project_context.FullyQualifiedName());
    CheckMetricA1a(*project_context.GetMetric(kMetricA1a));
    CheckMetricA1a(*project_context.GetMetric(kMetricA1aId));
    MetricRef metric_ref(&project_context.project(),
                         project_context.GetMetric(kMetricA1a));
    EXPECT_EQ(kMetricA1aId, metric_ref.metric_id());
    EXPECT_EQ(std::string(kCustomerA) + "." + kProjectA1 + "." + kMetricA1a,
              metric_ref.FullyQualifiedName());

    EXPECT_EQ(nullptr, project_context.GetMetric("NoSuchMetric"));
    EXPECT_EQ(nullptr, project_context.GetMetric(42));
  }

  std::shared_ptr<ProjectConfigs> project_configs_;
};

// Test ProjectContext starting with constructing one that owns its
// ProjectConfig.
TEST_F(ProjectContextTest, ConstructWithOwnedProjectConfig) {
  auto project_config = std::make_unique<ProjectConfig>(
      *project_configs_->GetProjectConfig(kCustomerA, kProjectA1));
  auto project_context = std::make_unique<ProjectContext>(
      kCustomerAId, kCustomerA, std::move(project_config));
  CheckProjectContextA1(*project_context);
}

// Test ProjectContext starting with constructing one that doesn't own its
// ProjectConfig.
TEST_F(ProjectContextTest, ConstructWithUnownedProjectConfig) {
  auto project_context = std::make_unique<ProjectContext>(
      kCustomerAId, kCustomerA,
      project_configs_->GetProjectConfig(kCustomerA, kProjectA1));
  CheckProjectContextA1(*project_context);
}

}  // namespace logger
}  // namespace cobalt

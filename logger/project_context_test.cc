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
const uint32_t kProjectA1Id = 234;
const char kMetricA1a[] = "MetricA1a";
const uint32_t kMetricA1aId = 1;

const char kCobaltConfig[] = R"(
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

bool PopulateCobaltConfig(CobaltConfig* cobalt_config) {
  google::protobuf::TextFormat::Parser parser;
  return parser.ParseFromString(kCobaltConfig, cobalt_config);
}

}  // namespace

class ProjectContextTest : public ::testing::Test {
 protected:
  void SetUp() {
    // We create a ProjectConfigs by first creating a CobaltConfig from
    // the ASCII proto above.
    auto cobalt_config = std::make_unique<CobaltConfig>();
    ASSERT_TRUE(PopulateCobaltConfig(cobalt_config.get()));
    auto project_configs =
        ProjectConfigs::CreateFromCobaltConfigProto(std::move(cobalt_config));
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

// Test ProjectContext starting with constructing one from an instance
// of |MetricDefinitions|.
TEST_F(ProjectContextTest, ConstructWithMetricDefinitions) {
  auto metric_definitions = std::make_unique<MetricDefinitions>();
  metric_definitions->mutable_metric()->CopyFrom(
      project_configs_->GetProjectConfig(kCustomerA, kProjectA1)->metrics());
  auto project_context = std::make_unique<ProjectContext>(
      kCustomerAId, kProjectA1Id, kCustomerA, kProjectA1,
      std::move(metric_definitions));
  CheckProjectContextA1(*project_context);
}

// Test ProjectContext starting with constructing one via the method
// ConstructWithProjectConfigs.
TEST_F(ProjectContextTest, ConstructWithProjectConfigs) {
  auto project_context_or = ProjectContext::ConstructWithProjectConfigs(
      kCustomerA, kProjectA1, project_configs_);
  ASSERT_TRUE(project_context_or.ok());
  auto project_context = project_context_or.ConsumeValueOrDie();
  CheckProjectContextA1(*project_context);
}

// Tests the method ConstructWithProjectConfigs() in the case where an
// invalid customer or project name is used.
TEST_F(ProjectContextTest, ConstructWithProjectConfigsBad) {
  auto project_context_or = ProjectContext::ConstructWithProjectConfigs(
      "NoSuchCustomer", kProjectA1, project_configs_);
  ASSERT_FALSE(project_context_or.ok());

  project_context_or = ProjectContext::ConstructWithProjectConfigs(
      kCustomerA, "NoSuchProject", project_configs_);
  ASSERT_FALSE(project_context_or.ok());
}

}  // namespace logger
}  // namespace cobalt

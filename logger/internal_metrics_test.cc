// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/internal_metrics.h"

#include <vector>

#include "./gtest.h"
#include "logger/fake_logger.h"

namespace cobalt {
namespace logger {

const int64_t kNumBytes = 123;

const uint32_t kTestCustomerId = 1;
const uint32_t kTestProjectId = 2;

constexpr char kTestCustomerName[] = "test";
constexpr char kTestProjectName[] = "project";

class InternalMetricsImplTest : public ::testing::Test {
 public:
  void SetUp() override {
    test_registry_.reset(new CobaltRegistry());

    auto* customer_config = test_registry_->add_customers();
    customer_config->set_customer_id(kTestCustomerId);
    customer_config->set_customer_name(kTestCustomerName);

    auto* project_config = customer_config->add_projects();
    project_config->set_project_id(kTestProjectId);
    project_config->set_project_name(kTestProjectName);
  }

  Project GetTestProject() {
    Project project;
    project.set_customer_id(kTestCustomerId);
    project.set_customer_name(kTestCustomerName);
    project.set_project_id(kTestProjectId);
    project.set_project_name(kTestProjectName);
    return project;
  }

  CobaltRegistry* TestRegistry() { return test_registry_.get(); }

  std::string ExpectedComponentString(const std::string& first,
                                      const std::string& second) {
    std::ostringstream component;
    component << first << '/' << second;
    return component.str();
  }

  std::string ExpectedComponentString(uint32_t first, uint32_t second) {
    std::ostringstream component;
    component << first << '/' << second;
    return component.str();
  }

  std::unique_ptr<CobaltRegistry> test_registry_;
};

TEST_F(InternalMetricsImplTest, LoggerCalled) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger, TestRegistry());

  metrics.LoggerCalled(
      LoggerCallsMadeMetricDimensionLoggerMethod::LogMemoryUsage,
      GetTestProject());

  ASSERT_EQ(logger.call_count(), 2);
  ASSERT_TRUE(logger.last_event_logged().has_count_event());
  ASSERT_EQ(logger.last_event_logged().count_event().component(),
            ExpectedComponentString(kTestCustomerName, kTestProjectName));
}

TEST_F(InternalMetricsImplTest, LoggerCalledPauseWorks) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger, TestRegistry());

  metrics.PauseLogging();
  for (int i = 0; i < 100; i++) {
    metrics.LoggerCalled(
        LoggerCallsMadeMetricDimensionLoggerMethod::LogMemoryUsage,
        GetTestProject());
  }
  metrics.ResumeLogging();

  ASSERT_EQ(logger.call_count(), 0);
}

TEST_F(InternalMetricsImplTest, BytesUploaded) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger, TestRegistry());

  ASSERT_EQ(logger.call_count(), 0);
  metrics.BytesUploaded(PerDeviceBytesUploadedMetricDimensionStatus::Attempted,
                        kNumBytes);

  ASSERT_EQ(logger.call_count(), 1);
  ASSERT_TRUE(logger.last_event_logged().has_count_event());
  ASSERT_EQ(logger.last_event_logged().count_event().count(), kNumBytes);
}

TEST_F(InternalMetricsImplTest, BytesUploadedPauseWorks) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger, TestRegistry());

  metrics.PauseLogging();
  for (int i = 0; i < 100; i++) {
    metrics.BytesUploaded(
        PerDeviceBytesUploadedMetricDimensionStatus::Attempted, kNumBytes);
  }
  metrics.ResumeLogging();
  ASSERT_EQ(logger.call_count(), 0);
}

TEST_F(InternalMetricsImplTest, BytesStored) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger, TestRegistry());

  ASSERT_EQ(logger.call_count(), 0);
  metrics.BytesStored(PerProjectBytesStoredMetricDimensionStatus::Attempted,
                      kNumBytes, kTestCustomerId, kTestProjectId);

  ASSERT_EQ(logger.call_count(), 1);
  ASSERT_TRUE(logger.last_event_logged().has_memory_usage_event());
  ASSERT_EQ(logger.last_event_logged().memory_usage_event().bytes(), kNumBytes);
}

TEST_F(InternalMetricsImplTest, BytesStoredComponentNameMatchesRegistry) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger, TestRegistry());

  metrics.BytesStored(PerProjectBytesStoredMetricDimensionStatus::Attempted,
                      kNumBytes, kTestCustomerId, kTestProjectId);

  ASSERT_EQ(logger.last_event_logged().memory_usage_event().component(),
            ExpectedComponentString(kTestCustomerName, kTestProjectName));
}

TEST_F(InternalMetricsImplTest, BytesStoredComponentNameMatchesIds) {
  testing::FakeLogger logger;
  // with no registry, each component will consist of the customer/project ids
  // rather than their names.
  InternalMetricsImpl metrics(&logger, nullptr);

  ASSERT_EQ(logger.call_count(), 0);
  metrics.BytesStored(PerProjectBytesStoredMetricDimensionStatus::Attempted,
                      kNumBytes, kTestCustomerId, kTestProjectId);

  ASSERT_EQ(logger.last_event_logged().memory_usage_event().component(),
            ExpectedComponentString(kTestCustomerId, kTestProjectId));
}

TEST_F(InternalMetricsImplTest, BytesStoredPauseWorks) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger, TestRegistry());

  metrics.PauseLogging();
  for (int i = 0; i < 100; i++) {
    metrics.BytesStored(PerProjectBytesStoredMetricDimensionStatus::Attempted,
                        kNumBytes, kTestCustomerId, kTestProjectId);
  }
  metrics.ResumeLogging();
  ASSERT_EQ(logger.call_count(), 0);
}

}  // namespace logger
}  // namespace cobalt

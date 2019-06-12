// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/internal_metrics.h"

#include <vector>

#include "./gtest.h"
#include "logger/logger_test_utils.h"

namespace cobalt {
namespace logger {

TEST(InternalMetricsImpl, PauseWorks) {
  Project project;
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger);
  ASSERT_EQ(logger.call_count(), 0);
  metrics.LoggerCalled(
      LoggerCallsMadeMetricDimensionLoggerMethod::LogMemoryUsage, project);
  ASSERT_EQ(logger.call_count(), 2);
  metrics.PauseLogging();
  for (int i = 0; i < 100; i++) {
    metrics.LoggerCalled(
        LoggerCallsMadeMetricDimensionLoggerMethod::LogMemoryUsage, project);
  }
  metrics.ResumeLogging();
  ASSERT_EQ(logger.call_count(), 2);
}

}  // namespace logger
}  // namespace cobalt

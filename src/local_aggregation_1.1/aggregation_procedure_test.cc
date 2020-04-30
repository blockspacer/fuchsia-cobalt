// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/local_aggregation_1.1/aggregation_procedure.h"

#include "src/local_aggregation_1.1/testing/test_registry.cb.h"
#include "src/logger/project_context_factory.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::local_aggregation {

TEST(AggregationProcedure, GetWorks) {
  auto global_project_context_factory =
      logger::ProjectContextFactory::CreateFromCobaltRegistryBase64(kCobaltRegistryBase64);

  auto project = global_project_context_factory->NewProjectContext(123, 1);
  auto metric = project->GetMetric(1);
  auto report = metric->reports(0);

  ASSERT_NE(AggregationProcedure::Get(*metric, report), nullptr);
}

}  // namespace cobalt::local_aggregation

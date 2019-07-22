// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/project_context_factory.h"

#include <google/protobuf/text_format.h>

#include <memory>
#include <string>

#include "./gtest.h"
#include "./logging.h"
#include "logger/test_registries/project_context_factory_test_registry/a.cb.h"
#include "logger/test_registries/project_context_factory_test_registry/b.cb.h"
#include "logger/test_registries/project_context_factory_test_registry/c.cb.h"
#include "util/crypto_util/base64.h"

namespace cobalt::logger {

namespace {

constexpr uint32_t kCustomer11 = 11;
constexpr uint32_t kProject11 = 11;
constexpr uint32_t kMetric11 = 11;
constexpr uint32_t kCustomer22 = 22;
constexpr uint32_t kProject22 = 22;
constexpr uint32_t kMetric22 = 22;

// Returns the bytes of a serialized CobaltRegistry, corresponding to
// registry A, B or C, according as |which_registry| is equal to 1, 2, or 3.
std::string GetCobaltRegistryBytes(int which_registry) {
  std::string registry_text;
  switch (which_registry) {
    case 1:
      registry_text = std::string(a::kCobaltRegistryBase64);
      break;

    case 2:
      registry_text = std::string(b::kCobaltRegistryBase64);
      break;

    case 3:
      registry_text = std::string(c::kCobaltRegistryBase64);
      break;

    default:
      LOG(FATAL) << "Unexpected value for which_registry: " << which_registry;
  }
  std::string registry_bytes;
  EXPECT_TRUE(crypto::Base64Decode(registry_text, &registry_bytes));
  return registry_bytes;
}

}  // namespace

// Tests the methods of ProjectContext factory when it is constructed
// with invalid bytes.
TEST(ProjectContextFactoryTest, InvalidBytes) {
  ProjectContextFactory factory("Invalid bytes");
  EXPECT_FALSE(factory.is_valid());
  EXPECT_FALSE(factory.is_single_project());
  EXPECT_FALSE(factory.is_single_legacy_project());
  EXPECT_EQ(nullptr, factory.NewProjectContext("Customer11", "Project11"));
  EXPECT_EQ(nullptr, factory.NewProjectContext("Customer22", "Project22"));
  EXPECT_EQ(nullptr, factory.TakeSingleProjectContext());
  EXPECT_EQ(nullptr,
            factory.NewLegacyProjectContext(kCustomer11, kProject11).get());
  EXPECT_EQ(nullptr, factory.NewSingleLegacyProjectContext().get());
}

// Tests the methods of ProjectContext factory when it is constructed
// with the bytes of registry A.
TEST(ProjectContextFactoryTest, RegistryA) {
  ProjectContextFactory factory(GetCobaltRegistryBytes(1));

  // Registry A is valid
  EXPECT_TRUE(factory.is_valid());

  // Registry A is not a single Cobalt 1.0 project
  EXPECT_FALSE(factory.is_single_project());

  // Registry A is a single Cobalt 0.1 project
  EXPECT_TRUE(factory.is_single_legacy_project());

  // Registry A does not contain any Cobalt 1.0 projects.
  EXPECT_EQ(nullptr,
            factory.NewProjectContext("Customer11", "Project11").get());
  EXPECT_EQ(nullptr,
            factory.NewProjectContext("Customer22", "Project22").get());

  // Registry A does not contain a single Cobalt 1.0 project.
  EXPECT_EQ(nullptr, factory.TakeSingleProjectContext());

  // Registry A does contain some Cobalt 0.1 project data.
  EXPECT_NE(nullptr,
            factory.NewLegacyProjectContext(kCustomer11, kProject11).get());
  EXPECT_NE(nullptr,
            factory.NewLegacyProjectContext(kCustomer22, kProject22).get());

  // Registry A does contain a single Cobalt 0.1 project
  EXPECT_NE(nullptr, factory.NewSingleLegacyProjectContext().get());

  // The single Cobalt 0.1 project contains only metric 11.
  auto legacy_context = factory.NewSingleLegacyProjectContext();
  EXPECT_NE(nullptr, legacy_context->Metric(kMetric11));
  EXPECT_EQ(nullptr, legacy_context->Metric(kMetric22));

  // The Cobalt 0.1 project 11 contains only metric 11.
  legacy_context = factory.NewLegacyProjectContext(kCustomer11, kProject11);
  EXPECT_NE(nullptr, legacy_context->Metric(kMetric11));
  EXPECT_EQ(nullptr, legacy_context->Metric(kMetric22));

  // The Cobalt 0.1 project 22 contains no metrics.
  legacy_context = factory.NewLegacyProjectContext(kCustomer22, kProject22);
  EXPECT_EQ(nullptr, legacy_context->Metric(kMetric11));
  EXPECT_EQ(nullptr, legacy_context->Metric(kMetric22));
}

TEST(ProjectContextFactoryTest, RegistryB) {
  ProjectContextFactory factory(GetCobaltRegistryBytes(2));

  // Registry B is valid.
  EXPECT_TRUE(factory.is_valid());

  // Registry B is a single Cobalt 1.0 poject.
  EXPECT_TRUE(factory.is_single_project());

  // Registry B is not a single Cobalt 0.1 project.
  EXPECT_FALSE(factory.is_single_legacy_project());

  // Registry B contains Cobalt 1.0 project 22, but no project 11
  EXPECT_EQ(nullptr,
            factory.NewProjectContext("Customer11", "Project11").get());
  EXPECT_NE(nullptr,
            factory.NewProjectContext("Customer22", "Project22").get());

  // Registry B does not contain any Cobalt 0.1 project data.
  EXPECT_EQ(nullptr,
            factory.NewLegacyProjectContext(kCustomer11, kProject11).get());
  EXPECT_EQ(nullptr, factory.NewSingleLegacyProjectContext().get());

  // Registry B does contain a single Cobalt 1.0 project.
  auto context = factory.TakeSingleProjectContext();
  EXPECT_NE(nullptr, context);

  // The single Cobalt 1.0 project contains metric 22 but not metric 11.
  EXPECT_EQ(nullptr, context->GetMetric("Metric11"));
  EXPECT_NE(nullptr, context->GetMetric("Metric22"));

  // The data has been removed from the factory.
  EXPECT_EQ(nullptr, factory.NewProjectContext("Customer22", "Project22"));
  EXPECT_FALSE(factory.is_valid());
}

TEST(ProjectContextFactoryTest, RegistryC) {
  ProjectContextFactory factory(GetCobaltRegistryBytes(3));

  // Registry C is valid.
  EXPECT_TRUE(factory.is_valid());

  // Registry C does not contain only a single Cobalt 1.0 project.
  EXPECT_FALSE(factory.is_single_project());

  // Registry C does not contain only a single Cobalt 0.1 project.
  EXPECT_FALSE(factory.is_single_legacy_project());

  // Registry C contains Cobalt 1.0 project 22 but not project 11.
  EXPECT_EQ(nullptr,
            factory.NewProjectContext("Customer11", "Project11").get());
  EXPECT_NE(nullptr,
            factory.NewProjectContext("Customer22", "Project22").get());

  // Registry C does not contain only a single Cobalt 0.1 project.
  EXPECT_EQ(nullptr, factory.TakeSingleProjectContext());

  // Registry C contains Cobalt 0.1 project 11 but not project 22.
  EXPECT_NE(nullptr,
            factory.NewLegacyProjectContext(kCustomer11, kProject11).get());
  EXPECT_EQ(nullptr, factory.NewSingleLegacyProjectContext().get());

  // Cobalt 0.1 project 11 contains metric 11 but not metric 22.
  auto legacy_context =
      factory.NewLegacyProjectContext(kCustomer11, kProject11);
  EXPECT_NE(nullptr, legacy_context->Metric(kMetric11));
  EXPECT_EQ(nullptr, legacy_context->Metric(kMetric22));

  // There are no Cobalt 0.1 metrics for project 22.
  legacy_context = factory.NewLegacyProjectContext(kCustomer22, kProject22);
  EXPECT_EQ(nullptr, legacy_context->Metric(kMetric11));
  EXPECT_EQ(nullptr, legacy_context->Metric(kMetric22));

  // Cobalt 1.0 project 22 contains metric 22 but not metric 11.
  auto context = factory.NewProjectContext("Customer22", "Project22");
  EXPECT_EQ(nullptr, context->GetMetric("Metric11"));
  EXPECT_NE(nullptr, context->GetMetric("Metric22"));
}

TEST(ProjectContextFactoryTest, ReleaseStage) {
  ProjectContextFactory factory(GetCobaltRegistryBytes(3));

  auto context = factory.NewProjectContext("Customer22", "Project22");
  EXPECT_EQ(GA, context->project().release_stage());

  context = factory.NewProjectContext("Customer22", "Project22", FISHFOOD);
  EXPECT_EQ(FISHFOOD, context->project().release_stage());
}

}  // namespace cobalt::logger

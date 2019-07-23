// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config/client_config.h"

#include <memory>
#include <string>
#include <utility>

#include "./logging.h"
#include "config/cobalt_registry.pb.h"
#include "config/encoding_config.h"
#include "config/encodings.pb.h"
#include "config/metric_config.h"
#include "config/metrics.pb.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/crypto_util/base64.h"

namespace cobalt::config {

namespace {
constexpr uint32_t kNonexistentId = 41;
constexpr uint32_t kAnId = 42;
constexpr uint32_t kAnotherId = 43;

void AddMetric(int customer_id, int project_id, int id, CobaltRegistry* cobalt_registry) {
  Metric* metric = cobalt_registry->add_metric_configs();
  metric->set_customer_id(customer_id);
  metric->set_project_id(project_id);
  metric->set_id(id);
}

void AddMetric(int id, CobaltRegistry* cobalt_registry) { AddMetric(id, id, id, cobalt_registry); }

void AddEncodingConfig(int customer_id, int project_id, int id, CobaltRegistry* cobalt_registry) {
  EncodingConfig* encoding_config = cobalt_registry->add_encoding_configs();
  encoding_config->set_customer_id(customer_id);
  encoding_config->set_project_id(project_id);
  encoding_config->set_id(id);
}

void AddEncodingConfig(int id, CobaltRegistry* cobalt_registry) {
  AddEncodingConfig(id, id, id, cobalt_registry);
}

std::unique_ptr<ClientConfig> CreateFromCopyOfRegistry(const CobaltRegistry& registry) {
  std::string cobalt_registry_bytes;
  EXPECT_TRUE(registry.SerializeToString(&cobalt_registry_bytes));
  return ClientConfig::CreateFromCobaltRegistryBytes(cobalt_registry_bytes);
}

}  // namespace

TEST(ClientConfigTest, CreateFromCobaltRegistryBytes) {
  std::string cobalt_registry_bytes;
  CobaltRegistry cobalt_registry;
  AddMetric(kAnId, &cobalt_registry);
  AddMetric(kAnotherId, &cobalt_registry);
  AddEncodingConfig(kAnId, &cobalt_registry);
  AddEncodingConfig(kAnotherId, &cobalt_registry);
  ASSERT_TRUE(cobalt_registry.SerializeToString(&cobalt_registry_bytes));
  auto client_config = ClientConfig::CreateFromCobaltRegistryBytes(cobalt_registry_bytes);
  ASSERT_NE(nullptr, client_config);
  EXPECT_EQ(nullptr, client_config->EncodingConfig(kNonexistentId, kNonexistentId, kNonexistentId));
  EXPECT_NE(nullptr, client_config->EncodingConfig(kAnId, kAnId, kAnId));
  EXPECT_NE(nullptr, client_config->EncodingConfig(kAnotherId, kAnotherId, kAnotherId));
  EXPECT_EQ(nullptr, client_config->Metric(kNonexistentId, kNonexistentId, kNonexistentId));
  EXPECT_NE(nullptr, client_config->Metric(kAnId, kAnId, kAnId));
  EXPECT_NE(nullptr, client_config->Metric(kAnotherId, kAnotherId, kAnotherId));
}

TEST(ClientConfigTest, CreateFromCobaltRegistryBase64) {
  std::string cobalt_registry_bytes;
  CobaltRegistry cobalt_registry;
  AddMetric(kAnId, &cobalt_registry);
  AddMetric(kAnotherId, &cobalt_registry);
  AddEncodingConfig(kAnId, &cobalt_registry);
  AddEncodingConfig(kAnotherId, &cobalt_registry);
  ASSERT_TRUE(cobalt_registry.SerializeToString(&cobalt_registry_bytes));
  std::string cobalt_registry_base64;
  crypto::Base64Encode(cobalt_registry_bytes, &cobalt_registry_base64);
  auto client_config = ClientConfig::CreateFromCobaltRegistryBase64(cobalt_registry_base64);
  ASSERT_NE(nullptr, client_config);
  EXPECT_EQ(nullptr, client_config->EncodingConfig(kNonexistentId, kNonexistentId, kNonexistentId));
  EXPECT_NE(nullptr, client_config->EncodingConfig(kAnId, kAnId, kAnId));
  EXPECT_NE(nullptr, client_config->EncodingConfig(kAnotherId, kAnotherId, kAnotherId));
  EXPECT_EQ(nullptr, client_config->Metric(kNonexistentId, kNonexistentId, kNonexistentId));
  EXPECT_NE(nullptr, client_config->Metric(kAnId, kAnId, kAnId));
  EXPECT_NE(nullptr, client_config->Metric(kAnotherId, kAnotherId, kAnotherId));
}

// Tests the method CreateFromCobaltRegistry along with the accessors
// is_single_project(), is_empty(), single_customer_id() and single_project_id()
// and IsLegacy(), in the case that the CobaltRegistry contains only Cobalt 0.1
// data and no Cobalt 1.0 data.
TEST(ClientConfigTest, CreateFromCobaltRegistry) {
  CobaltRegistry cobalt_registry;
  auto client_config = CreateFromCopyOfRegistry(cobalt_registry);

  EXPECT_TRUE(client_config->is_empty());
  EXPECT_FALSE(client_config->is_single_project());

  // Check that even though we have not added any metrics, querying for
  // a metric does not cause a crash.
  EXPECT_EQ(nullptr, client_config->Metric(1, 1, 1));

  AddMetric(kAnId, &cobalt_registry);
  client_config = CreateFromCopyOfRegistry(cobalt_registry);
  EXPECT_FALSE(client_config->is_empty());
  EXPECT_TRUE(client_config->is_single_project());
  EXPECT_EQ(kAnId, client_config->single_customer_id());
  EXPECT_EQ(kAnId, client_config->single_project_id());

  AddEncodingConfig(kAnId, &cobalt_registry);
  client_config = CreateFromCopyOfRegistry(cobalt_registry);
  EXPECT_FALSE(client_config->is_empty());
  EXPECT_TRUE(client_config->is_single_project());
  EXPECT_EQ(kAnId, client_config->single_customer_id());
  EXPECT_EQ(kAnId, client_config->single_project_id());

  AddMetric(kAnotherId, &cobalt_registry);
  client_config = CreateFromCopyOfRegistry(cobalt_registry);
  EXPECT_FALSE(client_config->is_empty());
  EXPECT_FALSE(client_config->is_single_project());
}

}  // namespace cobalt::config

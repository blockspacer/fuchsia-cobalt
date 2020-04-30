// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/local_aggregation_1.1/local_aggregate_storage.h"

#include "absl/strings/str_cat.h"
#include "src/lib/util/testing/test_with_files.h"
#include "src/local_aggregation_1.1/testing/test_registry.cb.h"
#include "src/logger/project_context_factory.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::local_aggregation {
using ::testing::Contains;
using ::testing::Not;

class LocalAggregateStorageTest : public util::testing::TestWithFiles {
  void SetUp() override {
    MakeTestFolder();
    global_project_context_factory_ =
        logger::ProjectContextFactory::CreateFromCobaltRegistryBase64(kCobaltRegistryBase64);
    storage_ = std::make_unique<LocalAggregateStorage>(test_folder(), fs(),
                                                       global_project_context_factory_.get());
  }

 protected:
  std::unique_ptr<logger::ProjectContextFactory> global_project_context_factory_;
  std::unique_ptr<LocalAggregateStorage> storage_;
};

TEST_F(LocalAggregateStorageTest, MakesExpectedFiles) {
  // Root directory should contain expected customer_id: 123
  ASSERT_THAT(fs()->ListFiles(test_folder()).ConsumeValueOrDie(), Contains("123"));

  // Customer 123 directory should contain expected project: 1
  ASSERT_THAT(fs()->ListFiles(absl::StrCat(test_folder(), "/123")).ConsumeValueOrDie(),
              Contains("1"));

  // Customer 123 Project 1 directory should *not* contain the metric file: 1
  ASSERT_THAT(fs()->ListFiles(absl::StrCat(test_folder(), "/123/1")).ConsumeValueOrDie(),
              Not(Contains("1")));

  storage_->SaveMetricAggregate(123, 1, 1);

  // Customer 123 Project 1 directory should contain the metric file: 1
  ASSERT_THAT(fs()->ListFiles(absl::StrCat(test_folder(), "/123/1")).ConsumeValueOrDie(),
              Contains("1"));
}

}  // namespace cobalt::local_aggregation

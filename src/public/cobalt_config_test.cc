// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/public/cobalt_config.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

TEST(TargetPipelineTest, ClearcutProd) {
  TargetPipeline pipeline(system_data::Environment::PROD, /*shuffler_encryption_key=*/"",
                          /*analyzer_encryption_key=*/"", /*http_client=*/nullptr);
  ASSERT_EQ(pipeline.clearcut_endpoint(), kProductionClearcutEndpoint);
}

TEST(TargetPipelineTest, ClearcutDevel) {
  TargetPipeline pipeline(system_data::Environment::DEVEL, /*shuffler_encryption_key=*/"",
                          /*analyzer_encryption_key=*/"", /*http_client=*/nullptr);
  ASSERT_EQ(pipeline.clearcut_endpoint(), kDefaultClearcutEndpoint);
}

}  // namespace cobalt

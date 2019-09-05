// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/registry/id.h"

#include "gflags/gflags.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::config {

TEST(IdFromName, Known) {
  const uint32_t expected = 0x8b85b08d;
  EXPECT_EQ(expected, IdFromName("test_name"));
}

}  // namespace cobalt::config

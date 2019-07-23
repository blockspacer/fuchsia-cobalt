// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/gcs/gcs_util.h"

#include <memory>
#include <string>
#include <vector>

#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace util {
namespace gcs {

// The guts of this test have been commented out so that on our CI and CQ
// bots all we are doing is testing that GcsUtil compiles. A developer may
// uncomment the guts of the test and replace the three string tokens:
//
// <cobalt_root_dir>
// <path to some service account key file>
// <put real bucket name here>
//
// appropriately in order to test uploading. Since GcsUtil is a very thin
// wrapper around google-api-cpp-client there is almost nothing we could
// mock out for a unit test.
TEST(GcUtilTest, SmokeTest) {
  GcsUtil gcs_util;
  /*
  setenv("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH",
         "<cobalt_root_dir>/third_party/grpc/etc/roots.pem", 1);
  setenv("COBALT_GCS_SERVICE_ACCOUNT_CREDENTIALS",
         "<path to some service account key file>",
         1);
  ASSERT_TRUE(gcs_util.InitFromDefaultPaths());

  // Start with a ping.
  std::string bucket_name = "<put real bucket name here>";
  ASSERT_TRUE(gcs_util.Ping(bucket_name));

  // Upload using the string method.
  std::string data(
      "It is a far, far better thing that I do, than I have ever done;");
  ASSERT_TRUE(gcs_util.Upload(bucket_name, "tale2citiesA", "text/plain",
                              data.data(), data.size()));

  // Upload using the stream method.
  std::istringstream stream(
      "it is a far, far better rest that I go to than I have ever known.");
  ASSERT_TRUE(
      gcs_util.Upload(bucket_name, "tale2citiesB", "text/plain", &stream));

  // More pings.
  ASSERT_TRUE(gcs_util.Ping(bucket_name));
  ASSERT_TRUE(gcs_util.Ping(bucket_name));
  */
}

}  // namespace gcs
}  // namespace util
}  // namespace cobalt

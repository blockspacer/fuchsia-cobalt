// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoder/system_data.h"

#include <stdio.h>

#include <set>
#include <string>
#include <utility>

#include "./gtest.h"
#include "./logging.h"

namespace cobalt {
namespace encoder {

TEST(SystemDataTest, BasicTest) {
  SystemData system_data("test_product", "", "test_version");
  EXPECT_NE(SystemProfile::UNKNOWN_OS, system_data.system_profile().os());
  EXPECT_NE(SystemProfile::UNKNOWN_ARCH, system_data.system_profile().arch());
  EXPECT_NE(system_data.system_profile().board_name(), "");
  EXPECT_EQ(system_data.system_profile().product_name(), "test_product");
  EXPECT_EQ(system_data.system_profile().system_version(), "test_version");

  // Board names we expect to see.
  std::set<std::string> expected_board_names = {"Eve", "Generic ARM"};

  // CPU signatures we expect to see.
  std::set<int> expected_signatures = {
      0x0306D4,  // Intel Broadwell (model=0x3D family=0x6) stepping=0x4
      0x0306F0,  // Intel Broadwell (model=0x3F family=0x6) stepping=0x0
      0x0406E3,  // Intel Broadwell (model=0x4E family=0x6) stepping=0x3
      0x0406F1,  // Intel Broadwell (model=0x4F family=0x6) stepping=0x1
  };

  auto name = system_data.system_profile().board_name();
  std::string unknown_prefix = "unknown:";
  if (name.compare(0, unknown_prefix.size(), unknown_prefix) == 0) {
    int signature = 0;
    sscanf(name.c_str(), "unknown:0x%X", &signature);
    if (expected_signatures.count(signature) == 0) {
      LOG(WARNING) << "***** found new signature: " << signature;
    }
    EXPECT_GE(signature, 0x030000);
    EXPECT_LE(signature, 0x090000);
  } else {
    EXPECT_NE(0ul, expected_board_names.count(name));
  }
}

TEST(SystemDataTest, SetExperimentTest) {
  const int kExperimentId = 1;
  const int kArmId = 123;

  SystemData system_data("test_product", "");

  Experiment experiment;
  experiment.set_experiment_id(kExperimentId);
  experiment.set_arm_id(kArmId);
  std::vector<Experiment> experiments = {experiment};

  system_data.SetExperimentState(experiments);

  EXPECT_EQ(system_data.experiments().front().experiment_id(), kExperimentId);
  EXPECT_EQ(system_data.experiments().front().arm_id(), kArmId);
}

TEST(SystemDataTest, SetChannelTest) {
  SystemData system_data("test_product", "", "test_version");
  EXPECT_EQ(system_data.system_profile().channel(), "");
  system_data.SetChannel("Channel");
  EXPECT_EQ(system_data.system_profile().channel(), "Channel");
}

}  // namespace encoder

}  // namespace cobalt

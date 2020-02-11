// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_SYSTEM_DATA_FAKE_SYSTEM_DATA_H_
#define COBALT_SRC_SYSTEM_DATA_FAKE_SYSTEM_DATA_H_

#include <string>
#include <vector>

#include "src/system_data/system_data.h"

namespace cobalt::system_data {

// Mock of the SystemDataInterface. Used for testing.
class FakeSystemData : public SystemDataInterface {
 public:
  FakeSystemData() {
    system_profile_.set_os(SystemProfile::FUCHSIA);
    system_profile_.set_arch(SystemProfile::ARM_64);
    system_profile_.set_board_name("Testing Board");
    system_profile_.set_product_name("Testing Product");
    system_profile_.set_channel("<unset>");
  }

  const SystemProfile& system_profile() const override { return system_profile_; }

  void SetExperimentState(std::vector<Experiment> experiments) override {
    experiments_ = std::move(experiments);
  }

  const std::vector<Experiment>& experiments() const override { return experiments_; }

  void SetChannel(const std::string& channel) override { system_profile_.set_channel(channel); };

  const std::string& channel() const override { return system_profile_.channel(); }

  const ReleaseStage& release_stage() const override { return release_stage_; }

 private:
  SystemProfile system_profile_;
  std::vector<Experiment> experiments_;
  ReleaseStage release_stage_ = ReleaseStage::GA;
};

}  // namespace cobalt::system_data

#endif  // COBALT_SRC_SYSTEM_DATA_FAKE_SYSTEM_DATA_H_

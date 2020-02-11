// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_SYSTEM_DATA_SYSTEM_DATA_H_
#define COBALT_SRC_SYSTEM_DATA_SYSTEM_DATA_H_

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "src/lib/util/protected_fields.h"
#include "src/pb/observation_batch.pb.h"
#include "src/registry/metric_definition.pb.h"

namespace cobalt::system_data {

// An abstraction of the interface to SystemData that allows mocking in
// tests.
class SystemDataInterface {
 public:
  virtual ~SystemDataInterface() = default;

  // Returns the SystemProfile for the current system.
  [[nodiscard]] virtual const SystemProfile& system_profile() const = 0;

  // Resets the experiment state to the one provided.
  virtual void SetExperimentState(std::vector<Experiment> experiments) = 0;

  // Returns a vector with all experiments the system has a notion of.
  [[nodiscard]] virtual const std::vector<Experiment>& experiments() const = 0;

  // Resets the current channel value.
  virtual void SetChannel(const std::string& channel) = 0;

  // Returns the current channel.
  [[nodiscard]] virtual const std::string& channel() const = 0;

  // Returns the current ReleaseStage.
  [[nodiscard]] virtual const ReleaseStage& release_stage() const = 0;
};

// The embedding client creates a singleton instance of SystemData at start-up
// time and uses it to query data about the client's running system. There
// are two categories of data: static data about the system encapsulated in
// the SystemProfile, and dynamic stateful data about the running system.
class SystemData : public SystemDataInterface {
 public:
  // Constructor: Uses the real SystemProfile of the actual running system.
  //
  // |product_name|: The value to use for the |product_name| field of the
  // embedded SystemProfile.
  //
  // |board_name_suggestion|: A suggestion for the value to use for the
  // |board_name| field of the embedded SystemProfile. This may be ignored if
  // SystemData is able to determine the boardh name directly. A value of ""
  // indicates that the caller has no information about board name, so one
  // should be guessed.
  //
  // |release_stage|: The ReleaseStage of the running system.
  //
  // |version|: The version of the running system. The use of this field is
  // system-specific. For example on Fuchsia a possible value for |version| is
  // "20190220_01_RC00".
  SystemData(const std::string& product_name, const std::string& board_name_suggestion,
             ReleaseStage release_stage, const std::string& version = "");

  ~SystemData() override = default;

  // Returns a vector with all experiments the system has a notion of.
  const std::vector<Experiment>& experiments() const override {
    auto unprotected_experiments = protected_experiments_.const_lock();
    return unprotected_experiments->experiments;
  }

  // Returns the SystemProfile for the current system.
  const SystemProfile& system_profile() const override { return system_profile_; }

  // Resets the experiment state to the one provided.
  void SetExperimentState(std::vector<Experiment> experiments) override {
    auto unprotected_experiments = protected_experiments_.lock();
    unprotected_experiments->experiments = std::move(experiments);
  }

  // Resets the current channel value.
  void SetChannel(const std::string& channel) override;

  const std::string& channel() const override { return system_profile_.channel(); }

  const ReleaseStage& release_stage() const override { return release_stage_; }

  // Overrides the stored SystemProfile. Useful for testing.
  void OverrideSystemProfile(const SystemProfile& profile);

 private:
  void PopulateSystemProfile();

  SystemProfile system_profile_;
  struct UnprotectedExperiments {
    std::vector<Experiment> experiments;
  };
  util::RWProtectedFields<UnprotectedExperiments> protected_experiments_;
  ReleaseStage release_stage_;
};

}  // namespace cobalt::system_data

namespace cobalt::encoder {

using system_data::SystemData;
using system_data::SystemDataInterface;

}  // namespace cobalt::encoder

#endif  // COBALT_SRC_SYSTEM_DATA_SYSTEM_DATA_H_

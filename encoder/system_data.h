// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_SYSTEM_DATA_H_
#define COBALT_ENCODER_SYSTEM_DATA_H_

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "./observation_batch.pb.h"
#include "logger/channel_mapper.h"
#include "third_party/abseil-cpp/absl/synchronization/mutex.h"

namespace cobalt {
namespace encoder {

// An abstraction of the interface to SystemData that allows mocking in
// tests.
class SystemDataInterface {
 public:
  virtual ~SystemDataInterface() = default;

  // Returns the SystemProfile for the current system.
  virtual const SystemProfile& system_profile() const = 0;

  // Returns a vector with all experiments the system has a notion of.
  virtual const std::vector<Experiment>& experiments() const = 0;

  // Returns the current channel.
  virtual const std::string& channel() const = 0;

  // Returns the current ReleaseStage.
  virtual const ReleaseStage& release_stage() const = 0;
};

// The Encoder client creates a singleton instance of SystemData at start-up
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
  // |version|: The version of the running system. The use of this field is
  // system-specific. For example on Fuchsia a possible value for |version| is
  // "20190220_01_RC00".
  SystemData(const std::string& product_name,
             const std::string& board_name_suggestion,
             const std::string& version = "",
             std::unique_ptr<logger::ChannelMapper> channel_mapper = nullptr);

  virtual ~SystemData() = default;

  // Returns a vector with all experiments the system has a notion of.
  const std::vector<Experiment>& experiments() const override
      LOCKS_EXCLUDED(experiments_mutex_) {
    absl::ReaderMutexLock lock(&experiments_mutex_);
    return experiments_;
  }

  // Returns the SystemProfile for the current system.
  const SystemProfile& system_profile() const override {
    return system_profile_;
  }

  // Resets the experiment state to the one provided.
  void SetExperimentState(std::vector<Experiment> experiments)
      LOCKS_EXCLUDED(experiments_mutex_) {
    absl::WriterMutexLock lock(&experiments_mutex_);
    experiments_ = std::move(experiments);
  }

  // Resets the current channel value.
  void SetChannel(const std::string& channel);

  const std::string& channel() const override {
    return system_profile_.channel();
  }

  const ReleaseStage& release_stage() const override { return current_stage_; }

  // Overrides the stored SystemProfile. Useful for testing.
  void OverrideSystemProfile(const SystemProfile& profile);

 private:
  void PopulateSystemProfile();

  SystemProfile system_profile_;
  mutable absl::Mutex experiments_mutex_;
  std::vector<Experiment> experiments_ GUARDED_BY(experiments_mutex_);
  std::unique_ptr<logger::ChannelMapper> channel_mapper_;
  ReleaseStage current_stage_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_SYSTEM_DATA_H_

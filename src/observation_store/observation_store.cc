// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/observation_store/observation_store.h"

#include <utility>

#include "src/logging.h"

namespace cobalt::observation_store {

namespace {

constexpr float kAlmostFullThreshold = 0.6;

}

ObservationStore::ObservationStore(size_t max_bytes_per_observation, size_t max_bytes_per_envelope,
                                   size_t max_bytes_total)
    : max_bytes_per_observation_(max_bytes_per_observation),
      max_bytes_per_envelope_(max_bytes_per_envelope),
      max_bytes_total_(max_bytes_total),
      almost_full_threshold_(kAlmostFullThreshold * max_bytes_total_) {
  CHECK_LE(max_bytes_per_observation_, max_bytes_per_envelope_);
  CHECK_LE(max_bytes_per_envelope_, max_bytes_total_);
  CHECK_LE(0, max_bytes_per_envelope_);
}

bool ObservationStore::IsAlmostFull() const { return Size() > almost_full_threshold_; }

std::string ObservationStore::StatusDebugString(StoreStatus status) {
  switch (status) {
    case kOk:
      return "kOk";

    case kObservationTooBig:
      return "kObservationTooBig";

    case kStoreFull:
      return "kStoreFull";

    case kWriteFailed:
      return "kWriteFailed";
  }
}

uint64_t ObservationStore::num_observations_added() const {
  uint64_t num_obs = 0;
  for (const auto &count : num_obs_per_report_) {
    num_obs += count.second;
  }
  return num_obs;
}

std::vector<uint64_t> ObservationStore::num_observations_added_for_reports(
    const std::vector<uint32_t> &report_ids) const {
  std::vector<uint64_t> num_obs;
  for (const auto &id : report_ids) {
    const auto &count = num_obs_per_report_.find(id);
    if (count != num_obs_per_report_.end()) {
      num_obs.push_back(count->second);
    } else {
      num_obs.push_back(0);
    }
  }
  return num_obs;
}

void ObservationStore::ResetObservationCounter() { num_obs_per_report_.clear(); }

void ObservationStore::Disable(bool is_disabled) {
  LOG(INFO) << "ObservationStore: " << (is_disabled ? "Disabling" : "Enabling")
            << " observation storage.";
  is_disabled_ = is_disabled;
}

}  // namespace cobalt::observation_store

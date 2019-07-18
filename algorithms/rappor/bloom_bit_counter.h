// Copyright 2017 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef COBALT_ALGORITHMS_RAPPOR_BLOOM_BIT_COUNTER_H_
#define COBALT_ALGORITHMS_RAPPOR_BLOOM_BIT_COUNTER_H_

#include <memory>
#include <vector>

#include "algorithms/rappor/rappor_config_validator.h"

namespace cobalt {
namespace rappor {

// Forward declaration. See definition below.
struct CohortCounts;

// A BloomBitCounter is used for performing the first steps of a string RAPPOR
// analysis: adding the raw counts for each bit of each cohort and computing
// the estimated true counts and std errors for each bit.
//
// Usage:
// - Construct a BloomBitCounter
// - Invoke AddObservation() many times to add all of the observations.
// - Invoke EstimateCounts() to retrieve the raw bit sums, estimated counts
//   and std errors for each bit position of each cohort.
// - The accesors num_observations() and observation_errors() may be used to
//   discover the number of times AddObservation() was invoked successfully
//   and unsuccessfully.
class BloomBitCounter {
 public:
  // Constructs a BloomBitCounter for the given config. All of the observations
  // added via AddObservation() must have been encoded using this config. If
  // the config is not valid then all calls to AddObservation() will return
  // false.
  explicit BloomBitCounter(const RapporConfig& config);

  // Adds an additional observation to be counted. The observation must have
  // been encoded using the RapporConfig passed to the constructor.
  //
  // Returns true to indicate the observation was added without error and
  // so num_observations() was incremented or false to indicate there was
  // an error and so observation_errors() was incremented.
  bool AddObservation(const RapporObservation& obs);

  // The number of times that AddObservation() was invoked minus the value
  // of observation_errors().
  [[nodiscard]] size_t num_observations() const { return num_observations_; }

  // The number of times that AddObservation() was invoked and the observation
  // was discarded due to an error. If this number is not zero it indicates
  // that the Analyzer received data that was not created by a legitimate
  // Cobalt client. See the error logs for details of the errors.
  [[nodiscard]] size_t observation_errors() const {
    return observation_errors_;
  }

  // Computes estimates for the number of times each bloom bit in each cohort
  // was set. The returned vector of CohortCounts will be in order of
  // cohort number from 0 to num_cohorts - 1.
  const std::vector<CohortCounts>& EstimateCounts();

  std::shared_ptr<RapporConfigValidator> config() { return config_; }

 private:
  friend class BloomBitCounterTest;

  std::shared_ptr<RapporConfigValidator> config_;

  size_t num_observations_ = 0;
  size_t observation_errors_ = 0;

  std::vector<CohortCounts> estimated_bloom_counts_;

  // The number of bytes needed to store the bloom bits in each observation.
  size_t num_bloom_bytes_;
};

// Stores the accumulated bit sums and the adjusted count estimates
// for the bloom bits of a single cohort. A vector of CohortCounts is
// returned from BloomBitCounter::EstimateCounts().
struct CohortCounts {
  CohortCounts(uint32_t cohort_num, size_t num_bits)
      : cohort_num(cohort_num), bit_sums(num_bits, 0) {}

  // Which cohort is this?
  uint32_t cohort_num;

  // The number of valid observations seen for this cohort. These observations
  // are reflected in the counts in |bit_sums| and |count_estimates|.
  size_t num_observations = 0;

  // The raw sums for each bit position for this cohort. The sums are listed
  // in bit order "from right to left". That is, bit_sums[0] contains the sum
  // for the right-most bit, i.e. the least significant bit.
  std::vector<size_t> bit_sums;

  // The following two vectors are either empty to indicate that they
  // have not yet been computed, or else they have size equal to the size
  // of |bit_sums|. In the latter case the values are listed
  // in bit order "from right to left". That is, count_estimates[0] and
  // std_error[0] contain values for the right-most bit, i.e. the least
  // significant bit of the last byte of the Bloom filter.

  // The adjusted counts giving our estimate of the true pre-encoded count
  // for each bit.
  std::vector<double> count_estimates;

  // The standard errors corresponding to |count_estimates|.
  std::vector<double> std_errors;
};

}  // namespace rappor
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_RAPPOR_BLOOM_BIT_COUNTER_H_

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

#include "algorithms/rappor/bloom_bit_counter.h"

#include <cmath>
#include <utility>
#include <vector>

#include <glog/logging.h>

#include "util/log_based_metrics.h"

namespace cobalt::rappor {

// Stackdriver metric constants
namespace {
constexpr char kBloomBitCounterConstructorFailure[] = "bloom-bit-counter-constructor-failure";
constexpr char kAddObservationFailure[] = "bloom-bin-counter-add-observation-failure";
constexpr int kBitsPerByte = 8;
}  // namespace

BloomBitCounter::BloomBitCounter(const RapporConfig& config)
    : config_(new RapporConfigValidator(config)), num_bloom_bytes_(0) {
  if (!config_->valid()) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kBloomBitCounterConstructorFailure)
        << "RapporConfig is invalid";
    return;
  }
  estimated_bloom_counts_.reserve(config_->num_cohorts());
  const auto num_bits = config_->num_bits();
  for (size_t cohort = 0; cohort < config_->num_cohorts(); cohort++) {
    estimated_bloom_counts_.emplace_back(cohort, num_bits);
  }
  num_bloom_bytes_ = (num_bits + kBitsPerByte - 1) / kBitsPerByte;
}

bool BloomBitCounter::AddObservation(const RapporObservation& obs) {
  if (!config_->valid()) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAddObservationFailure) << "RapporConfig is invalid";
    observation_errors_++;
    return false;
  }
  if (obs.data().size() != num_bloom_bytes_) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAddObservationFailure)
        << "RapporObservation has the wrong number of bytes: " << obs.data().size()
        << ". Expecting " << num_bloom_bytes_;
    observation_errors_++;
    return false;
  }
  auto cohort = obs.cohort();
  if (cohort >= config_->num_cohorts()) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAddObservationFailure)
        << "RapporObservation has an invalid cohort index: " << cohort
        << ". num_cohorts= " << config_->num_cohorts();
    observation_errors_++;
    return false;
  }
  // We have a good observation.
  num_observations_++;
  estimated_bloom_counts_[cohort].num_observations++;

  // We iterate through the bits of the observation "from right to left",
  // i.e. from the least-significant bit of the last byte to the
  // most-significant bit of the first byte. If the ith bit is set we increment
  // bit_sums[i] for the appropriate cohort.
  //
  // NOTE(rudominer) Possible performance optimizations: Consider using x86
  // vector operations or the find-first-bit-set instruction or simply checking
  // for zero bytes.
  std::vector<size_t>& bit_sums = estimated_bloom_counts_[cohort].bit_sums;
  size_t bit_index = 0;
  for (size_t byte_index = num_bloom_bytes_ - 1; byte_index >= 0; byte_index--) {
    uint8_t bit_mask = 1;
    for (int bit_in_byte_index = 0; bit_in_byte_index < kBitsPerByte; bit_in_byte_index++) {
      if (bit_index >= bit_sums.size()) {
        return true;
      }
      if (bit_mask & obs.data()[byte_index]) {
        bit_sums[bit_index]++;
      }
      bit_index++;
      bit_mask <<= 1;
    }
  }
  return true;
}

const std::vector<CohortCounts>& BloomBitCounter::EstimateCounts() {
  double q = config_->prob_1_stays_1();
  double p = config_->prob_0_becomes_1();
  double one_minus_q_plus_p = 1.0 - (q + p);
  double divisor = q - p;  // divisor != 0 because we don't allow q == p.
  double abs_divisor = std::abs(divisor);
  // Compute the estimated counts and std_error for each cohort.
  for (auto& cohort_counts : estimated_bloom_counts_) {
    double N = cohort_counts.num_observations;
    double Npq = N * p * q;
    double correction = p * N;
    // Note(rudominer) When we support PRR then we need to modify the above
    // formulas as follows. Let f = prob_rr. Then let
    // p11        = q * (1 - f/2) + p * f / 2;
    // p01        = p * (1 - f/2) + q * f / 2;
    // correction = p01 * N;
    // divisor    = p11 - p01;  // (1 - f) * (q - p)

    std::vector<size_t>& bit_sums = cohort_counts.bit_sums;
    std::vector<double>& count_estimates = cohort_counts.count_estimates;
    std::vector<double>& std_errors = cohort_counts.std_errors;
    count_estimates.resize(bit_sums.size());
    std_errors.resize(bit_sums.size());

    // Compute the estimate count and std_error for each bloom bit for the
    // current cohort.
    for (size_t bit_index = 0; bit_index < bit_sums.size(); bit_index++) {
      double Y = bit_sums[bit_index];
      // See go/cobalt-basic-rappor-analysis for an explanation of the
      // formulas we use for count_estimate and std_error.
      count_estimates[bit_index] = (Y - correction) / divisor;
      std_errors[bit_index] = sqrt(Y * one_minus_q_plus_p + Npq) / abs_divisor;
    }
  }

  return estimated_bloom_counts_;
}

}  // namespace cobalt::rappor

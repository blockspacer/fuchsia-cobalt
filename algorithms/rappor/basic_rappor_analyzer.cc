// Copyright 2016 The Fuchsia Authors
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

#include "algorithms/rappor/basic_rappor_analyzer.h"

#include <cmath>
#include <vector>

#include <glog/logging.h>

#include "util/log_based_metrics.h"

namespace cobalt::rappor {

// Stackdriver metric constants
namespace {
constexpr char kBasicRapporAnalyzerConstructorFailure[] =
    "basic-rappor-analyzer-constructor-failure";
constexpr char kAddObservationFailure[] = "basic-rappor-analyzer-add-observation-failure";
constexpr uint32_t kBitsPerByte = 8;
}  // namespace

BasicRapporAnalyzer::BasicRapporAnalyzer(const BasicRapporConfig& config)
    : config_(new RapporConfigValidator(config)), num_encoding_bytes_(0) {
  if (!config_->valid()) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kBasicRapporAnalyzerConstructorFailure)
        << "BasicRapporConfig is invalid";
    return;
  }
  category_counts_.resize(config_->num_bits(), 0);
  num_encoding_bytes_ = (config_->num_bits() + kBitsPerByte - 1) / kBitsPerByte;
}

bool BasicRapporAnalyzer::AddObservation(const BasicRapporObservation& obs) {
  if (!config_->valid()) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAddObservationFailure) << "BasicRapporConfig is invalid";
    observation_errors_++;
    return false;
  }
  if (obs.data().size() != num_encoding_bytes_) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAddObservationFailure)
        << "BasicRapporObservation has the wrong number of bytes: " << obs.data().size()
        << ". Expecting " << num_encoding_bytes_;
    observation_errors_++;
    return false;
  }
  // We have a good observation.
  num_observations_++;

  // We iterate through the bits of the observation "from right to left",
  // i.e. from the least-significant bit of the last byte to the
  // most-significant bit of the first byte. If the ith bit is set we increment
  // category_counts_[i].
  //
  // NOTE(rudominer) Possible performance optimizations: Consider using x86
  // vector operations or the find-first-bit-set instruction or simply checking
  // for zero bytes.
  size_t category = 0;
  for (auto byte_index = obs.data().size() - 1; byte_index >= 0; byte_index--) {
    uint8_t bit_mask = 1;
    for (uint32_t bit_index = 0; bit_index < kBitsPerByte; bit_index++) {
      if (category >= category_counts_.size()) {
        return true;
      }
      if (bit_mask & obs.data()[byte_index]) {
        category_counts_[category]++;
      }
      category++;
      bit_mask <<= 1;
    }
  }
  return true;
}

std::vector<BasicRapporAnalyzer::CategoryResult> BasicRapporAnalyzer::Analyze() {
  double q = config_->prob_1_stays_1();
  double p = config_->prob_0_becomes_1();
  double N = num_observations_;
  double one_minus_q_plus_p = 1.0 - (q + p);
  double Npq = N * p * q;
  double correction = p * N;
  double divisor = q - p;  // divisor != 0 because we don't allow q == p.
  double abs_divisor = fabs(divisor);
  // Note(rudominer) When we support PRR then we need to modify the above
  // formulas as follows. Let f = prob_rr. Then let
  // p11        = q * (1 - f/2) + p * f / 2;
  // p01        = p * (1 - f/2) + q * f / 2;
  // correction = p01 * N;
  // divisor    = p11 - p01;  // (1 - f) * (q - p)

  // Create a vector of empty results.
  std::vector<BasicRapporAnalyzer::CategoryResult> results(config_->num_bits());
  int category_index = 0;

  // Iterate through the vector and fill in the results.
  for (auto& result : results) {
    result.category = config_->categories().at(category_index);
    double Y = category_counts_[category_index];
    // See go/cobalt-basic-rappor-analysis for an explanation of the
    // formulas we use for count_estimate and std_error.
    result.count_estimate = (Y - correction) / divisor;
    result.std_error = sqrt(Y * one_minus_q_plus_p + Npq) / abs_divisor;
    category_index++;
  }

  return results;
}

}  // namespace cobalt::rappor

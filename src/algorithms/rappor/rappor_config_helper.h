// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_RAPPOR_RAPPOR_CONFIG_HELPER_H_
#define COBALT_SRC_ALGORITHMS_RAPPOR_RAPPOR_CONFIG_HELPER_H_

#include <string>

#include "config/metric_definition.pb.h"
#include "config/report_definition.pb.h"

namespace cobalt {
namespace rappor {

class RapporConfigHelper {
 public:
  // Sentinel value returned by ProbBitFlip() when the ReportDefinition
  // does not contain the necessary settings to determine a value
  // for the probability of flipping a bit.
  static const float kInvalidProbability;  // = -1

  // We do not support RAPPOR's PRR in Cobalt.
  static const float kProbRR;  // = 0

  // For String RAPPOR's number of hashes, always use h = 2.
  static const size_t kNumHashes = 2;

  // Returns the probability of flipping a bit in the RAPPOR encoding,
  // or kInvalidProbability.
  //
  // |metric_debug_name| should be the fully qualified name of the containing
  // MetricDefinition (including the customer and project). This will be
  // used to form a logged error message in case of an error.
  static float ProbBitFlip(const ReportDefinition& report_definition,
                           const std::string& metric_debug_name);

  // Returns the number of categories to use for the Basic RAPPOR encoding.
  // This is the same as the number of bits.
  static size_t BasicRapporNumCategories(const MetricDefinition& metric_definition);

  // Returns the number of cohorts to use for the String RAPPOR encoding.
  static size_t StringRapporNumCohorts(const ReportDefinition& report_definition);

  // Returns the number of bits to use for the String RAPPOR encoding.
  static size_t StringRapporNumBloomBits(const ReportDefinition& report_definition);
};

}  // namespace rappor
}  // namespace cobalt

#endif  // COBALT_SRC_ALGORITHMS_RAPPOR_RAPPOR_CONFIG_HELPER_H_

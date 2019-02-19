// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "algorithms/rappor/rappor_config_helper.h"

#include <string>

#include "./logging.h"

namespace cobalt {
namespace rappor {

// Sentinel value returned by ProbBitFlip() when the ReportDefinition
// does not contain the necessary settings to determine a value
// for the probability of flipping a bit.
const float RapporConfigHelper::kInvalidProbability = -1;

// We do not support RAPPOR's PRR in Cobalt.
const float RapporConfigHelper::kProbRR = 0.0;

////////////////////////////    p, q and f    //////////////////////////////////

// This is relevant to Basic RAPPOR (via the SIMPLE_OCCURRENCE_COUNT
// Report type) and String RAPPOR (via the HIGH_FREQUENCY_STRING_COUNTS Report
// type.)

// The mapping from different values of LocalPrivacyNoiseLevel to
// RAPPOR parameters p and q. The user sets LocalPrivacyNoiseLevel on
// a per-report basis. We always use q = 1 - p and so we don't need to
// configure two values, only a single value we call ProbBitFlip. We
// set p = ProbBitFlip, q = 1 - ProbBitFlip.

// NONE
static const float kLocalPrivacyNoneProbBitFlip = 0.0;

// SMALL
static const float kLocalPrivacySmallProbBitFlip = 0.01;

// MEDIUM
static const float kLocalPrivacyMediumProbBitFlip = 0.1;

// LARGE
static const float kLocalPrivacyLargeProbBitFlip = 0.25;

////////////////////////    m = num_cohorts    ///////////////////////////////

// This is relevant to String RAPPOR (via the HIGH_FREQUENCY_STRING_COUNTS
// Report type.)

// |expected_population_size| is the user's estimate of the number of Fuchsia
// devices from which data is collected.

// The mapping from expected_population_size to num_cohorts

// This is the number of cohorts to use if expected_population_size is not set.
static const size_t kDefaultNumCohorts = 50;

// For estimated_population_size < 100 we use 5 cohorts.
static const size_t kTinyPopulationSize = 100;
static const size_t kTinyNumCohorts = 5;

// For 100 <= estimated_population_size < 1000 we use 10 cohorts.
static const size_t kSmallPopulationSize = 1000;
static const size_t kSmallNumCohorts = 10;

// For 1,000 <= estimated_population_size < 10,000 we use 50 cohorts.
static const size_t kMediumPopulationSize = 10000;
static const size_t kMediumNumCohorts = 50;

// For estimated_population_size >= 10,000 we use 100 cohorts
static const size_t kLargeNumCohorts = 100;

////////////////////////    k = num_bloom_bits    //////////////////////////////

// This is relevant to String RAPPOR (via the HIGH_FREQUENCY_STRING_COUNTS
// Report type.)

// |expected_string_set_size| is the user's estimate of the number of candidate
// strings. This cannot be changed once data collection begins.

// The mapping from expected_string_set_size to num_bloom_bits

// This is the number of bits to use if expected_string_set_size is not set.
static const size_t kDefaultNumBits = 32;

// For estimated_string_set_size < 100 we use 8 bits.
static const size_t kTinyNumCandidates = 100;
static const size_t kTinyNumBits = 8;

// For 100 <= estimated_string_set_size < 1000 we use 16 bits.
static const size_t kSmallNumCandidates = 1000;
static const size_t kSmallNumBits = 16;

// For 1,000 <= estimated_string_set_size < 10,000 we use 64 bits.
static const size_t kMediumNumCandidates = 10000;
static const size_t kMediumNumBits = 64;

// For estimated_string_set_size >= 10,000 we use 128 bits
static const size_t kLargeNumBits = 128;

float RapporConfigHelper::ProbBitFlip(const ReportDefinition& report_definition,
                                      const std::string& metric_debug_name) {
  switch (report_definition.local_privacy_noise_level()) {
    case ReportDefinition::NONE:
      return kLocalPrivacyNoneProbBitFlip;

    case ReportDefinition::SMALL:
      return kLocalPrivacySmallProbBitFlip;

    case ReportDefinition::MEDIUM:
      return kLocalPrivacyMediumProbBitFlip;

    case ReportDefinition::LARGE:
      return kLocalPrivacyLargeProbBitFlip;

    default:
      LOG(ERROR) << "Invalid Cobalt config: Report "
                 << report_definition.report_name() << " from metric "
                 << metric_debug_name
                 << " does not have local_privacy_noise_level set to a "
                    "recognized value.";
      return kInvalidProbability;
  }
}

size_t RapporConfigHelper::BasicRapporNumCategories(
    const MetricDefinition& metric_definition) {
  return metric_definition.max_event_code() + 1;
}

size_t RapporConfigHelper::StringRapporNumCohorts(
    const ReportDefinition& report_definition) {
  if (report_definition.expected_population_size() == 0) {
    return kDefaultNumCohorts;
  } else if (report_definition.expected_population_size() <
             kTinyPopulationSize) {
    return kTinyNumCohorts;
  } else if (report_definition.expected_population_size() <
             kSmallPopulationSize) {
    return kSmallNumCohorts;
  } else if (report_definition.expected_population_size() <
             kMediumPopulationSize) {
    return kMediumNumCohorts;
  } else {
    return kLargeNumCohorts;
  }
}

size_t RapporConfigHelper::StringRapporNumBloomBits(
    const ReportDefinition& report_definition) {
  if (report_definition.expected_string_set_size() == 0) {
    return kDefaultNumBits;
  } else if (report_definition.expected_string_set_size() <
             kTinyNumCandidates) {
    return kTinyNumBits;
  } else if (report_definition.expected_string_set_size() <
             kSmallNumCandidates) {
    return kSmallNumBits;
  } else if (report_definition.expected_string_set_size() <
             kMediumNumCandidates) {
    return kMediumNumBits;
  } else {
    return kLargeNumBits;
  }
}

}  // namespace rappor
}  // namespace cobalt

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/algorithms/rappor/rappor_config_helper.h"

#include <string>

#include "src/logging.h"

namespace cobalt::rappor {

// Sentinel value returned by ProbBitFlip() when the ReportDefinition
// does not contain the necessary settings to determine a value
// for the probability of flipping a bit.
constexpr float RapporConfigHelper::kInvalidProbability = -1;

// We do not support RAPPOR's PRR in Cobalt.
constexpr float RapporConfigHelper::kProbRR = 0.0;

////////////////////////////    p, q and f    //////////////////////////////////
// This is relevant to Basic RAPPOR (via the SIMPLE_OCCURRENCE_COUNT
// and UNIQUE_N_DAY_ACTIVES Report types).
//
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
      LOG(ERROR) << "Invalid Cobalt config: Report " << report_definition.report_name()
                 << " from metric " << metric_debug_name
                 << " does not have local_privacy_noise_level set to a "
                    "recognized value.";
      return kInvalidProbability;
  }
}

// Calculates the number of categories based on the metric_definition.
//
// - If there is exactly 1 metric_dimensions, return
//   metric_dimensions[0].max_event_code() + 1 (this is the new registry).
//
// - Otherwise, return 0 and report an error (this is not a supported registry).
size_t RapporConfigHelper::BasicRapporNumCategories(const MetricDefinition& metric_definition) {
  if (metric_definition.metric_dimensions_size() == 1) {
    return metric_definition.metric_dimensions(0).max_event_code() + 1;
  }

  LOG(ERROR) << "Invalid Cobalt registry: Metric " << metric_definition.metric_name() << " has "
             << metric_definition.metric_dimensions_size()
             << " metric_dimensions. (expected exactly 1)";
  return 0;
}

}  // namespace cobalt::rappor

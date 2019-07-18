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

#ifndef COBALT_ALGORITHMS_RAPPOR_BASIC_RAPPOR_ANALYZER_H_
#define COBALT_ALGORITHMS_RAPPOR_BASIC_RAPPOR_ANALYZER_H_

#include <memory>
#include <vector>

#include "./observation.pb.h"
#include "algorithms/rappor/rappor_config_validator.h"
#include "config/encodings.pb.h"

namespace cobalt {
namespace rappor {

class BasicRapporAnalyzer {
 public:
  // Constructs a BasicRapporAnalyzer for the given config. All of the
  // observations added via AddObservation() must have been encoded using this
  // config. If the config is not valid then all calls to AddObservation()
  // will return false.
  // TODO(rudominer) Enhance this API to also accept DP release parameters.
  explicit BasicRapporAnalyzer(const BasicRapporConfig& config);

  // Adds an additional observation to be analyzed. The observation must have
  // been encoded using the BasicRapporConfig passed to the constructor.
  //
  // Returns true to indicate the observation was added without error and
  // so num_observations() was incremented or false to indicate there was
  // an error and so observation_errors() was incremented.
  bool AddObservation(const BasicRapporObservation& obs);

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

  // The number of categories being analyzed.
  [[nodiscard]] size_t num_categories() const {
    return category_counts_.size();
  }

  struct CategoryResult {
    ValuePart category;
    // An unbiased estimate of the true count for this category. Note that
    // in order to maintain unbiasedness we allow count_estimate to be
    // greater than num_observations() or less than zero. One may wish to
    // clip to [0, num_observations()] before displaying to a user.
    double count_estimate;

    // Multiply this value by z_{alpha/2} to obtain the radius of an approximate
    // 100(1 - alpha)% confidence interval. For example an approximate 95%
    // confidence interval for the count is given by
    // (count_estimate - 1.96*std_error, count_estimate + 1.96 * std_error)
    // because 1.96 ~= z_{.025} meaning that P(Z > 1.96) ~= 0.025 where
    // Z ~ Normal(0, 1).
    double std_error;
  };

  // Performs Basic RAPPOR analysis on the observations added via
  // AddObservation() and returns the results. The returned vector of
  // CategoryResults will have length equal to the number of categories
  // and will be in the category order specified in the config.
  std::vector<CategoryResult> Analyze();

 private:
  friend class BasicRapporAnalyzerTest;

  // Gives access to the raw counts for each category based on the observations
  // added via AddObservation(). This is mostly useful for tests.
  const std::vector<size_t>& raw_category_counts() { return category_counts_; }

  std::unique_ptr<RapporConfigValidator> config_;
  size_t num_observations_ = 0;
  size_t observation_errors_ = 0;

  // The raw counts for each category based on the observations added
  // via AddObservation().
  std::vector<size_t> category_counts_;

  // The number of bytes used to encode observations. This is a function
  // of the |config_|.
  size_t num_encoding_bytes_;
};

}  // namespace rappor
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_RAPPOR_BASIC_RAPPOR_ANALYZER_H_

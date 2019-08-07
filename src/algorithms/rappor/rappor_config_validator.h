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

#ifndef COBALT_SRC_ALGORITHMS_RAPPOR_RAPPOR_CONFIG_VALIDATOR_H_
#define COBALT_SRC_ALGORITHMS_RAPPOR_RAPPOR_CONFIG_VALIDATOR_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/pb/observation.pb.h"
#include "src/registry/encodings.pb.h"

namespace cobalt {
namespace rappor {

class RapporConfigValidator {
 public:
  // Constructor for String RAPPOR
  explicit RapporConfigValidator(const RapporConfig& config);

  // Constructor for Basic RAPPOR
  explicit RapporConfigValidator(const BasicRapporConfig& config);

  ~RapporConfigValidator() = default;

  float prob_0_becomes_1() { return prob_0_becomes_1_; }

  float prob_1_stays_1() { return prob_1_stays_1_; }

  bool valid() { return valid_; }

  uint32_t num_bits() { return num_bits_; }
  uint32_t num_hashes() { return num_hashes_; }
  uint32_t num_cohorts() { return num_cohorts_; }
  uint32_t num_cohorts_2_power() { return num_cohorts_2_power_; }

  // Returns the bit-index of |category| or -1 if |category| is not one of the
  // basic RAPPOR categories (or if this object was not initialized with a
  // BasicRapporConfig.)
  int bit_index(const ValuePart& category);

  // Gives access to the vector of Categories if this object was initialized
  // with a BasicRapporConfig.
  std::vector<ValuePart>& categories() { return categories_; }

 private:
  friend class RapporConfigValidatorTest_TestMinPower2Above_Test;

  // Returns the least power of 2 greater than or equal to x.
  static uint32_t MinPower2Above(uint16_t x);

  bool valid_;
  float prob_0_becomes_1_;
  float prob_1_stays_1_;
  uint32_t num_bits_;

  // Used only in string RAPPOR
  uint32_t num_hashes_;
  uint32_t num_cohorts_;
  // This is the least power of 2 greater than or equal to num_cohorts_.
  uint32_t num_cohorts_2_power_;

  // Used only in Basic RAPPOR. |categories_| is the list of all
  // categories. The keys to |category_to_bit_index_| are serialized
  // ValueParts.
  std::map<std::string, size_t> category_to_bit_index_;
  std::vector<ValuePart> categories_;
};

}  // namespace rappor
}  // namespace cobalt

#endif  // COBALT_SRC_ALGORITHMS_RAPPOR_RAPPOR_CONFIG_VALIDATOR_H_

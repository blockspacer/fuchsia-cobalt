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

#include "src/algorithms/rappor/rappor_config_validator.h"

#include <string>
#include <vector>

#include "src/logging.h"
#include "src/pb/observation.pb.h"

namespace cobalt::rappor {

namespace {
// Factors out some common validation logic.
bool CommonValidate(float prob_0_becomes_1, float prob_1_stays_1, float prob_rr) {
  if (prob_0_becomes_1 < 0.0 || prob_0_becomes_1 > 1.0) {
    LOG(ERROR) << "prob_0_becomes_1 is not valid";
    return false;
  }
  if (prob_1_stays_1 < 0.0 || prob_1_stays_1 > 1.0) {
    LOG(ERROR) << "prob_1_stays_1 < 0.0  is not valid";
    return false;
  }
  if (prob_0_becomes_1 == prob_1_stays_1) {
    LOG(ERROR) << "prob_0_becomes_1 == prob_1_stays_1";
    return false;
  }
  if (prob_rr != 0.0) {
    LOG(ERROR) << "prob_rr not supported";
    return false;
  }
  return true;
}

constexpr int64_t max_num_categories = 1024;
// Extracts the categories from |config| and populates |*categories|.  We
// support string and integer categories and we use ValueParts to represent
// these two uniformly. Returns true if |config| is valid or  false otherwise.
bool ExtractCategories(const BasicRapporConfig& config, std::vector<ValuePart>* categories) {
  switch (config.categories.categories_case()) {
    case BasicRapporConfig::kStringCategories: {
      int64_t num_categories = config.categories.strings().size();
      if (num_categories <= 1 || num_categories >= max_num_categories) {
        return false;
      }
      for (const auto& category : config.categories.strings()) {
        if (category.empty()) {
          return false;
        }
        ValuePart value_part;
        value_part.set_string_value(category);
        categories->push_back(value_part);
      }
    } break;
    case BasicRapporConfig::kIntRangeCategories: {
      int64_t first = config.categories.int_range().first;
      int64_t last = config.categories.int_range().last;
      int64_t num_categories = last - first + 1;
      if (last <= first || num_categories >= max_num_categories) {
        return false;
      }
      for (int64_t category = first; category <= last; category++) {
        ValuePart value_part;
        value_part.set_int_value(category);
        categories->push_back(value_part);
      }
    } break;
    case BasicRapporConfig::kIndexedCategories: {
      int64_t num_categories = config.categories.indexed();
      if (num_categories >= max_num_categories) {
        LOG(ERROR) << "BasicRappor: The maximum number of categories is 1024";
        return false;
      }
      for (auto i = 0; i < num_categories; i++) {
        ValuePart value_part;
        value_part.set_index_value(i);
        categories->push_back(value_part);
      }
    } break;
    default:
      return false;
  }
  return true;
}

}  // namespace

uint32_t RapporConfigValidator::MinPower2Above(uint16_t x) {
  // See http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
  if (x == 0) {
    return 1;
  }
  uint32_t v = x - 1;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;  // NOLINT readability-magic-numbers
  return v + 1;
}

RapporConfigValidator::RapporConfigValidator(const BasicRapporConfig& config)
    : prob_0_becomes_1_(config.prob_0_becomes_1), prob_1_stays_1_(config.prob_1_stays_1) {
  valid_ = false;
  if (!CommonValidate(prob_0_becomes_1_, prob_1_stays_1_, config.prob_rr)) {
    return;
  }
  if (!ExtractCategories(config, &categories_)) {
    return;
  }
  num_bits_ = categories_.size();

  // Insert all of the categories into the map.
  size_t index = 0;
  for (const auto& category : categories_) {
    std::string serialized_value_part;
    category.SerializeToString(&serialized_value_part);
    auto result = category_to_bit_index_.emplace(serialized_value_part, index++);
    if (!result.second) {
      return;
    }
  }

  valid_ = true;
}

// Returns the bit-index of |category| or -1 if |category| is not one of the
// basic RAPPOR categories.
int RapporConfigValidator::bit_index(const ValuePart& category) {
  std::string serialized_value;
  category.SerializeToString(&serialized_value);
  auto iterator = category_to_bit_index_.find(serialized_value);
  if (iterator == category_to_bit_index_.end()) {
    return -1;
  }
  return iterator->second;
}

}  // namespace cobalt::rappor

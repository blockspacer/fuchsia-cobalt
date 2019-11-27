// Copyright 2018 The Fuchsia Authors. All rights reserved.  Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE
// file.

#ifndef COBALT_SRC_ALGORITHMS_RAPPOR_RAPPOR_CONFIG_H_
#define COBALT_SRC_ALGORITHMS_RAPPOR_RAPPOR_CONFIG_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "src/logging.h"

namespace cobalt::rappor {

struct BasicRapporConfig {
  // All probabilities below must be in the range [0.0, 1.0].

  // prob_0_becomes_1 MAY NOT BE EQUAL to prob_1_stays_1.

  // p = p(a zero bit is changed to a one bit in the IRR)
  float prob_0_becomes_1 = 0.0;

  // q = p(a one bit remains a one bit in the IRR)
  float prob_1_stays_1 = 0.0;

  // f = p(a bit is randomly assigned a value in the PRR)
  // NOTE: PRR is not implemented in Version 0.1 of Cobalt.
  // This value must not be set to a non-zero value.
  float prob_rr = 0.0;

  enum CategoriesCase {
    kStringCategories,
    kIntRangeCategories,
    kIndexedCategories,

    kUnset,
  };

  class Categories {
   public:
    struct IntRange {
      int32_t first;
      int32_t last;
    };

    [[nodiscard]] CategoriesCase categories_case() const { return case_; }

    void set_strings(std::vector<std::string> categories) {
      case_ = kStringCategories;
      strings_ = std::move(categories);
    }

    void add_string(const std::string& category) {
      if (case_ != kStringCategories) {
        case_ = kStringCategories;
        strings_ = {};
      }
      strings_.push_back(category);
    }

    [[nodiscard]] const std::vector<std::string>& strings() const {
      CHECK(case_ == kStringCategories) << "Invalid category";
      return strings_;
    }

    void set_int_range(uint32_t first, uint32_t last) {
      case_ = kIntRangeCategories;
      int_range_.first = first;
      int_range_.last = last;
    }

    [[nodiscard]] const IntRange& int_range() const {
      CHECK(case_ == kIntRangeCategories) << "Invalid category";
      return int_range_;
    }

    void set_indexed(uint32_t indexed) {
      case_ = kIndexedCategories;
      indexed_ = indexed;
    }

    [[nodiscard]] uint32_t indexed() const {
      CHECK(case_ == kIndexedCategories) << "Invalid category";
      return indexed_;
    }

   private:
    CategoriesCase case_ = kUnset;
    std::vector<std::string> strings_ = {};
    IntRange int_range_ = {};
    uint32_t indexed_ = 0;
  };

  // For basic RAPPOR the Encoder needs to know something about the list of
  // categories. For encoding metric parts of type STRING |string_categories|
  // should be used, for encoding metric parts of type INT
  // |int_range_categories| should be used and for encoding metric parts of
  // type INDEX, |indexed_categories| should be used.
  Categories categories;
};

}  // namespace cobalt::rappor

#endif  // COBALT_SRC_ALGORITHMS_RAPPOR_RAPPOR_CONFIG_H_

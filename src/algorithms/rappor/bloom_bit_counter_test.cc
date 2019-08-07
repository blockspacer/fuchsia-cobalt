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
#include "src/algorithms/rappor/bloom_bit_counter.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <gflags/gflags.h>

#include "src/algorithms/rappor/rappor_encoder.h"
#include "src/algorithms/rappor/rappor_test_utils.h"
#include "src/lib/crypto_util/random_test_utils.h"
#include "src/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace rappor {

namespace {
// Makes a RapporConfig with the given data (and num_hashes=2).
RapporConfig Config(uint32_t num_bloom_bits, uint32_t num_cohorts, float p, float q) {
  RapporConfig config;
  config.set_num_bloom_bits(num_bloom_bits);
  config.set_num_hashes(2);
  config.set_num_cohorts(num_cohorts);
  config.set_prob_0_becomes_1(p);
  config.set_prob_1_stays_1(q);
  return config;
}

}  // namespace

class BloomBitCounterTest : public ::testing::Test {
 protected:
  // Sets the member variable bit_counter_ to a new BloomBitCounter configured
  // with the given arguments and the current values of prob_0_becomes_1_,
  // prob_1_stays_1_.
  void SetBitCounter(uint32_t num_bloom_bits, uint32_t num_cohorts) {
    bit_counter_ = std::make_unique<BloomBitCounter>(
        Config(num_bloom_bits, num_cohorts, prob_0_becomes_1_, prob_1_stays_1_));
    add_good_observation_call_count_ = 0;
    add_bad_observation_call_count_ = 0;
  }

  // Adds an observation to bit_counter_ described by |binary_string| for the
  // given cohort. Expects the operation to result in an error.
  void AddObservationExpectFalse(uint32_t cohort, const std::string &binary_string) {
    EXPECT_FALSE(bit_counter_->AddObservation(RapporObservationFromString(cohort, binary_string)));
    CheckState(add_good_observation_call_count_, ++add_bad_observation_call_count_);
  }

  // Adds an observation to bit_counter_ described by |binary_string| for the
  // given cohort. Expects the operation to succeed.
  void AddObservation(uint32_t cohort, const std::string &binary_string) {
    EXPECT_TRUE(bit_counter_->AddObservation(RapporObservationFromString(cohort, binary_string)));
    CheckState(++add_good_observation_call_count_, add_bad_observation_call_count_);
  }

  // Invokes AddObservation() many times.
  void AddObservations(uint32_t cohort, const std::string &binary_string, int num_times) {
    for (int count = 0; count < num_times; count++) {
      SCOPED_TRACE(std::string("count=") + std::to_string(count));
      AddObservation(cohort, binary_string);
    }
  }

  // Checks that bit_counter_ has the expected number observations and errors.
  void CheckState(size_t expected_num_observations, size_t expected_observation_errors) {
    EXPECT_EQ(expected_num_observations, bit_counter_->num_observations());
    EXPECT_EQ(expected_observation_errors, bit_counter_->observation_errors());
  }

  // Checks that bit_counter_ has the expected raw count for the given cohort
  // and bit index.
  void ExpectRawCount(uint32_t cohort, size_t index, size_t expected_count) {
    EXPECT_EQ(expected_count, bit_counter_->estimated_bloom_counts_[cohort].bit_sums[index]);
  }

  // Checks that bit_counter_ has the expected raw counts for the given cohort.
  void ExpectRawCounts(uint32_t cohort, const std::vector<size_t> &expected_counts) {
    EXPECT_EQ(expected_counts, bit_counter_->estimated_bloom_counts_[cohort].bit_sums);
  }

  // Invokes bit_countr_->EstimateCounts() and checks the count and std_error in
  // the given bit index for the given cohort.
  void EstimateCountsAndCheck(size_t cohort, size_t index, double expected_estimate,
                              double expected_std_err) {
    auto estimated_counts = bit_counter_->EstimateCounts();
    ASSERT_GT(estimated_counts.size(), cohort)
        << "size=" << estimated_counts.size() << ", cohort=" << cohort;
    ASSERT_GT(estimated_counts[cohort].count_estimates.size(), index)
        << "size=" << estimated_counts[cohort].count_estimates.size() << ", index=" << index;
    ASSERT_GT(estimated_counts[cohort].std_errors.size(), index)
        << "size=" << estimated_counts[cohort].std_errors.size() << ", index=" << index;
    EXPECT_FLOAT_EQ(expected_estimate, estimated_counts[cohort].count_estimates[index]);
    EXPECT_FLOAT_EQ(expected_std_err, estimated_counts[cohort].std_errors[index]);
  }

  // Tests BloomBitCounter focusing on only a single bit at a time.
  //
  // We use 32 bits and 5 cohorts. Multiple times we single out one bit and one
  // cohort. Only that bit receives any non-zero observation data and only that
  // bit is validated.
  //
  // Uses the currently set values for prob_0_becomes_1_ and prob_1_stays_1_.
  // There will be |n| total observations with |y| 1's and |n-y| 0's.
  void OneBitTest(int n, int y, double expected_estimate, double expected_std_err) {
    // We pick five different bits out of the 32 bits to test.
    for (int bit_index : {0, 1, 8, 19, 31}) {
      // We pick five different cohorts of the 100 cohorts to test.
      for (int cohort : {0, 1, 47, 61, 99}) {
        SCOPED_TRACE(std::to_string(bit_index) + ", " + std::to_string(cohort) + ", " +
                     std::to_string(n) + ", " + std::to_string(y));
        // Construct a BloomBitCounter with 32 bits and 100 cohorts.
        SetBitCounter(32, 100);  // NOLINT
        // Add y observations with a 1 in position |bit_index|.
        // NOLINTNEXTLINE
        AddObservations(cohort, BuildBitPatternString(32, bit_index, '1', '0'), y);
        // Add n-y observations with a 0 in position |bit_index|.
        // NOLINTNEXTLINE
        AddObservations(cohort, BuildBitPatternString(32, bit_index, '0', '0'), n - y);

        // Analyze and check position |bit_index|
        EstimateCountsAndCheck(cohort, bit_index, expected_estimate, expected_std_err);
      }
    }
  }

 public:
  // By default this test uses p=0, q=1. Individual tests may override this.
  float prob_0_becomes_1_ = 0.0;
  float prob_1_stays_1_ = 1.0;
  std::unique_ptr<BloomBitCounter> bit_counter_;
  int add_bad_observation_call_count_ = 0;
  int add_good_observation_call_count_ = 0;
};

// Tests the raw counts when there are four bits and two cohorts
TEST_F(BloomBitCounterTest, RawCounts4x2) {
  // Construct a bloom bit counter with four bits and 2 cohorts.
  SetBitCounter(4, 2);

  AddObservation(0, "00000000");
  ExpectRawCounts(0, {0, 0, 0, 0});
  ExpectRawCounts(1, {0, 0, 0, 0});

  AddObservation(1, "00000000");
  ExpectRawCounts(0, {0, 0, 0, 0});
  ExpectRawCounts(1, {0, 0, 0, 0});

  AddObservation(0, "00000001");
  ExpectRawCounts(0, {1, 0, 0, 0});
  ExpectRawCounts(1, {0, 0, 0, 0});

  AddObservation(0, "00000001");
  ExpectRawCounts(0, {2, 0, 0, 0});
  ExpectRawCounts(1, {0, 0, 0, 0});

  AddObservation(0, "00000010");
  ExpectRawCounts(0, {2, 1, 0, 0});
  ExpectRawCounts(1, {0, 0, 0, 0});

  AddObservation(0, "00000010");
  ExpectRawCounts(0, {2, 2, 0, 0});
  ExpectRawCounts(1, {0, 0, 0, 0});

  AddObservation(1, "00000010");
  ExpectRawCounts(0, {2, 2, 0, 0});
  ExpectRawCounts(1, {0, 1, 0, 0});

  AddObservation(1, "00000010");
  ExpectRawCounts(0, {2, 2, 0, 0});
  ExpectRawCounts(1, {0, 2, 0, 0});

  AddObservation(0, "00000011");
  ExpectRawCounts(0, {3, 3, 0, 0});
  ExpectRawCounts(1, {0, 2, 0, 0});

  AddObservation(0, "00000100");
  ExpectRawCounts(0, {3, 3, 1, 0});
  ExpectRawCounts(1, {0, 2, 0, 0});

  AddObservation(0, "00000101");
  ExpectRawCounts(0, {4, 3, 2, 0});
  ExpectRawCounts(1, {0, 2, 0, 0});

  AddObservation(0, "00000011");
  ExpectRawCounts(0, {5, 4, 2, 0});  // NOLINT
  ExpectRawCounts(1, {0, 2, 0, 0});

  AddObservation(1, "00001010");
  ExpectRawCounts(0, {5, 4, 2, 0});  // NOLINT
  ExpectRawCounts(1, {0, 3, 0, 1});

  AddObservation(1, "00001010");
  ExpectRawCounts(0, {5, 4, 2, 0});  // NOLINT
  ExpectRawCounts(1, {0, 4, 0, 2});

  for (int i = 0; i < 1000; i++) {  // NOLINT
    AddObservation(0, "00001100");
    AddObservation(1, "00000110");
  }
  ExpectRawCounts(0, {5, 4, 1002, 1000});  // NOLINT
  ExpectRawCounts(1, {0, 1004, 1000, 2});  // NOLINT

  // The extra high-order-bits should be ignored
  AddObservation(0, "11110000");
  AddObservation(1, "11110000");
  ExpectRawCounts(0, {5, 4, 1002, 1000});  // NOLINT
  ExpectRawCounts(1, {0, 1004, 1000, 2});  // NOLINT
}

// Tests the raw counts when there are 1024 bits and 100 cohorts
TEST_F(BloomBitCounterTest, RawCounts1024x100) {
  // Construct a bloom bit counter with 1024 bits and 100 cohorts.
  SetBitCounter(1024, 100);  // NOLINT
  // Iterate 100 times
  for (int iteration = 0; iteration < 100; iteration++) {  // NOLINT
    // For i = 0, 10, 20, 30, .....
    for (int bit_index = 0; bit_index < 1024; bit_index += 10) {  // NOLINT
      // Add observations with bit i alone set for cohorts 0, 51, 97.
      // NOLINTNEXTLINE
      AddObservation(0, BuildBitPatternString(1024, bit_index, '1', '0'));
      // NOLINTNEXTLINE
      AddObservation(51, BuildBitPatternString(1024, bit_index, '1', '0'));
      // NOLINTNEXTLINE
      AddObservation(97, BuildBitPatternString(1024, bit_index, '1', '0'));
    }
  }

  // Check the counts.
  for (int bit_index = 0; bit_index < 1024; bit_index++) {    // NOLINT
    size_t expected_count = (bit_index % 10 == 0 ? 100 : 0);  // NOLINT
    ExpectRawCount(0, bit_index, expected_count);
    ExpectRawCount(1, bit_index, 0);
    ExpectRawCount(51, bit_index, expected_count);  // NOLINT
    ExpectRawCount(52, bit_index, 0);               // NOLINT
    ExpectRawCount(97, bit_index, expected_count);  // NOLINT
    ExpectRawCount(98, bit_index, 0);               // NOLINT
  }
}

// Tests that AddObservation() returns false when an invalid config is
// provided to the constructor.
TEST_F(BloomBitCounterTest, InvalidConfig) {
  // Set prob_0_becomes_1 to an invalid value.
  prob_0_becomes_1_ = 1.1;  // NOLINT

  // Construct a bloom bit counter with 8 bits and 2 cohorts.
  SetBitCounter(8, 2);  // NOLINT

  AddObservationExpectFalse(0, "00000000");
  AddObservationExpectFalse(1, "00000000");
}

// Tests that AddObservation() returns false when an invalid observation
// is added.
TEST_F(BloomBitCounterTest, InvalidObservations) {
  // Construct a bloom bit counter with 8 bits and 2 cohorts.
  SetBitCounter(8, 2);  // NOLINT

  // Attempt to add observations with 2 bytes instead of one.
  AddObservationExpectFalse(0, "0000000000000000");
  AddObservationExpectFalse(1, "0000000000000000");
  AddObservationExpectFalse(0, "0000000100000000");
  AddObservationExpectFalse(1, "0000000100000000");

  // Successfully add observations with one bytes.
  AddObservation(0, "00000001");
  AddObservation(1, "00000001");

  // Attempt to add an observation for cohort 3.
  AddObservationExpectFalse(3, "00000001");
}

// Invokes OneBitTest on various y using n=100, p=0, q=1
TEST_F(BloomBitCounterTest, OneBitTestN100P0Q1) {
  const int n = 100;
  const double expected_std_err = 0;

  // Test with various values of y. expected_estimate = y.
  for (int y : {0, 1, 34, 49, 50, 51, 71, 99, 100}) {
    SCOPED_TRACE(std::to_string(y));
    OneBitTest(n, y, y, expected_std_err);
  }
}

// Invokes OneBitTest on various y using n=100, p=0.2, q=0.8
TEST_F(BloomBitCounterTest, OneBitTestN100P02Q08) {
  prob_0_becomes_1_ = 0.2;  // NOLINT
  prob_1_stays_1_ = 0.8;    // NOLINT
  int n = 100;              // NOLINT

  // This is the formula for computing expected_estimate when n=100, p=0.2,
  // q=0.8.
  // NOLINTNEXTLINE
  auto estimator = [](double y) { return (y - 20.0) * 5.0 / 3.0; };
  // This is the expected standard error for n=100, p=0.2, q=0.8, independent
  // of y.
  // NOLINTNEXTLINE
  double expected_std_err = 20.0 / 3.0;

  // Test with various values of y.
  for (int y : {0, 1, 34, 49, 50, 51, 71, 99, 100}) {
    SCOPED_TRACE(std::to_string(y));
    OneBitTest(n, y, estimator(y), expected_std_err);
  }
}

// Invokes OneBitTest on various y using n=1000, p=0.15, q=0.85
TEST_F(BloomBitCounterTest, OneBitTestN1000P015Q085) {
  prob_0_becomes_1_ = 0.15;  // NOLINT
  prob_1_stays_1_ = 0.85;    // NOLINT
  int n = 1000;              // NOLINT

  // This is the formula for computing expected_estimate when n=1000, p=0.15,
  // q=0.85.
  // NOLINTNEXTLINE
  auto estimator = [](double y) { return (y - 150.0) * 10.0 / 7.0; };
  // This is the expected standard error for n=1000, p=0.15, q=0.85,
  // independent
  // of y.
  // NOLINTNEXTLINE
  double expected_std_err = sqrt(127.5) * 10.0 / 7.0;

  // Test with various values of y.
  for (int y : {0, 1, 71, 333, 444, 555, 666, 777, 888, 999, 1000}) {
    SCOPED_TRACE(std::to_string(y));
    OneBitTest(n, y, estimator(y), expected_std_err);
  }
}

// Invokes OneBitTest on various y using n=5000, p=0.5, q=0.9. Notice that
// p + q > 1
TEST_F(BloomBitCounterTest, OneBitTestN5000P05Q09) {
  prob_0_becomes_1_ = 0.5;  // NOLINT
  prob_1_stays_1_ = 0.9;    // NOLINT
  const int n = 5000;

  // This is the formula for computing expected_estimate when n=5000, p=0.5,
  // q=0.9.
  // NOLINTNEXTLINE
  auto estimator = [](double y) { return (y - 2500.0) * 5.0 / 2.0; };

  // This is the formula for computing expected_std_err when n=5000, p=0.5,
  // q=0.9.
  // NOLINTNEXTLINE
  auto std_err = [](double y) { return sqrt(y * -0.4 + 2250.0) * 5.0 / 2.0; };

  // Test with various values of y.
  for (int y : {0, 1, 49, 222, 1333, 2444, 3555, 4999, 5000}) {
    SCOPED_TRACE(std::to_string(y));
    OneBitTest(n, y, estimator(y), std_err(y));
  }
}

// Invokes OneBitTest on various y using n=5000, p=0.05, q=0.5. Notice that
// p + q < 1
TEST_F(BloomBitCounterTest, OneBitTestN5000P005Q05) {
  prob_0_becomes_1_ = 0.05;  // NOLINT
  prob_1_stays_1_ = 0.5;     // NOLINT
  const int n = 5000;

  // This is the formula for computing expected_estimate when n=5000, p=0.05,
  // q=0.5.
  // NOLINTNEXTLINE
  auto estimator = [](double y) { return (y - 250.0) / 0.45; };

  // This is the formula for computing expected_std_err when n=5000, p=0.05,
  // q=0.5.
  // NOLINTNEXTLINE
  auto std_err = [](double y) { return sqrt(y * 0.45 + 125.0) / 0.45; };

  // Test with various values of y.
  for (int y : {0, 1, 49, 222, 1333, 2444, 3555, 4999, 5000}) {
    SCOPED_TRACE(std::to_string(y));
    OneBitTest(n, y, estimator(y), std_err(y));
  }
}

}  // namespace rappor
}  // namespace cobalt

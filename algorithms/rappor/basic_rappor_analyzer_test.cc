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

#include <gflags/gflags.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "./logging.h"
#include "algorithms/rappor/rappor_encoder.h"
#include "algorithms/rappor/rappor_test_utils.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/crypto_util/random_test_utils.h"

namespace cobalt {
namespace rappor {

using encoder::ClientSecret;

namespace {

// Given a string of "0"s and "1"s of length a multiple of 8, returns
// a BasicRapporObservation whose data is equal to the bytes whose binary
// representation is given by the string.
BasicRapporObservation BasicRapporObservationFromString(
    const std::string& binary_string) {
  BasicRapporObservation obs;
  obs.set_data(BinaryStringToData(binary_string));
  return obs;
}

// Makes a BasicRapporConfig with the given data.
BasicRapporConfig Config(int num_categories, double p, double q) {
  BasicRapporConfig config;
  config.set_prob_0_becomes_1(p);
  config.set_prob_1_stays_1(q);
  for (int i = 0; i < num_categories; i++) {
    config.mutable_string_categories()->add_category(CategoryName(i));
  }
  return config;
}

}  // namespace

class BasicRapporAnalyzerTest : public ::testing::Test {
 protected:
  // Sets the member variable analyzer_ to a new BasicRapporAnalyzer configured
  // to use |num_categories| categories and the current values of
  // prob_0_becomes_1_, prob_1_stays_1_.
  void SetAnalyzer(int num_categories) {
    analyzer_.reset(new BasicRapporAnalyzer(
        Config(num_categories, prob_0_becomes_1_, prob_1_stays_1_)));
    add_good_observation_call_count_ = 0;
    add_bad_observation_call_count_ = 0;
  }

  // Sets the member variable encoder_ to be a new BasicRapporEncoder configured
  // to use |num_categories| categories and the current values of
  // prob_0_becomes_1_, prob_1_stays_1_, a deterministic RNG.
  void SetEncoder(int num_categories) {
    encoder_.reset(new BasicRapporEncoder(
        Config(num_categories, prob_0_becomes_1_, prob_1_stays_1_),
        ClientSecret::GenerateNewSecret()));
    encoder_->SetRandomForTesting(
        std::unique_ptr<crypto::Random>(new crypto::DeterministicRandom()));
  }

  // Uses |encoder_| to encode |num_observations| observations for the given
  // |category| and adds each of the observations to |analyzer_|.
  void EncodeAndAdd(int category, int num_observations) {
    BasicRapporObservation obs;
    ValuePart value;
    value.set_string_value(CategoryName(category));
    for (int i = 0; i < num_observations; i++) {
      EXPECT_EQ(kOK, encoder_->Encode(value, &obs));
      EXPECT_TRUE(analyzer_->AddObservation(obs));
    }
  }

  // Adds an observation to analyzer_ described by |binary_string|. Expects
  // the operation to result in an error.
  void AddObservationExpectFalse(std::string binary_string) {
    EXPECT_FALSE(analyzer_->AddObservation(
        BasicRapporObservationFromString(binary_string)));
    CheckState(add_good_observation_call_count_,
               ++add_bad_observation_call_count_);
  }

  // Adds an observation to analyzer_ described by |binary_string|. Expects
  // the operation to succeed.
  void AddObservation(std::string binary_string) {
    EXPECT_TRUE(analyzer_->AddObservation(
        BasicRapporObservationFromString(binary_string)));
    CheckState(++add_good_observation_call_count_,
               add_bad_observation_call_count_);
  }

  // Invokes AddObservation() many times.
  void AddObservations(std::string binary_string, int num_times) {
    for (int count = 0; count < num_times; count++) {
      SCOPED_TRACE(std::string("count=") + std::to_string(count));
      AddObservation(binary_string);
    }
  }

  // Checks that analyzer_ has the expected number observations and errors.
  void CheckState(size_t expected_num_observations,
                  size_t expected_observation_errors) {
    EXPECT_EQ(expected_num_observations, analyzer_->num_observations());
    EXPECT_EQ(expected_observation_errors, analyzer_->observation_errors());
  }

  // Checks that analyzer_ has the expected raw count.
  void ExpectRawCount(size_t index, size_t expected_count) {
    EXPECT_EQ(expected_count, analyzer_->raw_category_counts()[index]);
  }

  // Checks that analyzer_ has the expected raw counts.
  void ExpectRawCounts(std::vector<size_t> expected_counts) {
    EXPECT_EQ(expected_counts, analyzer_->raw_category_counts());
  }

  // Invokes analyzer_->Analyze() and checks the count and std_error in
  // the given position.
  void AnalyzeAndCheckOnePosition(int position, double expected_estimate,
                                  double expected_std_err) {
    auto results = analyzer_->Analyze();
    EXPECT_FLOAT_EQ(expected_estimate, results[position].count_estimate)
        << "position=" << position;
    EXPECT_FLOAT_EQ(expected_std_err, results[position].std_error)
        << "position=" << position;
  }

  // Tests basic RAPPOR focusing on only a single bit at a time. No encoder
  // is used. Instead we directly construct observations to give to the
  // Analyzer.
  //
  // We use Basic RAPPOR with 24 bits but each time the test runs we single out
  // one |bit_index| to use. Only that |bit_index| receives any non-zero
  // observation data and only that |bit_index| is validated.
  //
  // Uses the currently set values for prob_0_becomes_1_ and prob_1_stays_1_.
  // There will be |n| total observations with |y| 1's and |n-y| 0's.
  void OneBitTest(int n, int y, double expected_estimate,
                  double expected_std_err) {
    // We pick five different bits out of the 24 bits to test.
    for (int bit_index : {0, 1, 8, 15, 23}) {
      SCOPED_TRACE(std::to_string(bit_index) + ", " + std::to_string(n) + ", " +
                   std::to_string(y));
      // Construct an analyzer for 24 bit Basic RAPPOR.
      SetAnalyzer(24);
      // Add y observations with a 1 in position |bit_index|.
      AddObservations(BuildBitPatternString(24, bit_index, '1', '0'), y);
      // Add n-y observations with a 0 in position |bit_index|.
      AddObservations(BuildBitPatternString(24, bit_index, '0', '0'), n - y);
      // Analyze and check position |bit_index|
      AnalyzeAndCheckOnePosition(bit_index, expected_estimate,
                                 expected_std_err);
    }
  }

  // We run basic RAPPOR with two categories using encoder_ and analyzer_.
  // We focus only on the results for category 0.
  //
  // We encode and add |y| observations for category 0 and |n-y| observations
  // for category 1. The parameters named "accumulated_*" are used to accumulate
  // sums of the results of invoking this method multiple times. This method
  // should be invoked multiple times with the same parameters.
  //
  // |accumulated_count_estimate| accumulates the sum of the count_estimates for
  // category 0.
  //
  // |accumulated_std_err_estimate| accumulates the sum of the std_errors for
  // category 0.
  //
  // |accumulated_actual_square_error| accumulates the sum of the squares of
  // the differences between the count_estimates, clipped to the feasible
  // region of [0, n], and the actual count,
  // y.
  void OneCategoryExperiment(int n, int y, double* accumulated_count_estimate,
                             double* accumulated_std_err_estimate,
                             double* accumulated_actual_square_error) {
    // Construct a fresh analyzer with 2 categories.
    SetAnalyzer(2);

    // Add y encoded observations with a true 1 for category 0.
    EncodeAndAdd(0, y);
    // Add n - y  encoded observations with a true 0 for category 0.
    EncodeAndAdd(1, n - y);

    // Analyze
    auto results = analyzer_->Analyze();

    (*accumulated_count_estimate) += results[0].count_estimate;
    (*accumulated_std_err_estimate) += results[0].std_error;
    double clipped_count_estimate =
        std::max(0.0, std::min(n * 1.0, results[0].count_estimate));
    double actual_error = (clipped_count_estimate - y);
    (*accumulated_actual_square_error) += actual_error * actual_error;
  }

  // Performs the OneCategoryExperiment 100 times and then checks that
  // (1) The average count_estimate is close to the true count, and
  // (2) The observed standard deviation is close to the std_error estimate.
  // p = the probability that a 0 is flipped to a 1
  // q = 1 minus the probability that a 1 is flipped to a 0.
  // n = number of observations
  // y = number of true 1's
  void OneCategoryTest(double p, double q, int n, int y) {
    // Set p and q.
    prob_0_becomes_1_ = p;
    prob_1_stays_1_ = q;
    SetEncoder(2);

    double accumulated_count_estimate = 0.0;
    double accumulated_std_err_estimate = 0.0;
    double accumulated_actual_square_error = 0.0;

    // Repeat the experiment 100 times.
    static const int kNumTrials = 100;

    for (int trial = 0; trial < kNumTrials; trial++) {
      OneCategoryExperiment(n, y, &accumulated_count_estimate,
                            &accumulated_std_err_estimate,
                            &accumulated_actual_square_error);
    }

    double average_count_estimate = accumulated_count_estimate / kNumTrials;
    double std_error_estimate = accumulated_std_err_estimate / kNumTrials;
    double observed_variance = accumulated_actual_square_error / kNumTrials;
    double observed_std_dev = sqrt(observed_variance);

    // Check that the average count estimate is within 1.66 observed
    // standard deviations of the true count, y. We may think of this
    // as performing a formal hypothesis test of the null hypothesis that
    // the count_estimates have been drawn from a distribution whose mean
    // is equal to y. For this we use a Student-t test with 100 degrees of
    // freedom (because kNumTrials = 100). By using the number 1.66 we are
    // performing the test at the 0.1 significance level because
    // P(t > 1.66) ~= 0.05 where t ~ T(100).
    double t_stat = fabs(average_count_estimate - y) / observed_std_dev;
    EXPECT_TRUE(t_stat < 1.66) << t_stat;

    // We check that the ratio of the observed_std_dev to the std_error_estimate
    // is close to one. Precisely, we check that the ratio is in the
    // interval (0.88, 1.11)  We may think of this as performing a formal
    // hypothesis test of the null hypothesis that the count_estimates have
    // been drawn from a distribution whose variance is equal to the square of
    // the std_error_estimate. For this we use a Chi-squared distribution with
    // 100 degrees of freedom (because kNumTrials = 100). The reason for numbers
    // 0.88 and 1.11 is that we are performing the test at the 0.1 significance
    // level and sqrt(77.93)/10 ~= 0.88 and sqrt(124.3)/10 ~= 1.11 and
    // P(X < 77.93) ~= 0.05 and P(X > 124.3) ~= 0.05 where X ~ Chi^2(100).
    // We changed the lower bound to 0.78 because one of the test cases was
    // failing with an x_stat ~= 0.789.
    double x_stat = observed_std_dev / std_error_estimate;
    EXPECT_TRUE(x_stat > 0.78) << x_stat;
    EXPECT_TRUE(x_stat < 1.11) << x_stat;
  }

  // By default this test uses p=0, q=1. Individual tests may override this.
  double prob_0_becomes_1_ = 0.0;
  double prob_1_stays_1_ = 1.0;
  std::unique_ptr<BasicRapporEncoder> encoder_;
  std::unique_ptr<BasicRapporAnalyzer> analyzer_;
  int add_bad_observation_call_count_ = 0;
  int add_good_observation_call_count_ = 0;
};

// Tests the raw counts when there are three categories.
TEST_F(BasicRapporAnalyzerTest, RawCountsThreeCategories) {
  // Construct an analyzer for BasicRappor with three categories.
  SetAnalyzer(3);

  AddObservation("00000000");
  ExpectRawCounts({0, 0, 0});

  AddObservation("00000000");
  ExpectRawCounts({0, 0, 0});

  AddObservation("00000001");
  ExpectRawCounts({1, 0, 0});

  AddObservation("00000001");
  ExpectRawCounts({2, 0, 0});

  AddObservation("00000010");
  ExpectRawCounts({2, 1, 0});

  AddObservation("00000010");
  ExpectRawCounts({2, 2, 0});

  AddObservation("00000011");
  ExpectRawCounts({3, 3, 0});

  AddObservation("00000100");
  ExpectRawCounts({3, 3, 1});

  AddObservation("00000101");
  ExpectRawCounts({4, 3, 2});

  AddObservation("00000011");
  ExpectRawCounts({5, 4, 2});

  AddObservation("00000111");
  ExpectRawCounts({6, 5, 3});

  AddObservation("00000111");
  ExpectRawCounts({7, 6, 4});

  for (int i = 0; i < 1000; i++) {
    AddObservation("00000101");
  }
  ExpectRawCounts({1007, 6, 1004});

  // The extra high-order-bits should be ignored
  AddObservation("11111000");
  ExpectRawCounts({1007, 6, 1004});
}

// Tests the raw counts when there are ten categories.
TEST_F(BasicRapporAnalyzerTest, RawCountsTenCategories) {
  // Construct an analyzer for BasicRappor with 10 categories.
  SetAnalyzer(10);

  AddObservation("0000000000000000");
  ExpectRawCounts({0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

  AddObservation("0000000000000000");
  ExpectRawCounts({0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

  AddObservation("0000000000000001");
  ExpectRawCounts({1, 0, 0, 0, 0, 0, 0, 0, 0, 0});

  AddObservation("0000000000000001");
  ExpectRawCounts({2, 0, 0, 0, 0, 0, 0, 0, 0, 0});

  AddObservation("0000000000000010");
  ExpectRawCounts({2, 1, 0, 0, 0, 0, 0, 0, 0, 0});

  AddObservation("0000000000000010");
  ExpectRawCounts({2, 2, 0, 0, 0, 0, 0, 0, 0, 0});

  AddObservation("0000000000000011");
  ExpectRawCounts({3, 3, 0, 0, 0, 0, 0, 0, 0, 0});

  AddObservation("0000000000000100");
  ExpectRawCounts({3, 3, 1, 0, 0, 0, 0, 0, 0, 0});

  AddObservation("0000000000000101");
  ExpectRawCounts({4, 3, 2, 0, 0, 0, 0, 0, 0, 0});

  AddObservation("0000000000000011");
  ExpectRawCounts({5, 4, 2, 0, 0, 0, 0, 0, 0, 0});

  AddObservation("0000000000000111");
  ExpectRawCounts({6, 5, 3, 0, 0, 0, 0, 0, 0, 0});

  AddObservation("0000000000000111");
  ExpectRawCounts({7, 6, 4, 0, 0, 0, 0, 0, 0, 0});

  AddObservation("0000001000000000");
  ExpectRawCounts({7, 6, 4, 0, 0, 0, 0, 0, 0, 1});

  AddObservation("0000000100000000");
  ExpectRawCounts({7, 6, 4, 0, 0, 0, 0, 0, 1, 1});

  AddObservation("0000000010000000");
  ExpectRawCounts({7, 6, 4, 0, 0, 0, 0, 1, 1, 1});

  AddObservation("0000001010000000");
  ExpectRawCounts({7, 6, 4, 0, 0, 0, 0, 2, 1, 2});

  AddObservation("0000001110000000");
  ExpectRawCounts({7, 6, 4, 0, 0, 0, 0, 3, 2, 3});

  for (int i = 0; i < 1000; i++) {
    AddObservation("0000000100000101");
  }
  ExpectRawCounts({1007, 6, 1004, 0, 0, 0, 0, 3, 1002, 3});

  // The extra high-order-bits should be ignored
  AddObservation("1111110000000000");
  ExpectRawCounts({1007, 6, 1004, 0, 0, 0, 0, 3, 1002, 3});
}

// Tests the raw counts when there are 1,000 categories.
TEST_F(BasicRapporAnalyzerTest, RawCountsThousandCategories) {
  // Construct an analyzer for BasicRappor with 1000 categories.
  SetAnalyzer(1000);
  // Iterate 100 times
  for (int iteration = 0; iteration < 100; iteration++) {
    // For i = 0, 10, 20, 30, .....
    for (int bit_index = 0; bit_index < 1000; bit_index += 10) {
      // Add an observation with category i alone set.
      AddObservation(BuildBitPatternString(1000, bit_index, '1', '0'));
    }
  }

  // Check the counts.
  for (int category = 0; category < 1000; category++) {
    size_t expected_count = (category % 10 == 0 ? 100 : 0);
    ExpectRawCount(category, expected_count);
  }
}

// Tests that AddObservation() returns false when an invalid config is
// provided to the constructor.
TEST_F(BasicRapporAnalyzerTest, InvalidConfig) {
  // Set prob_0_becomes_1 to an invalid value.
  prob_0_becomes_1_ = 1.1;

  // Construct an analyzer for BasicRappor with 8 categories using the
  // invalid config.
  SetAnalyzer(8);

  AddObservationExpectFalse("00000000");
  AddObservationExpectFalse("00000000");
  AddObservationExpectFalse("00000001");
  AddObservationExpectFalse("00000001");
}

// Tests that AddObservation() returns false when an invalid observation
// is added.
TEST_F(BasicRapporAnalyzerTest, InvalidObservations) {
  // Construct an analyzer for BasicRappor with 8 categories using the
  // invalid config.
  SetAnalyzer(8);

  // Attempt to add observations with 2 bytes instead of one.
  AddObservationExpectFalse("0000000000000000");
  AddObservationExpectFalse("0000000000000000");
  AddObservationExpectFalse("0000000100000000");
  AddObservationExpectFalse("0000000100000000");

  // Successfully add observations with one bytes.
  AddObservation("00000001");
  AddObservation("00000001");
  AddObservation("00000001");
  AddObservation("00000001");
}

// Invokes OneBitTest on various y using n=100, p=0, q=1
TEST_F(BasicRapporAnalyzerTest, OneBitTestN100P0Q1) {
  int n = 100;
  double expected_std_err = 0;

  // Test with various values of y. expected_estimate = y.
  for (int y : {0, 1, 34, 49, 50, 51, 71, 99, 100}) {
    SCOPED_TRACE(std::to_string(y));
    OneBitTest(n, y, y, expected_std_err);
  }
}

// Invokes OneBitTest on various y using n=100, p=0.2, q=0.8
TEST_F(BasicRapporAnalyzerTest, OneBitTestN100P02Q08) {
  prob_0_becomes_1_ = 0.2;
  prob_1_stays_1_ = 0.8;
  int n = 100;

  // This is the formula for computing expected_estimate when n=100, p=0.2,
  // q=0.8.
  auto estimator = [](double y) { return (y - 20.0) * 5.0 / 3.0; };
  // This is the expected standard error for n=100, p=0.2, q=0.8, independent
  // of y.
  double expected_std_err = 20.0 / 3.0;

  // Test with various values of y.
  for (int y : {0, 1, 34, 49, 50, 51, 71, 99, 100}) {
    SCOPED_TRACE(std::to_string(y));
    OneBitTest(n, y, estimator(y), expected_std_err);
  }
}

// Invokes OneBitTest on various y using n=1000, p=0.15, q=0.85
TEST_F(BasicRapporAnalyzerTest, OneBitTestN1000P015Q085) {
  prob_0_becomes_1_ = 0.15;
  prob_1_stays_1_ = 0.85;
  int n = 1000;

  // This is the formula for computing expected_estimate when n=1000, p=0.15,
  // q=0.85.
  auto estimator = [](double y) { return (y - 150.0) * 10.0 / 7.0; };
  // This is the expected standard error for n=1000, p=0.15, q=0.85,
  // independent
  // of y.
  double expected_std_err = sqrt(127.5) * 10.0 / 7.0;

  // Test with various values of y.
  for (int y : {0, 1, 71, 333, 444, 555, 666, 777, 888, 999, 1000}) {
    SCOPED_TRACE(std::to_string(y));
    OneBitTest(n, y, estimator(y), expected_std_err);
  }
}

// Invokes OneBitTest on various y using n=5000, p=0.5, q=0.9. Notice that
// p + q > 1
TEST_F(BasicRapporAnalyzerTest, OneBitTestN5000P05Q09) {
  prob_0_becomes_1_ = 0.5;
  prob_1_stays_1_ = 0.9;
  int n = 5000;

  // This is the formula for computing expected_estimate when n=5000, p=0.5,
  // q=0.9.
  auto estimator = [](double y) { return (y - 2500.0) * 5.0 / 2.0; };

  // This is the formula for computing expected_std_err when n=5000, p=0.5,
  // q=0.9.
  auto std_err = [](double y) { return sqrt(y * -0.4 + 2250.0) * 5.0 / 2.0; };

  // Test with various values of y.
  for (int y : {0, 1, 49, 222, 1333, 2444, 3555, 4999, 5000}) {
    SCOPED_TRACE(std::to_string(y));
    OneBitTest(n, y, estimator(y), std_err(y));
  }
}

// Invokes OneBitTest on various y using n=5000, p=0.05, q=0.5. Notice that
// p + q < 1
TEST_F(BasicRapporAnalyzerTest, OneBitTestN5000P005Q05) {
  prob_0_becomes_1_ = 0.05;
  prob_1_stays_1_ = 0.5;
  int n = 5000;

  // This is the formula for computing expected_estimate when n=5000, p=0.05,
  // q=0.5.
  auto estimator = [](double y) { return (y - 250.0) / 0.45; };

  // This is the formula for computing expected_std_err when n=5000, p=0.05,
  // q=0.5.
  auto std_err = [](double y) { return sqrt(y * 0.45 + 125.0) / 0.45; };

  // Test with various values of y.
  for (int y : {0, 1, 49, 222, 1333, 2444, 3555, 4999, 5000}) {
    SCOPED_TRACE(std::to_string(y));
    OneBitTest(n, y, estimator(y), std_err(y));
  }
}

TEST_F(BasicRapporAnalyzerTest, OneCategoryTest) {
  {
    SCOPED_TRACE("");
    OneCategoryTest(0.1, 0.9, 1000, 800);
  }
  {
    SCOPED_TRACE("");
    OneCategoryTest(0.1, 0.9, 1000, 500);
  }
  {
    SCOPED_TRACE("");
    OneCategoryTest(0.1, 0.9, 1000, 100);
  }
  {
    SCOPED_TRACE("");
    OneCategoryTest(0.2, 0.8, 1000, 900);
  }
  {
    SCOPED_TRACE("");
    OneCategoryTest(0.25, 0.75, 1000, 600);
  }
  {
    SCOPED_TRACE("");
    OneCategoryTest(0.3, 0.7, 1000, 200);
  }
  {
    SCOPED_TRACE("");
    OneCategoryTest(0.3, 0.85, 1000, 700);
  }
  {
    SCOPED_TRACE("");
    OneCategoryTest(0.1, 0.85, 1000, 400);
  }
  {
    SCOPED_TRACE("");
    OneCategoryTest(0.05, 0.7, 1000, 300);
  }
}

}  // namespace rappor
}  // namespace cobalt

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  INIT_LOGGING(argv[0]);
  return RUN_ALL_TESTS();
}

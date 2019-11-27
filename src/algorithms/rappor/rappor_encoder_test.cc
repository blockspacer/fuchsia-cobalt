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
#include "src/algorithms/rappor/rappor_encoder.h"

#include <algorithm>
#include <map>
#include <vector>

#include "src/algorithms/rappor/rappor_test_utils.h"
#include "src/lib/crypto_util/random_test_utils.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::rappor {

using system_data::ClientSecret;

// Constructs a BasicRapporEncoder with the given |config|, invokes
// Encode() with a dummy string and EncodeNullObservation(), and checks that the
// returned statuses are either kOK or kInvalidConfig, whichever is expected.
void TestBasicRapporConfig(const BasicRapporConfig& config, Status expected_status,
                           int caller_line_number) {
  // Make a ClientSecret once and statically store the token.
  static const std::string kClientSecretToken = ClientSecret::GenerateNewSecret().GetToken();
  // Each time this function is invoked reconstitute the secret from the token.
  BasicRapporEncoder encoder(config, ClientSecret::FromToken(kClientSecretToken));
  BasicRapporObservation obs;
  ValuePart value;
  value.set_string_value("cat");
  EXPECT_EQ(expected_status, encoder.Encode(value, &obs))
      << "Invoked from line number: " << caller_line_number;
  BasicRapporObservation null_obs;
  EXPECT_EQ(expected_status, encoder.EncodeNullObservation(&null_obs))
      << "Invoked from line number: " << caller_line_number;
}

// A macro to invoke TestBasicRapporConfig and pass it the current line number.
#define TEST_BASIC_RAPPOR_CONFIG(config, expected_status) \
  (TestBasicRapporConfig(config, expected_status, __LINE__))

// Tests the validation of config for Basic RAPPOR.
TEST(RapporEncoderTest, BasicRapporConfigValidation) {
  // Empty config: Invalid
  BasicRapporConfig config;
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Add two probabilities but no categories: Invalid
  config.prob_0_becomes_1 = 0.3;
  config.prob_1_stays_1 = 0.7;
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Add one category: Invalid.
  config.categories.add_string("cat");
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Add two more categories: Valid.
  config.categories.add_string("dog");
  config.categories.add_string("fish");
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Explicitly set PRR to 0: Valid.
  config.prob_rr = 0.0;
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Explicitly set PRR to non-zero: Invalid.
  config.prob_rr = 0.1;
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Explicitly set PRR back to zero: Valid.
  config.prob_rr = 0.0;
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Set one of the probabilities to negative: Invalid
  config.prob_0_becomes_1 = -0.3;
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set one of the probabilities to greater than 1: Invalid
  config.prob_0_becomes_1 = 1.3;
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Fix the probability: Valid
  config.prob_0_becomes_1 = 0.3;
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Set the other probability to negative: Invalid
  config.prob_1_stays_1 = -0.7;
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set the other the probability to greater than 1: Invalid
  config.prob_1_stays_1 = 1.7;
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Fix the probability: Valid
  config.prob_1_stays_1 = 0.7;
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Add an empty category: Invalid
  config.categories.add_string("");
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Test with an invalid ClientSecret
  BasicRapporEncoder encoder(config, ClientSecret::FromToken("Invalid Token"));
  BasicRapporObservation obs;
  ValuePart value;
  value.set_string_value("dummy");
  EXPECT_EQ(kInvalidConfig, encoder.Encode(value, &obs));
  BasicRapporObservation null_obs;
  EXPECT_EQ(kInvalidConfig, encoder.EncodeNullObservation(&null_obs));
}

// Test Config Validation with integer categories
TEST(RapporEncoderTest, BasicRapporWithIntsConfigValidation) {
  // Create a config with three integer categories.
  BasicRapporConfig config;
  config.prob_0_becomes_1 = 0.3;
  config.prob_1_stays_1 = 0.7;
  config.categories.set_int_range(-1, 1);

  // Construct the encoder
  BasicRapporEncoder encoder(config, ClientSecret::GenerateNewSecret());

  // Perform an encode with a value equal to one of the listed categories
  BasicRapporObservation obs;
  ValuePart value;
  value.set_int_value(-1);
  EXPECT_EQ(kOK, encoder.Encode(value, &obs));

  // Perform an encode with a value not equal to one of the listed categories
  value.set_int_value(2);
  EXPECT_EQ(kInvalidInput, encoder.Encode(value, &obs));

  // Perform an encode of a null observation
  BasicRapporObservation null_obs;
  EXPECT_EQ(kOK, encoder.EncodeNullObservation(&null_obs));
}

// Performs a test of BasicRapporEncoder::Encode() and
// BasicRapporEncoder::EncodeNullObservation() in the two special cases that
// that there is no randomness involved in the encoded string, namely
// (a) p = 0, q = 1
// (b) p = 1, q = 0
//
// |num_categories| must be a positive integer. Basic RAPPOR will be configured
// to have this many categories. The encoding will be performed for each of
// the categores.
//
// |q_is_one| Do the test in case (a) where p = 0, q = 1
void DoBasicRapporNoRandomnessTest(int num_categories, bool q_is_one) {
  // Select the parameters based on the mode. index_char and other_char
  // determine the expected bit pattern in the encoding. index_char is the
  // character we expect to see in the position of the given category and
  // other_char is the character we expect to see in the other positions.
  float p, q;
  char index_char, other_char;
  if (q_is_one) {
    // We expect a 1 in the index position and 0's everywhere else.
    p = 0.0;
    q = 1.0;
    index_char = '1';
    other_char = '0';
  } else {
    // We expect a 0 in the index position and 1's everywhere else.
    p = 1.0;
    q = 0.0;
    index_char = '0';
    other_char = '1';
  }

  // Configure basic RAPPOR with the selected parameters.
  BasicRapporConfig config;
  config.prob_0_becomes_1 = p;
  config.prob_1_stays_1 = q;
  for (int i = 0; i < num_categories; i++) {
    config.categories.add_string(CategoryName(i));
  }

  // Construct a BasicRapporEncoder.
  static const std::string kClientSecretToken = ClientSecret::GenerateNewSecret().GetToken();
  BasicRapporEncoder encoder(config, ClientSecret::FromToken(kClientSecretToken));

  // The expected number of bits in the encoding is the least multiple of 8
  // greater than or equal to num_categories.
  uint16_t expected_num_bits = 8 * (((num_categories - 1) / 8) + 1);

  // For each category, obtain the observation and check that the bit pattern
  // is as expected.
  BasicRapporObservation obs;
  for (int i = 0; i < num_categories; i++) {
    auto category_name = CategoryName(i);
    ValuePart value;
    value.set_string_value(category_name);
    ASSERT_EQ(kOK, encoder.Encode(value, &obs)) << category_name;
    auto expected_pattern = BuildBitPatternString(expected_num_bits, i, index_char, other_char);
    EXPECT_EQ(DataToBinaryString(obs.data()), expected_pattern);
  }

  // Encode a null observation and check that the bit pattern is as expected.
  BasicRapporObservation null_obs;
  ASSERT_EQ(kOK, encoder.EncodeNullObservation(&null_obs)) << "Null observation";
  std::vector<uint16_t> index_of_1s(0);
  if (!q_is_one) {
    for (uint16_t k = 0; k < expected_num_bits; k++) {
      index_of_1s.push_back(k);
    }
  }
  auto null_expected_pattern = BuildBinaryString(expected_num_bits, index_of_1s);
  EXPECT_EQ(DataToBinaryString(null_obs.data()), null_expected_pattern);
}

// Performs a test of BasicRapporEncoder::Encode() in the special case that
// the values of p and q are either 0 or 1 so that there is no randomness
// involved in the encoded string.
TEST(BasicRapporEncoderTest, NoRandomness) {
  // We test with between 2 and 50 categories.
  for (int num_categories = 2; num_categories <= 50; num_categories++) {
    // See comments at DoBasicRapporNoRandomnessTest.
    DoBasicRapporNoRandomnessTest(num_categories, true);
    DoBasicRapporNoRandomnessTest(num_categories, false);
  }
}

// Base class for tests of Basic RAPPOR that use a deterministic RNG.
class BasicRapporDeterministicTest : public ::testing::Test {
 protected:
  std::unique_ptr<BasicRapporEncoder> BuildEncoder(float prob_0_becomes_1, float prob_1_stays_1,
                                                   int num_categories) {
    // Configure BasicRappor.
    BasicRapporConfig config;
    config.prob_0_becomes_1 = prob_0_becomes_1;
    config.prob_1_stays_1 = prob_1_stays_1;
    for (int i = 0; i < num_categories; i++) {
      config.categories.add_string(CategoryName(i));
    }

    // Construct a BasicRapporEncoder.
    static const std::string kClientSecretToken = ClientSecret::GenerateNewSecret().GetToken();
    std::unique_ptr<BasicRapporEncoder> encoder(
        new BasicRapporEncoder(config, ClientSecret::FromToken(kClientSecretToken)));

    // Give the encoder a deterministic RNG.
    encoder->SetRandomForTesting(
        std::unique_ptr<crypto::Random>(new crypto::DeterministicRandom()));

    return encoder;
  }

  // Generates a Basic RAPPOR observation 1000 times and then performs Pearson's
  // chi-squared test on each bit separately to check for goodness of fit to
  // a binomial distribution with the appropriate parameter. Fails if
  // chi-squared >= |chi_squared_threshold|.
  //
  // Uses DeterministicRandom in order to ensure reproducibility.
  //
  // REQUIRES: 0 <= selected_category < num_categories.
  // All 1000 of the observations will be for the selected category. Thus
  // the expected number of 1's in the bit position corresponding to the
  // selected category is prob_1_stays_1 and the expected number of 1'1 in
  // all other bit positions is prob_0_becomes_1.
  void DoChiSquaredTest(float prob_0_becomes_1, float prob_1_stays_1, int num_categories,
                        int selected_category, double chi_squared_threshold) {
    // Build the encoder
    auto encoder = BuildEncoder(prob_0_becomes_1, prob_1_stays_1, num_categories);

    // Sample 1000 observations of the selected category and collect the bit
    // counts
    static const int kNumTrials = 1000;
    auto category_name = CategoryName(selected_category);
    BasicRapporObservation obs;
    std::vector<int> counts(num_categories, 0);
    for (size_t i = 0; i < kNumTrials; i++) {
      obs.Clear();
      ValuePart value;
      value.set_string_value(category_name);
      EXPECT_EQ(kOK, encoder->Encode(value, &obs));
      for (int bit_index = 0; bit_index < num_categories; bit_index++) {
        if (IsSet(obs.data(), bit_index)) {
          counts[bit_index]++;
        }
      }
    }

    // In the special case where prob_1_stays_1 is 1 make sure that we got
    // 1000 1's in the selected category.
    if (prob_1_stays_1 == 1.0) {
      EXPECT_EQ(kNumTrials, counts[selected_category]);
    }

    // This is the expected number of ones and zeroes for the bit position in
    // the selected category.
    const double expected_1_selected = static_cast<double>(kNumTrials) * prob_1_stays_1;
    const double expected_0_selected = static_cast<double>(kNumTrials) - expected_1_selected;

    // This is the expected number of ones and zeroes for all bit positions
    // other than the selected category.
    const double expected_1 = static_cast<double>(kNumTrials) * prob_0_becomes_1;
    const double expected_0 = static_cast<double>(kNumTrials) - expected_1;

    // For each of the bit positions, perform the chi-squared test.
    for (int bit_index = 0; bit_index < num_categories; bit_index++) {
      double exp_0 = (bit_index == selected_category ? expected_0_selected : expected_0);
      double exp_1 = (bit_index == selected_category ? expected_1_selected : expected_1);

      if (exp_0 != 0.0 && exp_1 != 0.0) {
        // Difference between actual 1 count and expected 1 count.
        double delta_1 = static_cast<double>(counts[bit_index]) - exp_1;

        // Difference between actual 0 count and expected 0 count.
        double delta_0 = static_cast<double>(kNumTrials - counts[bit_index]) - exp_0;

        // Compute and check the Chi-Squared value.
        double chi_squared = delta_1 * delta_1 / exp_1 + delta_0 * delta_0 / exp_0;

        EXPECT_TRUE(chi_squared < chi_squared_threshold)
            << "chi_squared=" << chi_squared << " chi_squared_threshold=" << chi_squared_threshold
            << " bit_index=" << bit_index << " delta_0=" << delta_0 << " delta_1=" << delta_1
            << " num_categories=" << num_categories << " selected_category=" << selected_category
            << " prob_0_becomes_1=" << prob_0_becomes_1 << " prob_1_stays_1=" << prob_1_stays_1;
      }
    }
  }

  // Generates a Basic RAPPOR encoding of a null observation 1000 times and then
  // performs Pearson's chi-squared test on each bit separately to check for
  // goodness of fit to a binomial distribution with the appropriate parameter.
  // Fails if chi-squared >= |chi_squared_threshold|.
  //
  // Uses DeterministicRandom in order to ensure reproducibility.
  //
  // The true value of each bit (before encoding) is 0, so the expected number
  // of 1's in each bit position is prob_0_becomes_1.
  void DoChiSquaredTestForNullObs(float prob_0_becomes_1, float prob_1_stays_1, int num_categories,
                                  double chi_squared_threshold) {
    // Build the encoder.
    // Since no bits of the true bit vector are 1, the probability that 1 stays
    // 1 is irrelevant.
    auto encoder = BuildEncoder(prob_0_becomes_1, prob_1_stays_1, num_categories);

    // Sample 1000 observations of the selected category and collect the bit
    // counts
    static const int kNumTrials = 1000;
    BasicRapporObservation null_obs;
    std::vector<int> counts(num_categories, 0);
    for (size_t i = 0; i < kNumTrials; i++) {
      null_obs.Clear();
      EXPECT_EQ(kOK, encoder->EncodeNullObservation(&null_obs));
      for (int bit_index = 0; bit_index < num_categories; bit_index++) {
        if (IsSet(null_obs.data(), bit_index)) {
          counts[bit_index]++;
        }
      }
    }

    // In the special case where prob_0_becomes_1 is 0 make sure that we got
    // 1000 0's.
    for (int k = 0; k < num_categories; k++) {
      if (prob_0_becomes_1 == 0.0) {
        EXPECT_EQ(0, counts[k]);
      }
    }

    // This is the expected number of ones and zeroes for each bit position.
    const double expected_1 = static_cast<double>(kNumTrials) * prob_0_becomes_1;
    const double expected_0 = static_cast<double>(kNumTrials) - expected_1;

    // For each of the bit positions, perform the chi-squared test.
    for (int bit_index = 0; bit_index < num_categories; bit_index++) {
      if (expected_0 != 0.0 && expected_1 != 0.0) {
        // Difference between actual 1 count and expected 1 count.
        double delta_1 = static_cast<double>(counts[bit_index]) - expected_1;

        // Difference between actual 0 count and expected 0 count.
        double delta_0 = static_cast<double>(kNumTrials - counts[bit_index]) - expected_0;

        // Compute and check the Chi-Squared value.
        double chi_squared = delta_1 * delta_1 / expected_1 + delta_0 * delta_0 / expected_0;

        EXPECT_TRUE(chi_squared < chi_squared_threshold)
            << "chi_squared=" << chi_squared << " chi_squared_threshold=" << chi_squared_threshold
            << " bit_index=" << bit_index << " delta_0=" << delta_0 << " delta_1=" << delta_1
            << " num_categories=" << num_categories << " prob_0_becomes_1=" << prob_0_becomes_1;
      }
    }
  }
};

TEST_F(BasicRapporDeterministicTest, ChiSquaredTest) {
  // Perform the chi-squared test for various numbers of categories and
  // various selected categories, as well as for null observations without
  // selected categories. This gets combinatorially explosive so to keep the
  // testing time reasonable we don't test every combination but rather step
  // through the num_categories by 7 and use at most 3 selected categories for
  // each num_categories.
  //
  // The last parameter to DoChiSquaredTest*() is the chi-squared value to use.
  // Notice that these values were chosen by experimentation to be as small as
  // possible so that the test passes. They do not necessarily correspond to
  // natural confidence intervals for the chi-squared test.
  for (int num_categories = 2; num_categories < 40; num_categories += 7) {
    for (int selected_category = 0; selected_category < num_categories;
         selected_category += (num_categories / 3 + 1)) {
      // The first parameter is prob_0_becomes_1, and the second parameter is
      // prob_1_stays_1.
      DoChiSquaredTest(0.01, 0.99, num_categories, selected_category, 8.2);
      DoChiSquaredTest(0.1, 0.9, num_categories, selected_category, 9.4);
      DoChiSquaredTest(0.2, 0.8, num_categories, selected_category, 11.1);
      DoChiSquaredTest(0.25, 0.75, num_categories, selected_category, 11.8);
      DoChiSquaredTest(0.3, 0.7, num_categories, selected_category, 11.8);
    }
    // The first parameter is prob_0_becomes_1, and the second parameter is
    // prob_1_stays_1.
    DoChiSquaredTestForNullObs(0.0, 1.0, num_categories, 0.0);
    DoChiSquaredTestForNullObs(0.01, 0.99, num_categories, 8.2);
    DoChiSquaredTestForNullObs(0.1, 0.9, num_categories, 9.4);
    DoChiSquaredTestForNullObs(0.2, 0.8, num_categories, 11.1);
    DoChiSquaredTestForNullObs(0.25, 0.75, num_categories, 11.8);
    DoChiSquaredTestForNullObs(0.3, 0.7, num_categories, 11.8);
  }
}

// Test that BasicRapporEncoder::Encode() returns kInvalidArgument if a category
// name is used that is not one of the registered categories.
TEST(BasicRapporEncoderTest, BadCategory) {
  // Configure Basic RAPPOR with two categories, "dog" and "cat".
  BasicRapporConfig config;
  config.prob_0_becomes_1 = 0.3;
  config.prob_1_stays_1 = 0.7;
  config.categories.set_strings({"dog", "cat"});

  // Construct a BasicRapporEncoder.
  static const std::string kClientSecretToken = ClientSecret::GenerateNewSecret().GetToken();
  BasicRapporEncoder encoder(config, ClientSecret::FromToken(kClientSecretToken));

  // Attempt to encode a string that is not one of the categories. Expect
  // to receive kInvalidInput.
  BasicRapporObservation obs;
  ValuePart value;
  value.set_string_value("fish");
  EXPECT_EQ(kInvalidInput, encoder.Encode(value, &obs));
}

// Tests that BasicRapporEncoder::Encode() correctly handles values of type
// INDEX
TEST(BasicRapporEncoderTest, EncodeIndex) {
  // Configure Basic RAPPOR with 5 indexed categories.
  // We use no randomness so we can check that the correct bit is being set.
  BasicRapporConfig config;
  config.prob_0_becomes_1 = 0.0;
  config.prob_1_stays_1 = 1.0;
  config.categories.set_indexed(5);

  // Construct a BasicRapporEncoder.
  static const std::string kClientSecretToken = ClientSecret::GenerateNewSecret().GetToken();
  BasicRapporEncoder encoder(config, ClientSecret::FromToken(kClientSecretToken));

  BasicRapporObservation obs;
  // Validate that category indices 0, 1 and 4 yield the correct data.
  ValuePart value;
  value.set_index_value(0);
  EXPECT_EQ(kOK, encoder.Encode(value, &obs));
  EXPECT_EQ("00000001", DataToBinaryString(obs.data()));
  obs.Clear();
  value.set_index_value(1);
  EXPECT_EQ(kOK, encoder.Encode(value, &obs));
  EXPECT_EQ("00000010", DataToBinaryString(obs.data()));
  obs.Clear();
  value.set_index_value(4);
  EXPECT_EQ(kOK, encoder.Encode(value, &obs));
  EXPECT_EQ("00010000", DataToBinaryString(obs.data()));
  obs.Clear();

  // Validate that category index 5 yields kInvalidInput.
  value.set_index_value(5);
  EXPECT_EQ(kInvalidInput, encoder.Encode(value, &obs));
}

}  // namespace cobalt::rappor

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
#include "algorithms/rappor/rappor_encoder.h"

#include <algorithm>
#include <map>
#include <vector>

#include "algorithms/rappor/rappor_test_utils.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/crypto_util/random_test_utils.h"

namespace cobalt {
namespace rappor {

using encoder::ClientSecret;

TEST(RapporConfigValidatorTest, TestMinPower2Above) {
  EXPECT_EQ(1u, RapporConfigValidator::MinPower2Above(0));
  EXPECT_EQ(1u, RapporConfigValidator::MinPower2Above(1));
  EXPECT_EQ(2u, RapporConfigValidator::MinPower2Above(2));
  EXPECT_EQ(4u, RapporConfigValidator::MinPower2Above(3));
  EXPECT_EQ(4u, RapporConfigValidator::MinPower2Above(4));
  EXPECT_EQ(8u, RapporConfigValidator::MinPower2Above(5));
  EXPECT_EQ(8u, RapporConfigValidator::MinPower2Above(6));
  EXPECT_EQ(8u, RapporConfigValidator::MinPower2Above(7));
  EXPECT_EQ(8u, RapporConfigValidator::MinPower2Above(8));
  EXPECT_EQ(16u, RapporConfigValidator::MinPower2Above(9));
  EXPECT_EQ(16u, RapporConfigValidator::MinPower2Above(10));
  EXPECT_EQ(16u, RapporConfigValidator::MinPower2Above(11));
  EXPECT_EQ(16u, RapporConfigValidator::MinPower2Above(12));
  EXPECT_EQ(16u, RapporConfigValidator::MinPower2Above(13));
  EXPECT_EQ(16u, RapporConfigValidator::MinPower2Above(14));
  EXPECT_EQ(16u, RapporConfigValidator::MinPower2Above(15));
  EXPECT_EQ(16u, RapporConfigValidator::MinPower2Above(16));
  EXPECT_EQ(32u, RapporConfigValidator::MinPower2Above(17));
}

TEST(RapporConfigValidatorTest, TestConstructor) {
  RapporConfig config;
  config.set_prob_0_becomes_1(0.3);  // NOLINT
  config.set_prob_1_stays_1(0.7);    // NOLINT
  config.set_num_bloom_bits(64);     // NOLINT
  config.set_num_hashes(5);          // NOLINT

  config.set_num_cohorts(100);
  auto validator = RapporConfigValidator(config);
  EXPECT_EQ(128u, validator.num_cohorts_2_power());

  config.set_num_cohorts(200);
  validator = RapporConfigValidator(config);
  EXPECT_EQ(256u, validator.num_cohorts_2_power());

  config.set_num_cohorts(300);
  validator = RapporConfigValidator(config);
  EXPECT_EQ(512u, validator.num_cohorts_2_power());

  config.set_num_cohorts(400);
  validator = RapporConfigValidator(config);
  EXPECT_EQ(512u, validator.num_cohorts_2_power());

  config.set_num_cohorts(500);
  validator = RapporConfigValidator(config);
  EXPECT_EQ(512u, validator.num_cohorts_2_power());

  config.set_num_cohorts(600);  // NOLINT
  validator = RapporConfigValidator(config);
  EXPECT_EQ(1024u, validator.num_cohorts_2_power());

  config.set_num_cohorts(1024 - 1);
  validator = RapporConfigValidator(config);
  EXPECT_EQ(1024u, validator.num_cohorts_2_power());

  config.set_num_cohorts(1024);
  validator = RapporConfigValidator(config);
  EXPECT_EQ(1024u, validator.num_cohorts_2_power());
}

// Constructs a RapporEncoder with the given |config|, invokes
// Encode() with a dummy string and EncodeNullObservation(), and checks that the
// returned statuses are either kOK or kInvalidConfig, whichever is expected.
void TestRapporConfig(const RapporConfig& config, Status expected_status, int caller_line_number) {
  // Make a ClientSecret once and statically store the token.
  static const std::string kClientSecretToken = ClientSecret::GenerateNewSecret().GetToken();
  // Each time this function is invoked reconstitute the secret from the token.
  RapporEncoder encoder(config, ClientSecret::FromToken(kClientSecretToken));
  RapporObservation obs;
  ValuePart value;
  value.set_string_value("dummy");
  EXPECT_EQ(expected_status, encoder.Encode(value, &obs))
      << "Invoked from line number: " << caller_line_number;
}

// A macro to invoke testRapporConfig and pass it the current line number.
#define TEST_RAPPOR_CONFIG(config, expected_status) \
  (TestRapporConfig(config, expected_status, __LINE__))

// Tests the validation of config for String RAPPOR.
TEST(RapporEncoderTest, StringRapporConfigValidation) {
  // Empty config: Invalid
  RapporConfig config;
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Add two probabilities, still Invalid
  config.set_prob_0_becomes_1(0.3);  // NOLINT
  config.set_prob_1_stays_1(0.7);    // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_bloom_bits, still Invalid
  config.set_num_bloom_bits(8);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_hashes, still Invalid
  config.set_num_hashes(2);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // set num_cohorts: Valid
  config.set_num_cohorts(20);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kOK);

  // Explicitly set PRR to 0: Valid.
  config.set_prob_rr(0.0);
  TEST_RAPPOR_CONFIG(config, kOK);

  // Explicitly set PRR to non-zero: Invalid.
  config.set_prob_rr(0.1);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Explicitly set PRR back to zero: Valid.
  config.set_prob_rr(0.0);
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set one of the probabilities to negative: Invalid
  config.set_prob_0_becomes_1(-0.3);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set one of the probabilities to greater than 1: Invalid
  config.set_prob_0_becomes_1(1.3);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Fix the probability: Valid
  config.set_prob_0_becomes_1(0.3);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set the other probability to negative: Invalid
  config.set_prob_1_stays_1(-0.7);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set the other probability to greater than 1: Invalid
  config.set_prob_1_stays_1(1.7);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Fix the probability: Valid
  config.set_prob_1_stays_1(0.7);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set num_bloom_bits to negative: Invalid
  config.set_num_bloom_bits(-8);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_bloom_bits to 0: Invalid
  config.set_num_bloom_bits(0);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_bloom_bits back to positive: Valid
  config.set_num_bloom_bits(8);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set num_hashes to negative: Invalid
  config.set_num_hashes(-2);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_hashes to 0: Invalid
  config.set_num_hashes(0);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_hashes to 8: Invalid
  config.set_num_hashes(8);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_hashes back to positive: Valid
  config.set_num_hashes(2);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set num_cohorts to negative: Invalid
  config.set_num_cohorts(-20);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_cohorts to 0: Invalid
  config.set_num_cohorts(0);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_cohorts to 1025: Invalid
  config.set_num_cohorts(1025);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_cohorts to 1024: Valid
  config.set_num_cohorts(1024);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set num_cohorts back to positive: Valid
  config.set_num_cohorts(20);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set num_bloom_bits to equal num_hashes: Invalid
  config.set_num_bloom_bits(2);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_bloom_bits to greater than num_hashes and a power of 2: Valid
  config.set_num_bloom_bits(4);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set num_bloom_bits to greater than num_hashes but not a power of 2: Invalid
  config.set_num_bloom_bits(3);  // NOLINT
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Test with an invalid ClientSecret
  RapporEncoder encoder(config, ClientSecret::FromToken("Invalid Token"));
  RapporObservation obs;
  ValuePart value;
  value.set_string_value("dummy");
  EXPECT_EQ(kInvalidConfig, encoder.Encode(value, &obs));
}

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
  config.set_prob_0_becomes_1(0.3);  // NOLINT
  config.set_prob_1_stays_1(0.7);    // NOLINT
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Add one category: Invalid.
  config.mutable_string_categories()->add_category("cat");
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Add two more categories: Valid.
  config.mutable_string_categories()->add_category("dog");
  config.mutable_string_categories()->add_category("fish");
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Explicitly set PRR to 0: Valid.
  config.set_prob_rr(0.0);
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Explicitly set PRR to non-zero: Invalid.
  config.set_prob_rr(0.1);  // NOLINT
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Explicitly set PRR back to zero: Valid.
  config.set_prob_rr(0.0);
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Set one of the probabilities to negative: Invalid
  config.set_prob_0_becomes_1(-0.3);  // NOLINT
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set one of the probabilities to greater than 1: Invalid
  config.set_prob_0_becomes_1(1.3);  // NOLINT
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Fix the probability: Valid
  config.set_prob_0_becomes_1(0.3);  // NOLINT
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Set the other probability to negative: Invalid
  config.set_prob_1_stays_1(-0.7);  // NOLINT
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set the other the probability to greater than 1: Invalid
  config.set_prob_1_stays_1(1.7);  // NOLINT
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Fix the probability: Valid
  config.set_prob_1_stays_1(0.7);  // NOLINT
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Add an empty category: Invalid
  config.mutable_string_categories()->add_category("");
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
  config.set_prob_0_becomes_1(0.3);  // NOLINT
  config.set_prob_1_stays_1(0.7);    // NOLINT
  config.mutable_int_range_categories()->set_first(-1);
  config.mutable_int_range_categories()->set_last(1);

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
  config.set_prob_0_becomes_1(p);
  config.set_prob_1_stays_1(q);
  for (int i = 0; i < num_categories; i++) {
    config.mutable_string_categories()->add_category(CategoryName(i));
  }

  // Construct a BasicRapporEncoder.
  static const std::string kClientSecretToken = ClientSecret::GenerateNewSecret().GetToken();
  BasicRapporEncoder encoder(config, ClientSecret::FromToken(kClientSecretToken));

  // The expected number of bits in the encoding is the least multiple of 8
  // greater than or equal to num_categories.
  uint16_t expected_num_bits = 8 * (((num_categories - 1) / 8) + 1);  // NOLINT

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
  // NOLINTNEXTLINE
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
    config.set_prob_0_becomes_1(prob_0_becomes_1);
    config.set_prob_1_stays_1(prob_1_stays_1);
    for (int i = 0; i < num_categories; i++) {
      config.mutable_string_categories()->add_category(CategoryName(i));
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
  // NOLINTNEXTLINE
  for (int num_categories = 2; num_categories < 40; num_categories += 7) {
    for (int selected_category = 0; selected_category < num_categories;
         selected_category += (num_categories / 3 + 1)) {
      // The first parameter is prob_0_becomes_1, and the second parameter is
      // prob_1_stays_1.
      // NOLINTNEXTLINE
      DoChiSquaredTest(0.01, 0.99, num_categories, selected_category, 8.2);
      // NOLINTNEXTLINE
      DoChiSquaredTest(0.1, 0.9, num_categories, selected_category, 9.4);
      // NOLINTNEXTLINE
      DoChiSquaredTest(0.2, 0.8, num_categories, selected_category, 11.1);
      // NOLINTNEXTLINE
      DoChiSquaredTest(0.25, 0.75, num_categories, selected_category, 11.8);
      // NOLINTNEXTLINE
      DoChiSquaredTest(0.3, 0.7, num_categories, selected_category, 11.8);
    }
    // The first parameter is prob_0_becomes_1, and the second parameter is
    // prob_1_stays_1.
    // NOLINTNEXTLINE
    DoChiSquaredTestForNullObs(0.0, 1.0, num_categories, 0.0);
    // NOLINTNEXTLINE
    DoChiSquaredTestForNullObs(0.01, 0.99, num_categories, 8.2);
    // NOLINTNEXTLINE
    DoChiSquaredTestForNullObs(0.1, 0.9, num_categories, 9.4);
    // NOLINTNEXTLINE
    DoChiSquaredTestForNullObs(0.2, 0.8, num_categories, 11.1);
    // NOLINTNEXTLINE
    DoChiSquaredTestForNullObs(0.25, 0.75, num_categories, 11.8);
    // NOLINTNEXTLINE
    DoChiSquaredTestForNullObs(0.3, 0.7, num_categories, 11.8);
  }
}

// Test that BasicRapporEncoder::Encode() returns kInvalidArgument if a category
// name is used that is not one of the registered categories.
TEST(BasicRapporEncoderTest, BadCategory) {
  // Configure Basic RAPPOR with two categories, "dog" and "cat".
  BasicRapporConfig config;
  config.set_prob_0_becomes_1(0.3);  // NOLINT
  config.set_prob_1_stays_1(0.7);    // NOLINT
  config.mutable_string_categories()->add_category("dog");
  config.mutable_string_categories()->add_category("cat");

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
  config.set_prob_0_becomes_1(0.0);
  config.set_prob_1_stays_1(1.0);
  config.mutable_indexed_categories()->set_num_categories(5);  // NOLINT

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
  value.set_index_value(5);  // NOLINT
  EXPECT_EQ(kInvalidInput, encoder.Encode(value, &obs));
}

class StringRapporEncoderTest : public ::testing::Test {
 protected:
  uint32_t AttemptDeriveCohortFromSecret(size_t attempt_number) {
    return encoder_->AttemptDeriveCohortFromSecret(attempt_number);
  }

  uint32_t DeriveCohortFromSecret() { return encoder_->DeriveCohortFromSecret(); }

  void SetNewEncoder(const RapporConfig& config, const encoder::ClientSecret& secret) {
    encoder_ = std::make_unique<RapporEncoder>(config, secret);
  }

  std::string MakeBloomBits(const ValuePart& value) { return encoder_->MakeBloomBits(value); }

  // Using the given parameters, and using the fixed input string
  // "www.google.com" and a fixed cohort (i.e. a fixed client secret),
  // this test generates a String RAPPOR observation 1000 times, counts
  // the number of resulting 1's and 0's in two bit positions, and performs
  // Pearson's Chi-squared test to check for goodness of fit to a binomial
  // distribution with the appropriate parameter. Fails if
  // chi-squared >= |chi_squared_threshold|.
  //
  // First we examine the Bloom filter with no bits flipped and we find one
  // index of a set bit and one index of an unset bit. We perform the
  // Chi-squared test twice: once for each of these two indices.
  //
  // Uses DeterministicRandom in order to ensure reproducibility.
  void DoChiSquaredTest(float prob_0_becomes_1, float prob_1_stays_1, int num_bits, int num_hashes,
                        double chi_squared_threshold) {
    // Build the encoder.
    RapporConfig config;
    config.set_prob_0_becomes_1(prob_0_becomes_1);
    config.set_prob_1_stays_1(prob_1_stays_1);
    config.set_num_bloom_bits(num_bits);
    config.set_num_hashes(num_hashes);
    // This value will not be used but it needs to be something valid.
    config.set_num_cohorts(100);  // NOLINT
    // We use a fixed client secret so this test is deterministic.
    static const char kClientSecret[] = "4b4BxKq253TTCWIXFhLDTg==";
    SetNewEncoder(config, ClientSecret::FromToken(kClientSecret));
    // Give the encoder a deterministic RNG.
    encoder_->SetRandomForTesting(
        std::unique_ptr<crypto::Random>(new crypto::DeterministicRandom()));

    // Build the input value. We use a fixed input string so this test is
    // deterministic.
    ValuePart value;
    value.set_string_value("www.google.com");

    // Capture the indices of one bit that is set and one bit that is unset in
    // the bloom filter for the input value. It doesn't matter which two bits
    // we capture. We will do two chi-squared tests, one on each of the two
    // bits.
    int index_of_set_bit = -1;
    int index_of_unset_bit = -1;
    auto bloom_bits = MakeBloomBits(value);
    int num_bits_set = 0;
    for (int bit_index = 0; bit_index < num_bits; bit_index++) {
      if (IsSet(bloom_bits, bit_index)) {
        num_bits_set++;
        index_of_set_bit = bit_index;
      } else {
        index_of_unset_bit = bit_index;
      }
    }
    ASSERT_GT(num_bits_set, 0);
    ASSERT_LE(num_bits_set, num_hashes);
    // This heuristic for a minimum reasonable number of bits that should be
    // set was found by experimentation to allow the test to pass.
    int expected_min_num_bits_set = std::min(num_hashes - 2, num_bits / 4);
    ASSERT_GE(num_bits_set, expected_min_num_bits_set)
        << " num_bits=" << num_bits << " num_hashes=" << num_hashes;
    ASSERT_GE(index_of_set_bit, 0);
    ASSERT_LT(index_of_set_bit, num_bits);
    ASSERT_GE(index_of_unset_bit, 0);
    ASSERT_LT(index_of_unset_bit, num_bits);
    ASSERT_NE(index_of_set_bit, index_of_unset_bit);
    ASSERT_TRUE(IsSet(bloom_bits, index_of_set_bit));
    ASSERT_FALSE(IsSet(bloom_bits, index_of_unset_bit));

    // Encode the input value 1000 times, tallying the counts for the two bits.
    static const int kNumTrials = 1000;
    int set_bit_count = 0;
    int unset_bit_count = 0;
    for (size_t i = 0; i < kNumTrials; i++) {
      RapporObservation obs;
      EXPECT_EQ(kOK, encoder_->Encode(value, &obs));
      if (IsSet(obs.data(), index_of_set_bit)) {
        set_bit_count++;
      }
      if (IsSet(obs.data(), index_of_unset_bit)) {
        unset_bit_count++;
      }
    }

    // This is the expected number of ones and zeroes for a bit that is set
    // in the Bloom filter.
    const double expected_1_set = static_cast<double>(kNumTrials) * prob_1_stays_1;
    const double expected_0_set = static_cast<double>(kNumTrials) - expected_1_set;

    // This is the expected number of ones and zeroes for a bit that is
    // unset in the Bloom filter.
    const double expected_1_unset = static_cast<double>(kNumTrials) * prob_0_becomes_1;
    const double expected_0_unset = static_cast<double>(kNumTrials) - expected_1_unset;

    // Perform Chi-squared test twice, once for the set bit, once for the
    // unset bit.
    for (int i = 0; i < 2; i++) {
      double exp_0 = (i == 0 ? expected_0_set : expected_0_unset);
      double exp_1 = (i == 0 ? expected_1_set : expected_1_unset);
      int count = (i == 0 ? set_bit_count : unset_bit_count);

      // Difference between actual 1 count and expected 1 count.
      double delta_1 = static_cast<double>(count) - exp_1;

      // Difference between actual 0 count and expected 0 count.
      double delta_0 = static_cast<double>(kNumTrials - count) - exp_0;

      // Compute and check the Chi-Squared value.
      double chi_squared = delta_1 * delta_1 / exp_1 + delta_0 * delta_0 / exp_0;

      EXPECT_LT(chi_squared, chi_squared_threshold)
          << " delta_0=" << delta_0 << " delta_1=" << delta_1 << " num_bits=" << num_bits
          << " num_hashes=" << num_hashes << " i=" << i << " prob_0_becomes_1=" << prob_0_becomes_1
          << " prob_1_stays_1=" << prob_1_stays_1;
    }
  }

 public:
  std::unique_ptr<RapporEncoder> encoder_;
};

// We invoke AttemptDeriveCohortFromSecret() 1000 times using a fixed
// ClientSecret and increasing values for |attempt_number|. We use 16 buckets
// (i.e. num_cohorts_2_power = 16). The outputs should be approximately
// uniformly distributed over the integers in [0, 15].
TEST_F(StringRapporEncoderTest, AttemptDeriveCohortFromSecret) {
  RapporConfig config;
  // These config values are not relevant but need to be something valid.
  config.set_prob_0_becomes_1(0.3);  // NOLINT
  config.set_prob_1_stays_1(0.7);    // NOLINT
  config.set_num_bloom_bits(64);     // NOLINT
  config.set_num_hashes(5);          // NOLINT

  // We set num_cohorts to 10 so num_cohorts_2_power will be 16.
  config.set_num_cohorts(10);  // NOLINT

  // We use a fixed client secret so this test is deterministic.
  static std::string kClientSecret = "4b4BxKq253TTCWIXFhLDTg==";
  SetNewEncoder(config, ClientSecret::FromToken(kClientSecret));

  // Initialize counts to all zeroes.
  int counts[16] = {0};  // NOLINT readability-magic-numbers

  // Invoke AttemptDeriveCohortFromSecret() 1000 times with successive
  // attempt indices. Accumulate the results.
  for (int i = 0; i < 1000; i++) {  // NOLINT
    counts[AttemptDeriveCohortFromSecret(i)]++;
  }

  // These are the counts we found we get. 1000/16 = 62.5 is the expected value
  // for each count.
  //
  // Each of the results for buckets 10, 11 12, 13, 14 and 15 would have been
  // discarded and another attempt would have been made. That happened with
  // probability (75 + 71 + 56 + 58 + 44 + 53) / 1000 = 0.357.
  const int expected_counts[] = {58, 71, 69, 62, 70, 64, 76, 57, 48, 68, 75, 71, 56, 58, 44, 53};

  for (size_t i = 0; i < sizeof(expected_counts) / sizeof(expected_counts[0]); i++) {
    EXPECT_EQ(expected_counts[i], counts[i]) << "i=" << i;
  }
}

// We invoke DeriveCohortFromSecret() 1000 times using a varying ClientSecret.
// (We use a deterministic PRNG so the test is deterministic.)
// We use 10 buckets (i.e. num_cohorts = 10). The outputs should be
// approximately uniformly distributed over the integers in [0, 9].
TEST_F(StringRapporEncoderTest, DeriveCohortFromSecret) {
  RapporConfig config;
  // These config values are not relevant but need to be valid.
  config.set_prob_0_becomes_1(0.3);  // NOLINT
  config.set_prob_1_stays_1(0.7);    // NOLINT
  config.set_num_bloom_bits(64);     // NOLINT
  config.set_num_hashes(5);          // NOLINT

  // We set num_cohorts to 10.
  config.set_num_cohorts(10);  // NOLINT

  // Initialize counts to all zeroes.
  int counts[10] = {0};  // NOLINT

  crypto::DeterministicRandom deterministic_random;

  // Invoke DeriveCohortFromSecret() 1000 times. Accumulate the results.
  for (int i = 0; i < 1000; i++) {  // NOLINT
    SetNewEncoder(config, ClientSecret::GenerateNewSecret(&deterministic_random));
    // The constructor should have already invoked DeriveCohortFromSecret
    // and set cohort to that value.
    EXPECT_EQ(encoder_->cohort(), DeriveCohortFromSecret());
    counts[encoder_->cohort()]++;
  }

  // These are the counts we found we get. 1000/10 = 100 is the expected value
  // for each count.
  const int expected_counts[] = {85, 98, 104, 93, 113, 93, 89, 99, 103, 123};

  for (int i = 0; i < 10; i++) {  // NOLINT
    EXPECT_EQ(expected_counts[i], counts[i]) << "i=" << i;
  }
}

// We invoke MakeBloomBits 1000 times with a fixed cohort
// (i.e. a fixed ClientSecret) and varying input strings. We use 10 different
// initial segments of 100 different randomly generated strings. (We use a
// deterministic PRNG so this test is deterministic.) We use the values
// num_hashes = 2, num_bloom_bits = 16. We accumulate the counts of the number
// of times each bit is set. The counts should be approximately uniformly
// distributed over the integers in [0, 15].
TEST_F(StringRapporEncoderTest, MakeBloomBits) {
  RapporConfig config;
  // These config values are not relevant but need to be valid.
  config.set_prob_0_becomes_1(0.3);  // NOLINT
  config.set_prob_1_stays_1(0.7);    // NOLINT
  config.set_num_cohorts(10);        // NOLINT

  // Set the number of bloom bits to 16
  static const int kNumBloomBits = 16;
  config.set_num_bloom_bits(kNumBloomBits);

  // Set the number of hashes to 2.
  config.set_num_hashes(2);

  // We use a fixed client secret so this test is deterministic.
  static const char kClientSecret[] = "4b4BxKq253TTCWIXFhLDTg==";
  SetNewEncoder(config, ClientSecret::FromToken(kClientSecret));

  // Initialize counts to all zeroes.
  int counts[kNumBloomBits] = {0};

  crypto::DeterministicRandom prng;

  // We invoke MakeBloomBits() 1000 times and accumulate the results.

  // Generate 100 random strings of length 100.
  for (int i = 0; i < 100; i++) {   // NOLINT readability-magic-numbers
    crypto::byte random_bits[100];  // NOLINT readability-magic-numbers
    prng.RandomBytes(random_bits, sizeof(random_bits));
    // Use 10 progressively longer initial segments of |random_bits|.
    for (int size = 10; size <= 100; size += 10) {  // NOLINT
      ValuePart value;
      auto blob_value = std::string(reinterpret_cast<char*>(random_bits), size);
      value.set_blob_value(blob_value);
      auto bloom_bits = MakeBloomBits(value);
      // Capture which bits were set.
      int num_set = 0;
      for (int bit_index = 0; bit_index < kNumBloomBits; bit_index++) {
        if (IsSet(bloom_bits, bit_index)) {
          num_set++;
          counts[bit_index]++;
        }
      }
      // Since we are using 2 hashes the number of bits set should be 1 or 2.
      EXPECT_TRUE(num_set == 1 || num_set == 2);
    }
  }

  // These are the counts we found we get. 2000/16 = 125 is the
  // expected value for each count.
  const int expected_counts[] = {114, 139, 100, 113, 117, 118, 119, 122,
                                 137, 129, 114, 134, 116, 109, 137, 123};

  for (int i = 0; i < 16; i++) {  // NOLINT
    EXPECT_EQ(expected_counts[i], counts[i]) << "i=" << i;
  }
}

// For various numbers of bits, and hashes, and for various values of p and q
// we invoke DoChiSquaredTest().
TEST_F(StringRapporEncoderTest, ChiSquaredTest) {
  // Use num_bits = 4, 16, 64, 256, 1024
  for (int num_bits_exp = 2; num_bits_exp <= 10; num_bits_exp += 2) {  // NOLINT
    int num_bits = 1 << num_bits_exp;
    // Use num_hashes = 2, 5 and 8.
    int max_num_hashes = std::min(8, num_bits - 1);  // NOLINT
    for (int num_hashes = 2; num_hashes <= max_num_hashes; num_hashes += 3) {
      // The first two parameters are p and q.
      //
      // The last parameter is the chi-squared value to use. Notice that these
      // values were chosen by experimentation to be as small as possible so
      // that the test passes. They do not necessarily correspond to natural
      // confidence intervals for the chi-squared test.
      DoChiSquaredTest(0.01, 0.99, num_bits, num_hashes, 4.95);  // NOLINT
      DoChiSquaredTest(0.1, 0.9, num_bits, num_hashes, 5.38);    // NOLINT
      DoChiSquaredTest(0.2, 0.8, num_bits, num_hashes, 2.26);    // NOLINT
      DoChiSquaredTest(0.25, 0.75, num_bits, num_hashes, 2.83);  // NOLINT
      DoChiSquaredTest(0.3, 0.7, num_bits, num_hashes, 2.31);    // NOLINT
    }
  }
}

}  // namespace rappor
}  // namespace cobalt

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

#include "src/algorithms/forculus/forculus_encrypter.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::forculus {

using encoder::ClientSecret;

static const uint32_t kDayIndex = 12345;

void TestEncrypterValidation(uint32_t threshold, bool use_valid_token,
                             ForculusEncrypter::Status expected_status, int caller_line_number) {
  // Make a ClientSecret once and statically store the token.
  static const std::string kClientSecretToken = ClientSecret::GenerateNewSecret().GetToken();

  std::string client_secret_token = (use_valid_token ? kClientSecretToken : "Invalid Token");

  // Make a config with the given threshold
  ForculusConfig config;
  config.threshold = threshold;

  // Construct the Encrypter.
  ForculusEncrypter encrypter(config, 0, 0, 0, "", ClientSecret::FromToken(client_secret_token));

  // Invoke Encrypt() and check the status.
  ForculusObservation obs;
  EXPECT_EQ(expected_status, encrypter.Encrypt("hello", kDayIndex, &obs))
      << "Invoked from line number: " << caller_line_number;
}

// A macro to invoke TestEncrypterValidation and pass it the current line num.
#define TEST_ENCRYPTER_VALIDATION(threshold, use_valid_token, expected_status) \
  (TestEncrypterValidation(threshold, use_valid_token, expected_status, __LINE__))

// Tests ForculusEncrypter config and input validation.
TEST(ForculusEncrypterTest, Validation) {
  bool use_valid_token = true;

  // threshold = 1: kInvalidConfig
  TEST_ENCRYPTER_VALIDATION(1, use_valid_token, ForculusEncrypter::kInvalidConfig);

  // threshold = 2: kOK
  TEST_ENCRYPTER_VALIDATION(2, use_valid_token, ForculusEncrypter::kOK);

  // threshold = UINT32_MAX: kInvalidConfig
  TEST_ENCRYPTER_VALIDATION(UINT32_MAX, use_valid_token, ForculusEncrypter::kInvalidConfig);

  // threshold = 1000: kOK
  TEST_ENCRYPTER_VALIDATION(1000, use_valid_token, ForculusEncrypter::kOK);

  // invalid token: kInvalidConfig
  use_valid_token = false;
  TEST_ENCRYPTER_VALIDATION(1000, use_valid_token, ForculusEncrypter::kInvalidConfig);
}

// Constructs a ForculusEncrypter and invoke Encrypt().
ForculusObservation Encrypt(const std::string& plaintext, uint32_t threshold, uint32_t customer_id,
                            uint32_t project_id, uint32_t metric_id, std::string metric_part_name,
                            const std::string& secret_token, uint32_t day_index,
                            EpochType epoch_type) {
  // Make a config with the given threshold
  ForculusConfig config;
  config.threshold = threshold;
  config.epoch_type = epoch_type;

  // Construct the Encrypter.
  ForculusEncrypter encrypter(config, customer_id, project_id, metric_id,
                              std::move(metric_part_name), ClientSecret::FromToken(secret_token));

  // Invoke Encrypt() and check the status.
  ForculusObservation obs;
  EXPECT_EQ(ForculusEncrypter::kOK, encrypter.Encrypt(plaintext, day_index, &obs));
  return obs;
}

// We test all that we can about the Encrypter without doing any decryption.
// See forculus_decrypter_test.cc for tests that involve decryption.
TEST(ForculusEncrypterTest, SanityTest) {
  static const std::string kToken1 = ClientSecret::GenerateNewSecret().GetToken();
  static const std::string kToken2 = ClientSecret::GenerateNewSecret().GetToken();
  static const uint32_t kThreshold = 20;
  static const uint32_t kOneWeek = 7;
  static const uint32_t kOneMonth = 31;
  uint32_t day_index1 = kDayIndex;
  uint32_t day_index2 = day_index1 + 1;

  // The encryption and points should be deterministic as a function of
  // the inputs.
  ForculusObservation obs1 =
      Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  ForculusObservation obs2 =
      Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  EXPECT_EQ(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_EQ(obs1.point_x(), obs2.point_x());
  EXPECT_EQ(obs1.point_y(), obs2.point_y());

  // Different epochs should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  obs2 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index2, DAY);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Two different days in the same week are the same epoch if we are using
  // WEEK epochs.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, WEEK);
  obs2 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1 + 1, WEEK);
  EXPECT_EQ(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_EQ(obs1.point_x(), obs2.point_x());
  EXPECT_EQ(obs1.point_y(), obs2.point_y());

  // But two different days in different weeks are different epochs if we
  // are using WEEK epochs.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, WEEK);
  obs2 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1 + kOneWeek, WEEK);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Similarly two different days in the same month are the same epoch if we
  // are using MONTH epochs.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, MONTH);
  obs2 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1 + kOneWeek, MONTH);
  EXPECT_EQ(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_EQ(obs1.point_x(), obs2.point_x());
  EXPECT_EQ(obs1.point_y(), obs2.point_y());

  // But two different days in different months are different epochs if we
  // are using MONTH epochs.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, MONTH);
  obs2 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1 + kOneMonth, MONTH);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different epochs types should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  obs2 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, WEEK);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different epochs types should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  obs2 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, MONTH);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different epochs types should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, WEEK);
  obs2 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, MONTH);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different tokens should yield the same ciphertexts but different points.
  // This represents different clients doing the same threshold encryption.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  obs2 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken2, day_index1, DAY);
  EXPECT_EQ(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different metric parts should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  obs2 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part2", kToken1, day_index1, DAY);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different customer_ids should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  obs2 = Encrypt("Message 1", kThreshold, 2, 1, 1, "part1", kToken1, day_index1, DAY);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different project_ids should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  obs2 = Encrypt("Message 1", kThreshold, 1, 2, 1, "part1", kToken1, day_index1, DAY);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different metric_ids should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  obs2 = Encrypt("Message 1", kThreshold, 1, 1, 2, "part1", kToken1, day_index1, DAY);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different thresholds should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  obs2 = Encrypt("Message 1", kThreshold + 1, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different plaintexts should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  obs2 = Encrypt("Message 2", kThreshold, 1, 1, 1, "part1", kToken1, day_index1, DAY);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());
}

// We sanity test the function EncryptValue().
// See forculus_decrypter_test.cc for tests that involve decryption.
TEST(ForculusEncrypterTest, EncryptValue) {
  static const uint32_t kThreshold = 20;

  // Construct an Encrypter.
  ForculusConfig config;
  config.threshold = kThreshold;
  ForculusEncrypter encrypter(config, 1, 1, 1, "", ClientSecret::GenerateNewSecret());

  // Construct three values.
  ValuePart value1, value2, value3;
  value1.set_int_value(42);  // NOLINT readability-magic-numbers
  value2.set_string_value("42");
  value3.set_blob_value("42");

  // Invoke EncryptValue() three times.
  ForculusObservation obs1, obs2, obs3;
  EXPECT_EQ(ForculusEncrypter::kOK, encrypter.EncryptValue(value1, kDayIndex, &obs1));
  EXPECT_EQ(ForculusEncrypter::kOK, encrypter.EncryptValue(value2, kDayIndex, &obs2));
  EXPECT_EQ(ForculusEncrypter::kOK, encrypter.EncryptValue(value3, kDayIndex, &obs3));

  // Check that the three observations have different ciphertexts.
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.ciphertext(), obs3.ciphertext());
  EXPECT_NE(obs2.ciphertext(), obs3.ciphertext());
}

}  // namespace cobalt::forculus

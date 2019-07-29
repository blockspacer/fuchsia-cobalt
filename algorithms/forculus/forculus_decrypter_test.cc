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

#include "algorithms/forculus/forculus_decrypter.h"

#include <map>

#include "algorithms/forculus/forculus_encrypter.h"
#include "encoder/client_secret.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace forculus {

using encoder::ClientSecret;

static const uint32_t kThreshold = 20;
static const uint32_t kDayIndex = 12345;

// Encrypts the plaintext using Forculus encryption with threshold = kThreshold
// and default values for the other parameters and a fresh ClientSecret will
// be generated each time this function is invoked..
ForculusObservation Encrypt(const std::string& plaintext) {
  // Make a config with the given threshold
  ForculusConfig config;
  config.set_threshold(kThreshold);

  // Construct an Encrypter.
  ForculusEncrypter encrypter(config, 0, 0, 0, "", ClientSecret::GenerateNewSecret());

  // Invoke Encrypt() and check the status.
  ForculusObservation obs;
  EXPECT_EQ(ForculusEncrypter::kOK, encrypter.Encrypt(plaintext, kDayIndex, &obs));
  return obs;
}

// This function is similar to the function Encrypt() above except that this
// function invokes ForculusEncrypter::EncryptValue() instead of
// ForculusEncrypter::Encrypt().
ForculusObservation EncryptValue(const ValuePart& value) {
  // Make a config with the given threshold
  ForculusConfig config;
  config.set_threshold(kThreshold);

  // Construct an Encrypter.
  ForculusEncrypter encrypter(config, 0, 0, 0, "", ClientSecret::GenerateNewSecret());

  // Invoke EncryptValue() and check the status.
  ForculusObservation obs;
  EXPECT_EQ(ForculusEncrypter::kOK, encrypter.EncryptValue(value, kDayIndex, &obs));
  return obs;
}

// Simulates kThreshold different clients generating ciphertexts for the
// same plaintext. Verifies that the plaintext will be properly decrypted.
TEST(ForculusDecrypterTest, TestSuccessfulDecryption) {
  const std::string plaintext("The woods are lovely, dark and deep.");
  ForculusDecrypter* decrypter = nullptr;
  for (size_t i = 0; i < kThreshold; i++) {
    auto observation = Encrypt(plaintext);
    if (!decrypter) {
      decrypter = new ForculusDecrypter(kThreshold, observation.ciphertext());
    } else {
      EXPECT_EQ(decrypter->ciphertext(), observation.ciphertext());
    }
    decrypter->AddObservation(observation);
  }
  std::string recovered_text;
  EXPECT_EQ(ForculusDecrypter::kOK, decrypter->Decrypt(&recovered_text));
  EXPECT_EQ(plaintext, recovered_text);

  delete decrypter;
}

// This function is similar to TestSuccesfulDecryption above except that it
// invokes EncryptValue() instead of Encrypt(). It is used in
// TestValueDecryption below.
void DoDecryptValueTest(const ValuePart& value) {
  ForculusDecrypter* decrypter = nullptr;
  for (size_t i = 0; i < kThreshold; i++) {
    auto observation = EncryptValue(value);
    if (!decrypter) {
      decrypter = new ForculusDecrypter(kThreshold, observation.ciphertext());
    } else {
      EXPECT_EQ(decrypter->ciphertext(), observation.ciphertext());
    }
    decrypter->AddObservation(observation);
  }
  std::string recovered_text;
  EXPECT_EQ(ForculusDecrypter::kOK, decrypter->Decrypt(&recovered_text));
  ValuePart recovered_value;
  recovered_value.ParseFromString(recovered_text);
  switch (value.data_case()) {
    case ValuePart::kStringValue:
      EXPECT_EQ(value.string_value(), recovered_value.string_value());
      break;

    case ValuePart::kIntValue:
      EXPECT_EQ(value.int_value(), recovered_value.int_value());
      break;

    case ValuePart::kBlobValue:
      EXPECT_EQ(value.blob_value(), recovered_value.blob_value());
      break;

    default:
      EXPECT_TRUE(false) << "unexpected case";
  }

  delete decrypter;
}

// This test is similar to TestSuccessfulDecryption but it uses the function
// EncryptValue() instead of the function Encrypt()
TEST(ForculusDecrypterTest, TestValueDecryption) {
  ValuePart value;
  // Test with a string value.
  value.set_string_value("42");
  DoDecryptValueTest(value);

  // Test with an int value.
  value.set_int_value(42);  // NOLINT readability-magic-numbers
  DoDecryptValueTest(value);

  // Test with a blob value.
  value.set_blob_value("42");
  DoDecryptValueTest(value);
}

// Verifies that ForculusDecrypter returns appropriate error statuses.
TEST(ForculusDecrypterTest, TestErrors) {
  // Construct Observation 1.
  ForculusObservation obs1;
  obs1.set_ciphertext("A ciphertext");
  obs1.set_point_x("12345");
  obs1.set_point_y("abcde");

  // Construct Observation 2 with the same ciphertext and the same x-value
  // but a different y-value.
  ForculusObservation obs2;
  obs2.set_ciphertext("A ciphertext");
  obs2.set_point_x("12345");
  obs2.set_point_y("fghij");

  // Construct a decrypter with the same ciphertext and a threshold of 3.
  ForculusDecrypter decrypter(3, "A ciphertext");
  EXPECT_EQ("A ciphertext", decrypter.ciphertext());

  // It is ok to add the same observation twice. It will be ignored the
  // second time.
  EXPECT_EQ(ForculusDecrypter::kOK, decrypter.AddObservation(obs1));
  EXPECT_EQ(ForculusDecrypter::kOK, decrypter.AddObservation(obs1));
  EXPECT_EQ(1u, decrypter.size());

  // Trying to add Obervation 2 will yield kInconsistentPoints.
  EXPECT_EQ(ForculusDecrypter::kInconsistentPoints, decrypter.AddObservation(obs2));

  // Trying to decrypt now will yield kNontEnoughPoints.
  std::string plaintext;
  EXPECT_EQ(ForculusDecrypter::kNotEnoughPoints, decrypter.Decrypt(&plaintext));

  // Change Observation 2 to have a different x-value and a different
  // ciphertext. Now trying to add it yields kWrongCiphertext.
  obs2.set_ciphertext("A different ciphertext");
  obs2.set_point_x("23456");
  EXPECT_EQ(ForculusDecrypter::kWrongCiphertext, decrypter.AddObservation(obs2));

  // Fix observation 2 and we can successfully add it.
  obs2.set_ciphertext("A ciphertext");
  obs2.set_point_x("23456");
  EXPECT_EQ(ForculusDecrypter::kOK, decrypter.AddObservation(obs2));
  EXPECT_EQ(2u, decrypter.size());

  // Still not enough points.
  EXPECT_EQ(ForculusDecrypter::kNotEnoughPoints, decrypter.Decrypt(&plaintext));

  // Change observation 2 to a third point and add it.
  obs2.set_ciphertext("A ciphertext");
  obs2.set_point_x("45678");
  EXPECT_EQ(ForculusDecrypter::kOK, decrypter.AddObservation(obs2));
  EXPECT_EQ(3u, decrypter.size());

  // Now there are enough points to try to decrypt but the decryption will
  // fail because the ciphertext is not a real ciphertext.
  EXPECT_EQ(ForculusDecrypter::kDecryptionFailed, decrypter.Decrypt(&plaintext));
}

}  // namespace forculus
}  // namespace cobalt

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

#include "util/crypto_util/cipher.h"

#include <gtest/gtest.h>
#include <limits.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>

#include <string>
#include <vector>

#include "util/crypto_util/errors.h"
#include "util/crypto_util/random.h"
#include "util/crypto_util/types.h"

namespace cobalt {
namespace crypto {

constexpr char* kLine1 = "The woods are lovely, dark and deep,\n";
constexpr char* kLine2 = "But I have promises to keep,\n";
constexpr char* kLine3 = "And miles to go before I sleep,\n";
constexpr char* kLine4 = "And miles to go before I sleep.";
constexpr char* kLines[] = {kLine1, kLine2, kLine3, kLine4};
constexpr int kNumLines = 4;

// Tests SymmetricCipher().

// This function is invoked by the SymmetricCipherTest.
// It encrypts and then decrypts |plain_text| and checks that the
// recovered text is equal to the plain text. It generates a random
// key and nonce.
void doSymmetricCipherTest(SymmetricCipher* cipher, const byte* plain_text,
                           int ptext_len) {
  // Initialize
  byte key[SymmetricCipher::KEY_SIZE];
  byte nonce[SymmetricCipher::NONCE_SIZE];
  Random rand;
  rand.RandomBytes(key, SymmetricCipher::KEY_SIZE);
  rand.RandomBytes(nonce, SymmetricCipher::NONCE_SIZE);
  EXPECT_TRUE(cipher->set_key(key)) << GetLastErrorMessage();

  // Encrypt
  std::vector<byte> cipher_text;
  EXPECT_TRUE(cipher->Encrypt(nonce, plain_text, ptext_len, &cipher_text))
      << GetLastErrorMessage();

  // Decrypt
  std::vector<byte> recovered_text;
  EXPECT_TRUE(cipher->Decrypt(nonce, cipher_text.data(), cipher_text.size(),
                              &recovered_text))
      << GetLastErrorMessage();

  // Compare
  EXPECT_EQ(
      std::string((const char*)recovered_text.data(), recovered_text.size()),
      std::string((const char*)plain_text));
}

TEST(SymmetricCipherTest, TestManyStrings) {
  SymmetricCipher cipher;

  // Test once with each line separately.
  for (int i = 0; i < kNumLines; i++) {
    doSymmetricCipherTest(&cipher, (const byte*)kLines[i], strlen(kLines[i]));
  }

  // Test once with all lines together.
  std::string all_lines;
  for (int i = 0; i < kNumLines; i++) {
    all_lines += kLines[i];
  }
  doSymmetricCipherTest(&cipher, (const byte*)all_lines.data(),
                        all_lines.size());

  // Test once with a longer string: Repeat string 32 times.
  for (int i = 0; i < 5; i++) {
    all_lines += all_lines;
  }
  doSymmetricCipherTest(&cipher, (const byte*)all_lines.data(),
                        all_lines.size());
}

// This function is invoked by the HybridCipherTest
// It encrypts and then decrypts |plain_text| and checks that the
// recovered text is equal to the plain text.
void doHybridCipherTest(HybridCipher* hybrid_cipher, const byte* plain_text,
                        int ptext_len, std::string public_key,
                        std::string private_key) {
  // Encrypt
  std::vector<byte> cipher_text;
  ASSERT_TRUE(hybrid_cipher->set_public_key_pem(public_key))
      << GetLastErrorMessage();
  ASSERT_TRUE(hybrid_cipher->Encrypt(plain_text, ptext_len, &cipher_text))
      << GetLastErrorMessage();
  byte fingerprint[HybridCipher::PUBLIC_KEY_FINGERPRINT_SIZE];
  ASSERT_TRUE(hybrid_cipher->public_key_fingerprint(fingerprint))
      << GetLastErrorMessage();

  // Decrypt
  std::vector<byte> recovered_text;
  ASSERT_TRUE(hybrid_cipher->set_private_key_pem(private_key))
      << GetLastErrorMessage();
  ASSERT_TRUE(hybrid_cipher->Decrypt(cipher_text.data(), cipher_text.size(),
                                     &recovered_text))
      << GetLastErrorMessage();

  // Compare
  EXPECT_EQ(
      std::string((const char*)recovered_text.data(), recovered_text.size()),
      std::string((const char*)plain_text));

  // Decrypt with flipped salt
  cipher_text.data()[HybridCipher::PUBLIC_KEY_SIZE] ^=
      0x1;  // flip a bit in the first byte of the salt
  EXPECT_FALSE(hybrid_cipher->Decrypt(cipher_text.data(), cipher_text.size(),
                                      &recovered_text))
      << GetLastErrorMessage();

  // Decrypt with modified public_key_part
  cipher_text.data()[HybridCipher::PUBLIC_KEY_SIZE] ^=
      0x1;                       // flip salt bit back
  cipher_text.data()[2] ^= 0x1;  // flip any bit except in first byte (due to
                                 // X9.62 serialization)
  EXPECT_FALSE(hybrid_cipher->Decrypt(cipher_text.data(), cipher_text.size(),
                                      &recovered_text))
      << GetLastErrorMessage();
}

void doGenerateKeys(std::string* public_key, std::string* private_key) {
  ASSERT_TRUE(HybridCipher::GenerateKeyPairPEM(public_key, private_key))
      << GetLastErrorMessage();
}

TEST(HybridCipherTest, Test) {
  HybridCipher hybrid_cipher;
  std::string public_key;
  std::string private_key;

  // Test with five different key pairs
  for (int times = 0; times < 5; ++times) {
    doGenerateKeys(&public_key, &private_key);

    // Test once with each line separately.
    for (int i = 0; i < kNumLines; i++) {
      doHybridCipherTest(&hybrid_cipher, (const byte*)kLines[i],
                         strlen(kLines[i]), public_key, private_key);
    }

    // Test once with all lines together.
    std::string all_lines;
    for (int i = 0; i < kNumLines; i++) {
      all_lines += kLines[i];
    }
    doHybridCipherTest(&hybrid_cipher, (const byte*)all_lines.data(),
                       all_lines.size(), public_key, private_key);

    // Test once with a longer string: Repeat string 32 times.
    for (int i = 0; i < 5; i++) {
      all_lines += all_lines;
    }
    doHybridCipherTest(&hybrid_cipher, (const byte*)all_lines.data(),
                       all_lines.size(), public_key, private_key);
  }
}

}  // namespace crypto

}  // namespace cobalt

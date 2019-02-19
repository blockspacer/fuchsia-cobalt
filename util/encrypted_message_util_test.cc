// Copyright 2017 The Fuchsia Authors
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

#include "util/encrypted_message_util.h"

#include <string>
#include <utility>

#include "./encrypted_message.pb.h"
#include "./observation.pb.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "third_party/tink/cc/cleartext_keyset_handle.h"
#include "third_party/tink/cc/hybrid_config.h"
#include "third_party/tink/cc/hybrid_decrypt_factory.h"
#include "third_party/tink/cc/hybrid_encrypt_factory.h"
#include "third_party/tink/cc/hybrid_key_templates.h"
#include "third_party/tink/cc/keyset_handle.h"
#include "third_party/tink/include/proto/tink.pb.h"
#include "util/crypto_util/base64.h"
#include "util/crypto_util/cipher.h"

namespace cobalt {
namespace util {

using crypto::HybridCipher;

Observation MakeDummyObservation(std::string part_name) {
  Observation observation;
  (*observation.mutable_parts())[part_name] = ObservationPart();
  return observation;
}

// Tests the use of the no-encryption option.
TEST(EncryptedMessageUtilTest, NoEncryption) {
  // Make a dummy observation.
  auto observation = MakeDummyObservation("hello");
  // Make an EncryptedMessageMaker that uses the NONE encryption scheme.
  auto maker = EncryptedMessageMaker::MakeAllowUnencrypted(
                   "dummy_key", EncryptedMessage::NONE)
                   .ValueOrDie();
  // Encrypt the dummy observation.
  EncryptedMessage encrypted_message;
  EXPECT_TRUE(maker->Encrypt(observation, &encrypted_message));

  // Make a MessageDecrypter.
  MessageDecrypter decrypter("dummy_key");
  // Decrypt and check.
  observation.Clear();
  EXPECT_TRUE(decrypter.DecryptMessage(encrypted_message, &observation));
  EXPECT_EQ(1u, observation.parts().count("hello"));
}

// Tests the use of bad encryption keys
TEST(EncryptedMessageUtilTest, BadKeys) {
  // Make a dummy observation.
  auto observation = MakeDummyObservation("hello");

  // Make an EncryptedMessageMaker that uses a bad public key.
  auto maker =
      EncryptedMessageMaker::Make("dummy_key", EncryptedMessage::HYBRID_ECDH_V1)
          .ValueOrDie();
  // Try to encrypt the dummy observation.
  EncryptedMessage encrypted_message;
  // Expect it to fail, but not crash.
  EXPECT_FALSE(maker->Encrypt(observation, &encrypted_message));
}

// Tests the use of the hybrid cipher option.
TEST(EncryptedMessageUtilTest, HybridEncryption) {
  std::string public_key;
  std::string private_key;
  EXPECT_TRUE(HybridCipher::GenerateKeyPairPEM(&public_key, &private_key));

  // Make a dummy observation.
  auto observation = MakeDummyObservation("hello");

  // Make an EncryptedMessageMaker that uses our real encryption scheme.
  auto maker =
      EncryptedMessageMaker::Make(public_key, EncryptedMessage::HYBRID_ECDH_V1)
          .ValueOrDie();
  // Encrypt the dummy observation.
  EncryptedMessage encrypted_message;
  ASSERT_TRUE(maker->Encrypt(observation, &encrypted_message));
  EXPECT_EQ(32u, encrypted_message.public_key_fingerprint().size());

  // Make a MessageDecrypter.
  MessageDecrypter decrypter(private_key);
  // Decrypt and check.
  observation.Clear();
  ASSERT_TRUE(decrypter.DecryptMessage(encrypted_message, &observation));
  EXPECT_EQ(1u, observation.parts().count("hello"));

  // Make a MessageDecrypter that uses a bad private key.
  MessageDecrypter bad_decrypter("dummy_key");
  // Try to decrypt.
  // Expect it to fail, but not crash.
  EXPECT_FALSE(bad_decrypter.DecryptMessage(encrypted_message, &observation));
}

// Tests that Make does not allow the NONE scheme.
TEST(EncryptedMessageUtilTest, DisallowUnencrypted) {
  // Make a dummy observation.
  auto observation = MakeDummyObservation("hello");
  // Try to make an EncryptedMessageMaker that uses the NONE encryption scheme.
  auto status =
      EncryptedMessageMaker::Make("dummy_key", EncryptedMessage::NONE);
  EXPECT_EQ(INVALID_ARGUMENT, status.status().error_code());
}

// Tests that using encryption incorrectly fails but doesn't cause any crashes.
TEST(EncryptedMessageUtilTest, Crazy) {
  std::string public_key;
  std::string private_key;
  EXPECT_TRUE(HybridCipher::GenerateKeyPairPEM(&public_key, &private_key));

  // Make a dummy observation.
  auto observation = MakeDummyObservation("hello");

  // Make an EncryptedMessageMaker that incorrectly uses the private key
  // instead of the public key
  auto bad_maker =
      EncryptedMessageMaker::Make(private_key, EncryptedMessage::HYBRID_ECDH_V1)
          .ValueOrDie();

  // Try to encrypt the dummy observation.
  EncryptedMessage encrypted_message;
  // Expect it to fail, but not crash.
  EXPECT_FALSE(bad_maker->Encrypt(observation, &encrypted_message));

  // Now make a good EncryptedMessageMaker
  auto real_maker =
      EncryptedMessageMaker::Make(public_key, EncryptedMessage::HYBRID_ECDH_V1)
          .ValueOrDie();
  // Encrypt the dummy observation.
  EXPECT_TRUE(real_maker->Encrypt(observation, &encrypted_message));

  // Make a MessageDecrypter that uses the correct private key.
  MessageDecrypter real_decrypter(private_key);
  // Decrypt and check.
  observation.Clear();
  EXPECT_TRUE(real_decrypter.DecryptMessage(encrypted_message, &observation));
  EXPECT_EQ(1u, observation.parts().count("hello"));

  // Make a MessageDecrypter that incorrectly uses the public key
  MessageDecrypter bad_decrypter(public_key);
  // Try to decrypt.
  // Expect it to fail, but not crash.
  EXPECT_FALSE(bad_decrypter.DecryptMessage(encrypted_message, &observation));

  // Try to decrypt corrupted ciphertext.
  // Expect it to fail, but not crash.
  encrypted_message.mutable_ciphertext()[0] =
      encrypted_message.ciphertext()[0] + 1;
  EXPECT_FALSE(real_decrypter.DecryptMessage(encrypted_message, &observation));
}

// Check some ways that creating a HybridTinkEncryptedMessageMaker could fail.
TEST(HybridTinkEncryptedMessageMaker, Make) {
  // Check that an empty key is rejected.
  auto result = cobalt::util::EncryptedMessageMaker::MakeHybridTink("");
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());

  // Check that a key with invalid base64 characters is rejected.
  result = cobalt::util::EncryptedMessageMaker::MakeHybridTink("'''");
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());
}

// Check that the HybridTinkEncryptedMessageMaker is correctly created and
// encrypts the observations it is given.
TEST(HybridTinkEncryptedMessageMaker, Encrypt) {
  auto status = ::crypto::tink::HybridConfig::Register();
  EXPECT_TRUE(status.ok());

  // Generate a handle to a new public-private key set.
  auto keyset_handle_result = ::crypto::tink::KeysetHandle::GenerateNew(
      ::crypto::tink::HybridKeyTemplates::EciesP256HkdfHmacSha256Aes128Gcm());
  EXPECT_TRUE(keyset_handle_result.ok());

  // Get a handle to the public key.
  auto public_keyset_handle_result =
      keyset_handle_result.ValueOrDie()->GetPublicKeysetHandle();
  EXPECT_TRUE(public_keyset_handle_result.ok());

  // Get the keyset protobuf message itself.
  const google::crypto::tink::Keyset public_keyset =
      ::crypto::tink::CleartextKeysetHandle::GetKeyset(
          *(public_keyset_handle_result.ValueOrDie()));
  EXPECT_EQ(1, public_keyset.key_size());

  // Serialize an encode the public key in the format expected by
  // HybridTinkEncryptedMessageMaker.
  std::string serialized_public_keyset;
  EXPECT_TRUE(public_keyset.SerializeToString(&serialized_public_keyset));

  std::string base64_public_keyset;
  EXPECT_TRUE(cobalt::crypto::Base64Encode(serialized_public_keyset,
                                           &base64_public_keyset));

  auto maker =
      cobalt::util::EncryptedMessageMaker::MakeHybridTink(base64_public_keyset);
  EXPECT_TRUE(maker.ok());

  std::string expected = "hello";
  // Make a dummy observation.
  auto observation = MakeDummyObservation(expected);

  // Encrypt the dummy observation.
  EncryptedMessage encrypted_message;
  EXPECT_TRUE(maker.ValueOrDie()->Encrypt(observation, &encrypted_message));
  EXPECT_EQ(EncryptedMessage::HYBRID_TINK, encrypted_message.scheme());

  // Obtain a decrypter to be able to check the encrypted dummy observation.
  auto decrypter_result = keyset_handle_result.ValueOrDie()
                              ->GetPrimitive<::crypto::tink::HybridDecrypt>();
  EXPECT_TRUE(decrypter_result.ok());
  auto decrypter = std::move(decrypter_result.ValueOrDie());
  auto decrypted_result =
      decrypter->Decrypt(encrypted_message.ciphertext(), "");
  EXPECT_TRUE(decrypted_result.ok());

  observation.Clear();
  EXPECT_EQ(0u, observation.parts().count(expected));
  EXPECT_TRUE(observation.ParseFromString(decrypted_result.ValueOrDie()));

  EXPECT_EQ(1u, observation.parts().count(expected));
}

}  // namespace util
}  // namespace cobalt

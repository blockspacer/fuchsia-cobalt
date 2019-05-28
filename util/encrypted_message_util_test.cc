// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/encrypted_message_util.h"

#include <string>
#include <utility>

#include "./encrypted_message.pb.h"
#include "./envelope.pb.h"
#include "./key.pb.h"
#include "./observation.pb.h"
#include "./observation2.pb.h"
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

const char kShufflerContextInfo[] = "cobalt-1.0-shuffler";
const char kAnalyzerContextInfo[] = "cobalt-1.0-analyzer";

using crypto::HybridCipher;

Observation MakeDummyObservation(std::string part_name) {
  Observation observation;
  (*observation.mutable_parts())[part_name] = ObservationPart();
  return observation;
}

std::string MakeCobaltEncryptionKeyBytes(
    const std::string& key_bytes, uint32_t key_index,
    CobaltEncryptionKey::KeyPurpose purpose) {
  CobaltEncryptionKey key;
  key.set_serialized_key(key_bytes);
  key.set_key_index(key_index);
  key.set_purpose(purpose);
  std::string cobalt_key_bytes;
  EXPECT_TRUE(key.SerializeToString(&cobalt_key_bytes));
  return cobalt_key_bytes;
}

// Tests the use of the no-encryption option.
TEST(EncryptedMessageUtilTest, NoEncryption) {
  // Make a dummy observation.
  auto observation = MakeDummyObservation("hello");
  // Make an EncryptedMessageMaker that uses the NONE encryption scheme.
  auto maker = EncryptedMessageMaker::MakeUnencrypted();
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
  auto maker = EncryptedMessageMaker::MakeHybridEcdh("dummy_key").ValueOrDie();
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
  auto maker = EncryptedMessageMaker::MakeHybridEcdh(public_key).ValueOrDie();
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
      EncryptedMessageMaker::MakeHybridEcdh(private_key).ValueOrDie();

  // Try to encrypt the dummy observation.
  EncryptedMessage encrypted_message;
  // Expect it to fail, but not crash.
  EXPECT_FALSE(bad_maker->Encrypt(observation, &encrypted_message));

  // Now make a good EncryptedMessageMaker
  auto real_maker =
      EncryptedMessageMaker::MakeHybridEcdh(public_key).ValueOrDie();
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
  auto result = EncryptedMessageMaker::MakeHybridTink("");
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());

  // Check that a key with invalid base64 characters is rejected.
  result = EncryptedMessageMaker::MakeHybridTink("'''");
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
  EXPECT_TRUE(
      crypto::Base64Encode(serialized_public_keyset, &base64_public_keyset));

  auto maker = EncryptedMessageMaker::MakeHybridTink(base64_public_keyset);
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

class EncryptedMessageMakerTest : public ::testing::Test {
 public:
  // Get a serialized Keyset proto for a public key.
  std::string GetPublicKeysetBytes() const {
    // Get the keyset protobuf message itself.
    const google::crypto::tink::Keyset public_keyset =
        ::crypto::tink::CleartextKeysetHandle::GetKeyset(
            *GetPublicKeysetHandle());
    EXPECT_EQ(1, public_keyset.key_size());

    // Serialize and encode the public keyset.
    std::string public_keyset_bytes;
    EXPECT_TRUE(public_keyset.SerializeToString(&public_keyset_bytes));
    return public_keyset_bytes;
  }

  // Decrypt the specified ciphertext.
  std::string Decrypt(const std::string& ciphertext,
                      const std::string& context_info) {
    // Obtain a decrypter to be able to check the encrypted dummy observation.
    auto decrypter_result =
        keyset_handle_->GetPrimitive<::crypto::tink::HybridDecrypt>();
    EXPECT_TRUE(decrypter_result.ok());
    auto decrypter = std::move(decrypter_result.ValueOrDie());
    auto decrypted_result = decrypter->Decrypt(ciphertext, context_info);
    EXPECT_TRUE(decrypted_result.ok());
    return decrypted_result.ValueOrDie();
  }

 protected:
  void SetUp() {
    auto status = ::crypto::tink::HybridConfig::Register();
    EXPECT_TRUE(status.ok());

    // Create a new key pair for testing.
    auto keyset_handle_result = ::crypto::tink::KeysetHandle::GenerateNew(
        ::crypto::tink::HybridKeyTemplates::EciesP256HkdfHmacSha256Aes128Gcm());
    EXPECT_TRUE(keyset_handle_result.ok());
    keyset_handle_ = std::move(keyset_handle_result.ValueOrDie());
  }

 private:
  std::unique_ptr<::crypto::tink::KeysetHandle> GetPublicKeysetHandle() const {
    auto public_keyset_handle_result = keyset_handle_->GetPublicKeysetHandle();
    EXPECT_TRUE(public_keyset_handle_result.ok());
    return std::move(public_keyset_handle_result.ValueOrDie());
  }

  std::unique_ptr<::crypto::tink::KeysetHandle> keyset_handle_;
};

// Try to roundtrip an observation through a message encrypter.
TEST_F(EncryptedMessageMakerTest, EncryptObservation) {
  uint32_t key_index = 1;
  auto key_bytes = MakeCobaltEncryptionKeyBytes(
      GetPublicKeysetBytes(), key_index, CobaltEncryptionKey::ANALYZER);
  auto encrypted_message_maker_or_status =
      EncryptedMessageMaker::MakeForObservations(key_bytes);
  EXPECT_TRUE(encrypted_message_maker_or_status.ok());
  auto maker = std::move(encrypted_message_maker_or_status.ValueOrDie());

  // Built an observation.
  std::string obs_id = "obs_id";
  Observation2 observation;
  observation.set_random_id(obs_id);

  // Encrypt the observation.
  EncryptedMessage encrypted_message;
  EXPECT_TRUE(maker->Encrypt(observation, &encrypted_message));

  EXPECT_TRUE(encrypted_message.public_key_fingerprint().empty());
  EXPECT_EQ(EncryptedMessage::NONE, encrypted_message.scheme());
  EXPECT_EQ(key_index, encrypted_message.key_index());

  // Decrypt the observation.
  std::string decrypted =
      Decrypt(encrypted_message.ciphertext(), kAnalyzerContextInfo);
  observation.Clear();

  // Check that the observation was correctly round-tripped.
  EXPECT_TRUE(observation.random_id().empty());
  EXPECT_TRUE(observation.ParseFromString(decrypted));
  EXPECT_EQ(obs_id, observation.random_id());
}

// Try to roundtrip an envelope through a message encrypter.
TEST_F(EncryptedMessageMakerTest, EncryptEnvelope) {
  uint32_t key_index = 1;
  auto key_bytes = MakeCobaltEncryptionKeyBytes(
      GetPublicKeysetBytes(), key_index, CobaltEncryptionKey::SHUFFLER);
  auto encrypted_message_maker_or_status =
      EncryptedMessageMaker::MakeForEnvelopes(key_bytes);
  EXPECT_TRUE(encrypted_message_maker_or_status.ok());
  auto maker = std::move(encrypted_message_maker_or_status.ValueOrDie());

  // Build an envelope proto.
  uint32_t metric_id = 25;
  Envelope envelope;
  envelope.add_batch()->mutable_meta_data()->set_metric_id(metric_id);

  // Encrypt the envelope.
  EncryptedMessage encrypted_message;
  EXPECT_TRUE(maker->Encrypt(envelope, &encrypted_message));
  EXPECT_TRUE(encrypted_message.public_key_fingerprint().empty());
  EXPECT_EQ(EncryptedMessage::NONE, encrypted_message.scheme());
  EXPECT_EQ(key_index, encrypted_message.key_index());

  // Decrypt the envelope.
  std::string decrypted =
      Decrypt(encrypted_message.ciphertext(), kShufflerContextInfo);
  envelope.Clear();
  EXPECT_EQ(0, envelope.batch_size());
  EXPECT_TRUE(envelope.ParseFromString(decrypted));

  // Check to see that the envelope rountripped correctly.
  EXPECT_EQ(1, envelope.batch_size());
  EXPECT_EQ(metric_id, envelope.batch(0).meta_data().metric_id());
}

// Expect an error if the key_index field is set to 0 or unset.
TEST_F(EncryptedMessageMakerTest, ZeroKeyIndex) {
  auto key_bytes = MakeCobaltEncryptionKeyBytes(GetPublicKeysetBytes(), 0,
                                                CobaltEncryptionKey::SHUFFLER);
  auto result = EncryptedMessageMaker::MakeForEnvelopes(key_bytes);
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());

  key_bytes = MakeCobaltEncryptionKeyBytes(GetPublicKeysetBytes(), 0,
                                           CobaltEncryptionKey::ANALYZER);
  result = EncryptedMessageMaker::MakeForObservations(key_bytes);
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());
}

// Expect an error if the purpose field is unset.
TEST_F(EncryptedMessageMakerTest, PurposeUnset) {
  auto key_bytes = MakeCobaltEncryptionKeyBytes(GetPublicKeysetBytes(), 1,
                                                CobaltEncryptionKey::UNSET);
  auto result = EncryptedMessageMaker::MakeForEnvelopes(key_bytes);
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());

  key_bytes = MakeCobaltEncryptionKeyBytes(GetPublicKeysetBytes(), 1,
                                           CobaltEncryptionKey::UNSET);
  result = EncryptedMessageMaker::MakeForObservations(key_bytes);
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());
}

// Expect an error if trying to use an analyzer key to encrypt envelopes or
// trying to use a shuffler key to encrypt observations.
TEST_F(EncryptedMessageMakerTest, WrongPurpose) {
  auto key_bytes = MakeCobaltEncryptionKeyBytes(GetPublicKeysetBytes(), 1,
                                                CobaltEncryptionKey::ANALYZER);
  auto result = EncryptedMessageMaker::MakeForEnvelopes(key_bytes);
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());

  key_bytes = MakeCobaltEncryptionKeyBytes(GetPublicKeysetBytes(), 1,
                                           CobaltEncryptionKey::SHUFFLER);
  result = EncryptedMessageMaker::MakeForObservations(key_bytes);
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());
}

// Expect an error if the string purported to be a serialized
// CobaltEncryptionKey is actually not.
TEST_F(EncryptedMessageMakerTest, NotASerializedCobaltEncryptionKey) {
  auto result = EncryptedMessageMaker::MakeForEnvelopes("hello");
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());

  result = EncryptedMessageMaker::MakeForObservations("hello");
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());
}

// Expect an error if the serialized key is invalid.
TEST_F(EncryptedMessageMakerTest, NotRealSerializedKey) {
  auto key_bytes = MakeCobaltEncryptionKeyBytes("not key bytes", 1,
                                                CobaltEncryptionKey::SHUFFLER);
  auto result = EncryptedMessageMaker::MakeForEnvelopes(key_bytes);
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());

  key_bytes = MakeCobaltEncryptionKeyBytes("not key bytes", 1,
                                           CobaltEncryptionKey::ANALYZER);
  result = EncryptedMessageMaker::MakeForObservations(key_bytes);
  EXPECT_EQ(INVALID_ARGUMENT, result.status().error_code());
}
}  // namespace util
}  // namespace cobalt

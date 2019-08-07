// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains two classes for working with EncryptedMessages.
//
// EncryptedMessageMaker is used by the Encoder to create EncryptedMessages
// by encrypting Observations, and Envelopes.
//
// MessageDecrypter is used by the Analyzer to decrypt EncryptedMessages
// containing Observations.

#ifndef COBALT_SRC_LIB_UTIL_ENCRYPTED_MESSAGE_UTIL_H_
#define COBALT_SRC_LIB_UTIL_ENCRYPTED_MESSAGE_UTIL_H_

#include <memory>
#include <string>
#include <vector>

#include "google/protobuf/message_lite.h"
#include "src/lib/crypto_util/cipher.h"
#include "src/pb/encrypted_message.pb.h"
#include "third_party/statusor/statusor.h"
#include "third_party/tink/cc/hybrid_encrypt.h"

namespace cobalt {
namespace util {

// EncryptedMessageMaker is used by the Encoder to encrypt protocol buffer
// messages before sending them to the Shuffler (and then to the Analyzer).
//
// The Encoder should make two instances of this class:
// one constructed with the public key of the Analyzer used for encrypting
// Observations and one with the public key of the Shuffler used for
// encrypting Envelopes.
class EncryptedMessageMaker {
 public:
  // Encrypts a protocol buffer |message| and populates |encrypted_message|
  // with the result. Returns true for success or false on failure.
  virtual bool Encrypt(const google::protobuf::MessageLite& message,
                       EncryptedMessage* encrypted_message) const = 0;

  // Returns the EncryptionScheme used by the EncryptedMessageMaker.
  [[nodiscard]] virtual EncryptedMessage::EncryptionScheme scheme() const = 0;

  // Make an UnencryptedMessageMaker.
  // Message will be serialized, but not encrypted: they will be sent in plain
  // text. This scheme must never be used in production Cobalt.
  static std::unique_ptr<EncryptedMessageMaker> MakeUnencrypted();

  // Make an EncryptedMessageMaker to encrypt Envelopes.
  // Messages will be encrypted using the scheme corresponding to the key
  // that is passed in. |cobalt_encryption_key_bytes| is a serialized
  // cobalt::CobaltEncryptionKey protobuf message.
  static statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>> MakeForEnvelopes(
      const std::string& cobalt_encryption_key_bytes);

  // Make an EncryptedMessageMaker to encrypt Observations.
  // Messages will be encrypted using the scheme corresponding to the key
  // that is passed in. |cobalt_encryption_key_bytes| is a serialized
  // cobalt::CobaltEncryptionKey protobuf message.
  static statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>> MakeForObservations(
      const std::string& cobalt_encryption_key_bytes);

  virtual ~EncryptedMessageMaker() = default;
};

// HybridTinkEncryptedMessageMaker is an implementation of
// EncryptedMessageMaker. See EncryptedMessage::HYBRID_TINK for details.
class HybridTinkEncryptedMessageMaker : public EncryptedMessageMaker {
 public:
  HybridTinkEncryptedMessageMaker(std::unique_ptr<::crypto::tink::HybridEncrypt> encrypter,
                                  std::string context_info, uint32_t key_index);

  bool Encrypt(const google::protobuf::MessageLite& message,
               EncryptedMessage* encrypted_message) const override;

  [[nodiscard]] EncryptedMessage::EncryptionScheme scheme() const override {
    return EncryptedMessage::HYBRID_TINK;
  }

 private:
  std::unique_ptr<::crypto::tink::HybridEncrypt> encrypter_;
  std::string context_info_;
  uint32_t key_index_;
};

// UnencryptedMessageMaker is an implementation of EncryptedMessageMaker that
// does not perform any encryption.
class UnencryptedMessageMaker : public EncryptedMessageMaker {
 public:
  bool Encrypt(const google::protobuf::MessageLite& message,
               EncryptedMessage* encrypted_message) const override;

  [[nodiscard]] EncryptedMessage::EncryptionScheme scheme() const override {
    return EncryptedMessage::NONE;
  }
};

class MessageDecrypter {
 public:
  // TODO(rudominer) For key-rotation support the MessageDecrypter
  // should accept multiple (public, private) key pairs and use the
  // fingerprint field of EncryptedMessage to select the appropriate private
  // key.
  explicit MessageDecrypter(const std::string& private_key_pem);

  bool DecryptMessage(const EncryptedMessage& encrypted_message,
                      google::protobuf::MessageLite* recovered_message) const;

 private:
  std::unique_ptr<crypto::HybridCipher> cipher_;
};

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_SRC_LIB_UTIL_ENCRYPTED_MESSAGE_UTIL_H_

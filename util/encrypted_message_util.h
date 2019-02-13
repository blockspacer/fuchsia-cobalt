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

// This file contains two classes for working with EncryptedMessages.
//
// EncryptedMessageMaker is used by the Encoder to create EncryptedMessages
// by encrypting Observations, and Envelopes.
//
// MessageDecrypter is used by the Analyzer to decrypt EncryptedMessages
// containing Observations.

#ifndef COBALT_UTIL_ENCRYPTED_MESSAGE_UTIL_H_
#define COBALT_UTIL_ENCRYPTED_MESSAGE_UTIL_H_

#include <memory>
#include <string>
#include <vector>

#include "./encrypted_message.pb.h"
#include "google/protobuf/message_lite.h"
#include "third_party/statusor/statusor.h"
#include "util/crypto_util/cipher.h"

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
  bool Encrypt(const google::protobuf::MessageLite& message,
               EncryptedMessage* encrypted_message) const;

  // Make an EncryptedMessageMaker.
  //
  // |scheme| specifies which encryption scheme should be used.
  // The use of EncryptedMessage::NONE is disallowed.
  //
  // |public_key_pem| must be appropriate to |scheme|. If |scheme| is
  // EncryptedMessage::HYBRID_ECDH_V1 then |public_key_pem| must be a PEM
  // encoding of a public key appropriate for that scheme.
  static statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
  Make(const std::string& public_key_pem,
       EncryptedMessage::EncryptionScheme scheme);

  // Make an EncryptedMessageMaker. (Do not use in production)
  //
  // |scheme| specifies which encryption scheme should be used. As of this
  // writing there are two schemes:
  //   (i) EncryptedMessage::NONE means that messages will not be
  //   encrypted: they will be sent in plain text. This scheme must
  //   never be used in production Cobalt.
  //
  //   (ii) EncryptedMessage::HYBRID_ECDH_V1 indicates that version 1 of
  //   Cobalt's Elliptic-Curve Diffie-Hellman-based hybrid
  //   public-key/private-key encryption scheme should be used.
  //
  // |public_key_pem| must be appropriate to |scheme|. If |scheme| is
  // EncryptedMessage::NONE then |public_key_pem| is ignored. If |scheme| is
  // EncryptedMessage::HYBRID_ECDH_V1 then |public_key_pem| must be a PEM
  // encoding of a public key appropriate for that scheme.
  static statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
  MakeAllowUnencrypted(const std::string& public_key_pem,
                       EncryptedMessage::EncryptionScheme scheme);

  // DEPRECATED. Use Make instead.
  EncryptedMessageMaker(const std::string& public_key_pem,
                        EncryptedMessage::EncryptionScheme scheme);

 private:
  std::unique_ptr<crypto::HybridCipher> cipher_;
  EncryptedMessage::EncryptionScheme encryption_scheme_;
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

#endif  // COBALT_UTIL_ENCRYPTED_MESSAGE_UTIL_H_

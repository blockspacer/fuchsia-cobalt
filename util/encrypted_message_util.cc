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

#include <vector>

#include "./encrypted_message.pb.h"
#include "./logging.h"
#include "google/protobuf/message_lite.h"
#include "util/crypto_util/cipher.h"
#include "util/status.h"
#include "util/status_codes.h"

namespace cobalt {
namespace util {

using ::cobalt::crypto::byte;
using ::cobalt::crypto::HybridCipher;

EncryptedMessageMaker::EncryptedMessageMaker(
    const std::string& public_key_pem,
    EncryptedMessage::EncryptionScheme scheme)
    : cipher_(new HybridCipher()), encryption_scheme_(scheme) {
  if (!cipher_->set_public_key_pem(public_key_pem)) {
    cipher_.reset();
    return;
  }
  if (encryption_scheme_ == EncryptedMessage::NONE) {
    VLOG(5) << "WARNING: encryption_scheme is NONE. Cobalt data will not be "
               "encrypted!";
  }
}

tensorflow_statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
EncryptedMessageMaker::Make(const std::string& public_key_pem,
                            EncryptedMessage::EncryptionScheme scheme) {
  if (scheme == EncryptedMessage::NONE) {
    return Status(INVALID_ARGUMENT,
                  "EncryptedMessageMaker: encryption_scheme NONE is not "
                  "allowed in production.");
  }
  return std::make_unique<EncryptedMessageMaker>(public_key_pem, scheme);
}

tensorflow_statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
EncryptedMessageMaker::MakeAllowUnencrypted(
    const std::string& public_key_pem,
    EncryptedMessage::EncryptionScheme scheme) {
  return std::make_unique<EncryptedMessageMaker>(public_key_pem, scheme);
}

bool EncryptedMessageMaker::Encrypt(
    const google::protobuf::MessageLite& message,
    EncryptedMessage* encrypted_message) const {
  if (!encrypted_message) {
    return false;
  }

  std::string serialized_message;
  message.SerializeToString(&serialized_message);

  if (encryption_scheme_ == EncryptedMessage::NONE) {
    encrypted_message->set_scheme(EncryptedMessage::NONE);
    encrypted_message->set_ciphertext(serialized_message);
    VLOG(5) << "EncryptedMessage: encryption_scheme=NONE.";
    return true;
  }

  if (encryption_scheme_ != EncryptedMessage::HYBRID_ECDH_V1) {
    // HYBRID_ECDH_V1 is the only other scheme we know about.
    VLOG(5) << "EncryptedMessage: encryption_scheme != HYBRID_ECDH_V1.";
    return false;
  }

  VLOG(5) << "EncryptedMessage: encryption_scheme=HYBRID_ECDH_V1.";

  if (!cipher_) {
    return false;
  }

  std::vector<byte> ciphertext;
  if (!cipher_->Encrypt((const byte*)serialized_message.data(),
                        serialized_message.size(), &ciphertext)) {
    return false;
  }
  encrypted_message->set_allocated_ciphertext(
      new std::string((const char*)ciphertext.data(), ciphertext.size()));
  encrypted_message->set_scheme(EncryptedMessage::HYBRID_ECDH_V1);
  byte fingerprint[HybridCipher::PUBLIC_KEY_FINGERPRINT_SIZE];
  if (!cipher_->public_key_fingerprint(fingerprint)) {
    return false;
  }
  encrypted_message->set_allocated_public_key_fingerprint(
      new std::string((const char*)fingerprint, sizeof(fingerprint)));
  return true;
}

MessageDecrypter::MessageDecrypter(const std::string& private_key_pem)
    : cipher_(new HybridCipher()) {
  if (!cipher_->set_private_key_pem(private_key_pem)) {
    cipher_.reset();
    return;
  }
}

bool MessageDecrypter::DecryptMessage(
    const EncryptedMessage& encrypted_message,
    google::protobuf::MessageLite* recovered_message) const {
  if (!recovered_message) {
    return false;
  }

  if (encrypted_message.scheme() == EncryptedMessage::NONE) {
    if (!recovered_message->ParseFromString(encrypted_message.ciphertext())) {
      return false;
    }
    VLOG(5) << "WARNING: Deserialized unencrypted message!";
    return true;
  }

  if (encrypted_message.scheme() != EncryptedMessage::HYBRID_ECDH_V1) {
    // HYBRID_ECDH_V1 is the only other scheme we know about.
    return false;
  }

  if (!cipher_) {
    return false;
  }

  std::vector<byte> ptext;
  if (!cipher_->Decrypt((const byte*)encrypted_message.ciphertext().data(),
                        encrypted_message.ciphertext().size(), &ptext)) {
    return false;
  }
  std::string serialized_observation((const char*)ptext.data(), ptext.size());
  if (!recovered_message->ParseFromString(serialized_observation)) {
    return false;
  }
  VLOG(5) << "Successfully decrypted message.";
  return true;
}

}  // namespace util
}  // namespace cobalt

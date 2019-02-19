// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/encrypted_message_util.h"

#include <utility>
#include <vector>

#include "./encrypted_message.pb.h"
#include "./logging.h"
#include "google/protobuf/message_lite.h"
#include "third_party/tink/cc/hybrid/hybrid_config.h"
#include "third_party/tink/cc/hybrid_encrypt.h"
#include "third_party/tink/cc/keyset_handle.h"
#include "util/crypto_util/base64.h"
#include "util/crypto_util/cipher.h"
#include "util/status.h"
#include "util/status_codes.h"

namespace cobalt {
namespace util {

using ::cobalt::crypto::byte;
using ::cobalt::crypto::HybridCipher;

namespace {
Status StatusFromTinkStatus(::crypto::tink::util::Status tink_status) {
  return Status(StatusCode(tink_status.error_code()),
                tink_status.error_message());
}
}  // namespace

statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
EncryptedMessageMaker::Make(const std::string& public_key,
                            EncryptedMessage::EncryptionScheme scheme) {
  switch (scheme) {
    case EncryptedMessage::NONE:
      return Status(INVALID_ARGUMENT,
                    "EncryptedMessageMaker: encryption_scheme NONE is not "
                    "allowed in production.");
    case EncryptedMessage::HYBRID_ECDH_V1:
      return EncryptedMessageMaker::MakeHybridEcdh(public_key);
    case EncryptedMessage::HYBRID_TINK:
      return EncryptedMessageMaker::MakeHybridTink(public_key);
    default:
      return Status(INVALID_ARGUMENT,
                    "EncryptedMessageMaker: Unknown encryption_scheme.");
  }
}

statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
EncryptedMessageMaker::MakeAllowUnencrypted(
    const std::string& public_key, EncryptedMessage::EncryptionScheme scheme) {
  if (scheme == EncryptedMessage::NONE) {
    return EncryptedMessageMaker::MakeUnencrypted();
  }
  return EncryptedMessageMaker::Make(public_key, scheme);
}

std::unique_ptr<EncryptedMessageMaker>
EncryptedMessageMaker::MakeUnencrypted() {
  VLOG(5) << "WARNING: encryption_scheme is NONE. Cobalt data will not be "
             "encrypted!";
  return std::make_unique<UnencryptedMessageMaker>();
}

statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
EncryptedMessageMaker::MakeHybridTink(const std::string& public_keyset_base64) {
  if (public_keyset_base64.empty()) {
    return Status(INVALID_ARGUMENT, "EncryptedMessageMaker: Empty key.");
  }

  auto status = ::crypto::tink::HybridConfig::Register();
  if (!status.ok()) {
    return StatusFromTinkStatus(status);
  }

  std::string public_keyset;
  if (!crypto::Base64Decode(public_keyset_base64, &public_keyset)) {
    return Status(INVALID_ARGUMENT,
                  "EncryptedMessageMaker: Invalid base64-encoded keyset.");
  }

  auto read_result = ::crypto::tink::KeysetHandle::ReadNoSecret(public_keyset);
  if (!read_result.ok()) {
    return StatusFromTinkStatus(read_result.status());
  }
  auto keyset_handle = std::move(read_result.ValueOrDie());

  auto primitive_result =
      keyset_handle->GetPrimitive<::crypto::tink::HybridEncrypt>();
  if (!primitive_result.ok()) {
    return StatusFromTinkStatus(primitive_result.status());
  }

  std::unique_ptr<EncryptedMessageMaker> maker(
      new HybridTinkEncryptedMessageMaker(
          std::move(primitive_result.ValueOrDie())));

  return maker;
}

statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
EncryptedMessageMaker::MakeHybridEcdh(const std::string& public_key_pem) {
  auto cipher = std::make_unique<HybridCipher>();
  if (!cipher->set_public_key_pem(public_key_pem)) {
    // TODO(azani): Return an error here.
    cipher.reset();
  }
  std::unique_ptr<EncryptedMessageMaker> maker(
      new HybridEcdhEncryptedMessageMaker(std::move(cipher)));
  return maker;
}

HybridTinkEncryptedMessageMaker::HybridTinkEncryptedMessageMaker(
    std::unique_ptr<::crypto::tink::HybridEncrypt> encrypter)
    : encrypter_(std::move(encrypter)) {}

bool HybridTinkEncryptedMessageMaker::Encrypt(
    const google::protobuf::MessageLite& message,
    EncryptedMessage* encrypted_message) const {
  if (!encrypted_message) {
    return false;
  }

  std::string serialized_message;
  message.SerializeToString(&serialized_message);

  VLOG(5) << "EncryptedMessage: encryption_scheme=HYBRID_TINK.";

  auto encrypted_result = encrypter_->Encrypt(serialized_message, "");
  if (!encrypted_result.ok()) {
    VLOG(5) << "EncryptedMessage: Tink could not encrypt message: "
            << encrypted_result.status().error_message();
    return false;
  }
  encrypted_message->set_ciphertext(encrypted_result.ValueOrDie());
  encrypted_message->set_scheme(EncryptedMessage::HYBRID_TINK);

  return true;
}

HybridEcdhEncryptedMessageMaker::HybridEcdhEncryptedMessageMaker(
    std::unique_ptr<HybridCipher> cipher)
    : cipher_(std::move(cipher)) {}

bool HybridEcdhEncryptedMessageMaker::Encrypt(
    const google::protobuf::MessageLite& message,
    EncryptedMessage* encrypted_message) const {
  if (!encrypted_message) {
    return false;
  }

  std::string serialized_message;
  message.SerializeToString(&serialized_message);

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

bool UnencryptedMessageMaker::Encrypt(
    const google::protobuf::MessageLite& message,
    EncryptedMessage* encrypted_message) const {
  if (!encrypted_message) {
    return false;
  }
  std::string serialized_message;
  message.SerializeToString(&serialized_message);
  encrypted_message->set_scheme(EncryptedMessage::NONE);
  encrypted_message->set_ciphertext(serialized_message);
  VLOG(5) << "EncryptedMessage: encryption_scheme=NONE.";
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

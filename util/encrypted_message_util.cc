// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/encrypted_message_util.h"

#include <utility>
#include <vector>

#include "./encrypted_message.pb.h"
#include "./key.pb.h"
#include "./logging.h"
#include "./tracing.h"
#include "google/protobuf/message_lite.h"
#include "third_party/tink/cc/hybrid/hybrid_config.h"
#include "third_party/tink/cc/hybrid_encrypt.h"
#include "third_party/tink/cc/keyset_handle.h"
#include "util/crypto_util/cipher.h"
#include "util/status.h"
#include "util/status_codes.h"

namespace cobalt {
namespace util {

using ::cobalt::crypto::byte;
using ::cobalt::crypto::HybridCipher;

namespace {
const char kShufflerContextInfo[] = "cobalt-1.0-shuffler";
const char kAnalyzerContextInfo[] = "cobalt-1.0-analyzer";

Status StatusFromTinkStatus(::crypto::tink::util::Status tink_status) {
  return Status(StatusCode(tink_status.error_code()),
                tink_status.error_message());
}

// Make a HybridTinkEncryptedMessageMaker from a serialized encoded keyset.
statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
MakeHybridTinkEncryptedMessageMaker(const std::string& public_keyset_bytes,
                                    const std::string& context_info,
                                    uint32_t key_index) {
  auto status = ::crypto::tink::HybridConfig::Register();
  if (!status.ok()) {
    return StatusFromTinkStatus(status);
  }

  auto read_result =
      ::crypto::tink::KeysetHandle::ReadNoSecret(public_keyset_bytes);
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
          std::move(primitive_result.ValueOrDie()), context_info, key_index));

  return maker;
}

// Parse and validate a serialized CobaltEncryptionKey.
statusor::StatusOr<cobalt::CobaltEncryptionKey> ParseCobaltEncryptionKey(
    const std::string& cobalt_encryption_key_bytes) {
  cobalt::CobaltEncryptionKey cobalt_encryption_key;
  if (!cobalt_encryption_key.ParseFromString(cobalt_encryption_key_bytes)) {
    return Status(INVALID_ARGUMENT,
                  "EncryptedMessageMaker: Invalid Cobalt encryption key.");
  }

  if (cobalt_encryption_key.key_index() == 0) {
    return Status(INVALID_ARGUMENT,
                  "EncryptedMessageMaker: Invalid Cobalt key_index: 0.");
  }

  if (cobalt_encryption_key.purpose() !=
          cobalt::CobaltEncryptionKey::SHUFFLER &&
      cobalt_encryption_key.purpose() !=
          cobalt::CobaltEncryptionKey::ANALYZER) {
    return Status(INVALID_ARGUMENT,
                  "EncryptedMessageMaker: Invalid Cobalt key purpose.");
  }

  return cobalt_encryption_key;
}
}  // namespace

std::unique_ptr<EncryptedMessageMaker>
EncryptedMessageMaker::MakeUnencrypted() {
  VLOG(5) << "WARNING: encryption_scheme is NONE. Cobalt data will not be "
             "encrypted!";
  return std::make_unique<UnencryptedMessageMaker>();
}

statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
EncryptedMessageMaker::MakeForEnvelopes(
    const std::string& cobalt_encryption_key_bytes) {
  auto cobalt_encryption_key_or_result =
      ParseCobaltEncryptionKey(cobalt_encryption_key_bytes);
  if (!cobalt_encryption_key_or_result.ok()) {
    return cobalt_encryption_key_or_result.status();
  }
  auto cobalt_encryption_key = cobalt_encryption_key_or_result.ValueOrDie();

  if (cobalt_encryption_key.purpose() !=
      cobalt::CobaltEncryptionKey::SHUFFLER) {
    return Status(INVALID_ARGUMENT,
                  "EncryptedMessageMaker: Expected a shuffler key.");
  }

  return MakeHybridTinkEncryptedMessageMaker(
      cobalt_encryption_key.serialized_key(), kShufflerContextInfo,
      cobalt_encryption_key.key_index());
}

statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
EncryptedMessageMaker::MakeForObservations(
    const std::string& cobalt_encryption_key_bytes) {
  auto cobalt_encryption_key_or_result =
      ParseCobaltEncryptionKey(cobalt_encryption_key_bytes);
  if (!cobalt_encryption_key_or_result.ok()) {
    return cobalt_encryption_key_or_result.status();
  }
  auto cobalt_encryption_key = cobalt_encryption_key_or_result.ValueOrDie();

  if (cobalt_encryption_key.purpose() !=
      cobalt::CobaltEncryptionKey::ANALYZER) {
    return Status(INVALID_ARGUMENT,
                  "EncryptedMessageMaker: Expected an analyzer key.");
  }

  return MakeHybridTinkEncryptedMessageMaker(
      cobalt_encryption_key.serialized_key(), kAnalyzerContextInfo,
      cobalt_encryption_key.key_index());
}

HybridTinkEncryptedMessageMaker::HybridTinkEncryptedMessageMaker(
    std::unique_ptr<::crypto::tink::HybridEncrypt> encrypter,
    const std::string& context_info, uint32_t key_index)
    : encrypter_(std::move(encrypter)),
      context_info_(context_info),
      key_index_(key_index) {}

bool HybridTinkEncryptedMessageMaker::Encrypt(
    const google::protobuf::MessageLite& message,
    EncryptedMessage* encrypted_message) const {
  TRACE_DURATION("cobalt_core", "HybridTinkEncryptedMessageMaker::Encrypt");
  if (!encrypted_message) {
    return false;
  }

  std::string serialized_message;
  message.SerializeToString(&serialized_message);

  VLOG(5) << "EncryptedMessage: encryption_scheme=HYBRID_TINK.";

  auto encrypted_result =
      encrypter_->Encrypt(serialized_message, context_info_);
  if (!encrypted_result.ok()) {
    VLOG(5) << "EncryptedMessage: Tink could not encrypt message: "
            << encrypted_result.status().error_message();
    return false;
  }
  encrypted_message->set_ciphertext(encrypted_result.ValueOrDie());
  if (key_index_ == 0) {
    encrypted_message->set_scheme(EncryptedMessage::HYBRID_TINK);
  } else {
    encrypted_message->set_key_index(key_index_);
  }

  return true;
}

bool UnencryptedMessageMaker::Encrypt(
    const google::protobuf::MessageLite& message,
    EncryptedMessage* encrypted_message) const {
  TRACE_DURATION("cobalt_core", "UnencryptedMessageMaker::Encrypt");
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

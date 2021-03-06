// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/util/encrypted_message_util.h"

#include <utility>
#include <vector>

#include "google/protobuf/message_lite.h"
#include "src/lib/util/status.h"
#include "src/lib/util/status_codes.h"
#include "src/logging.h"
#include "src/pb/encrypted_message.pb.h"
#include "src/pb/key.pb.h"
#include "src/tracing.h"
#include "third_party/tink/cc/hybrid/hybrid_config.h"
#include "third_party/tink/cc/hybrid_encrypt.h"
#include "third_party/tink/cc/keyset_handle.h"

namespace cobalt::util {

namespace {
constexpr char kShufflerContextInfo[] = "cobalt-1.0-shuffler";
constexpr char kAnalyzerContextInfo[] = "cobalt-1.0-analyzer";

Status StatusFromTinkStatus(const ::crypto::tink::util::Status& tink_status) {
  return Status(StatusCode(tink_status.error_code()), tink_status.error_message());
}

// Make a HybridTinkEncryptedMessageMaker from a serialized encoded keyset.
lib::statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>> MakeHybridTinkEncryptedMessageMaker(
    const std::string& public_keyset_bytes, const std::string& context_info, uint32_t key_index) {
  auto status = ::crypto::tink::HybridConfig::Register();
  if (!status.ok()) {
    return StatusFromTinkStatus(status);
  }

  auto read_result = ::crypto::tink::KeysetHandle::ReadNoSecret(public_keyset_bytes);
  if (!read_result.ok()) {
    return StatusFromTinkStatus(read_result.status());
  }
  auto keyset_handle = std::move(read_result.ValueOrDie());

  auto primitive_result = keyset_handle->GetPrimitive<::crypto::tink::HybridEncrypt>();
  if (!primitive_result.ok()) {
    return StatusFromTinkStatus(primitive_result.status());
  }

  std::unique_ptr<EncryptedMessageMaker> maker(new HybridTinkEncryptedMessageMaker(
      std::move(primitive_result.ValueOrDie()), context_info, key_index));

  return maker;
}

// Parse and validate a serialized CobaltEncryptionKey.
lib::statusor::StatusOr<cobalt::CobaltEncryptionKey> ParseCobaltEncryptionKey(
    const std::string& cobalt_encryption_key_bytes) {
  cobalt::CobaltEncryptionKey cobalt_encryption_key;
  if (!cobalt_encryption_key.ParseFromString(cobalt_encryption_key_bytes)) {
    return Status(INVALID_ARGUMENT, "EncryptedMessageMaker: Invalid Cobalt encryption key.");
  }

  if (cobalt_encryption_key.key_index() == 0) {
    return Status(INVALID_ARGUMENT, "EncryptedMessageMaker: Invalid Cobalt key_index: 0.");
  }

  if (cobalt_encryption_key.purpose() != cobalt::CobaltEncryptionKey::SHUFFLER &&
      cobalt_encryption_key.purpose() != cobalt::CobaltEncryptionKey::ANALYZER) {
    return Status(INVALID_ARGUMENT, "EncryptedMessageMaker: Invalid Cobalt key purpose.");
  }

  return cobalt_encryption_key;
}
}  // namespace

std::unique_ptr<EncryptedMessageMaker> EncryptedMessageMaker::MakeUnencrypted() {
  VLOG(5) << "WARNING: encryption_scheme is NONE. Cobalt data will not be "
             "encrypted!";
  return std::make_unique<UnencryptedMessageMaker>();
}

lib::statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
EncryptedMessageMaker::MakeForEnvelopes(const std::string& cobalt_encryption_key_bytes) {
  auto cobalt_encryption_key_or_result = ParseCobaltEncryptionKey(cobalt_encryption_key_bytes);
  if (!cobalt_encryption_key_or_result.ok()) {
    return cobalt_encryption_key_or_result.status();
  }
  auto cobalt_encryption_key = cobalt_encryption_key_or_result.ValueOrDie();

  if (cobalt_encryption_key.purpose() != cobalt::CobaltEncryptionKey::SHUFFLER) {
    return Status(INVALID_ARGUMENT, "EncryptedMessageMaker: Expected a shuffler key.");
  }

  return MakeHybridTinkEncryptedMessageMaker(cobalt_encryption_key.serialized_key(),
                                             kShufflerContextInfo,
                                             cobalt_encryption_key.key_index());
}

lib::statusor::StatusOr<std::unique_ptr<EncryptedMessageMaker>>
EncryptedMessageMaker::MakeForObservations(const std::string& cobalt_encryption_key_bytes) {
  auto cobalt_encryption_key_or_result = ParseCobaltEncryptionKey(cobalt_encryption_key_bytes);
  if (!cobalt_encryption_key_or_result.ok()) {
    return cobalt_encryption_key_or_result.status();
  }
  auto cobalt_encryption_key = cobalt_encryption_key_or_result.ValueOrDie();

  if (cobalt_encryption_key.purpose() != cobalt::CobaltEncryptionKey::ANALYZER) {
    return Status(INVALID_ARGUMENT, "EncryptedMessageMaker: Expected an analyzer key.");
  }

  return MakeHybridTinkEncryptedMessageMaker(cobalt_encryption_key.serialized_key(),
                                             kAnalyzerContextInfo,
                                             cobalt_encryption_key.key_index());
}

HybridTinkEncryptedMessageMaker::HybridTinkEncryptedMessageMaker(
    std::unique_ptr<::crypto::tink::HybridEncrypt> encrypter, std::string context_info,
    uint32_t key_index)
    : encrypter_(std::move(encrypter)),
      context_info_(std::move(context_info)),
      key_index_(key_index) {}

bool HybridTinkEncryptedMessageMaker::Encrypt(const google::protobuf::MessageLite& message,
                                              EncryptedMessage* encrypted_message) const {
  TRACE_DURATION("cobalt_core", "HybridTinkEncryptedMessageMaker::Encrypt");
  if (!encrypted_message) {
    return false;
  }

  std::string serialized_message;
  message.SerializeToString(&serialized_message);

  VLOG(5) << "EncryptedMessage: encryption_scheme=HYBRID_TINK.";

  auto encrypted_result = encrypter_->Encrypt(serialized_message, context_info_);
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

bool UnencryptedMessageMaker::Encrypt(const google::protobuf::MessageLite& message,
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

}  // namespace cobalt::util

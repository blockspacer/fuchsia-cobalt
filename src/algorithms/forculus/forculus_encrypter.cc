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

#include "src/algorithms/forculus/forculus_encrypter.h"

#include <cstring>
#include <vector>

#include "src/algorithms/forculus/field_element.h"
#include "src/algorithms/forculus/forculus_utils.h"
#include "src/algorithms/forculus/polynomial_computations.h"
#include "src/lib/crypto_util/cipher.h"
#include "src/lib/crypto_util/mac.h"

namespace cobalt::forculus {

using crypto::SymmetricCipher;
using crypto::hmac::HMAC;
using encoder::ClientSecret;

namespace {
// Derives a master key for use in Forculus encryption by applying a slow
// random oracle to the the input data. Returns the master key, or an empty
// vector if the operation fails for any reason.
std::vector<byte> DeriveMasterKey(uint32_t customer_id, uint32_t project_id, uint32_t metric_id,
                                  const std::string& metric_part_name, uint32_t epoch_index,
                                  uint32_t threshold, const std::string& plaintext) {
  // First we build up a byte vector consisting of the concatenation of all of
  // the input material. This will be the input to the random oracle.
  // We prepend each string with its length.
  size_t part_name_size = metric_part_name.size();
  size_t plaintext_size = plaintext.size();
  std::vector<byte> master_key_material(sizeof(customer_id) + sizeof(project_id) +
                                        sizeof(metric_id) + sizeof(part_name_size) +
                                        part_name_size + sizeof(epoch_index) + sizeof(threshold) +
                                        sizeof(plaintext_size) + plaintext.size());
  // Add customer_id
  std::memcpy(master_key_material.data(), &customer_id, sizeof(customer_id));
  size_t index = sizeof(customer_id);
  // Add project_id
  std::memcpy(master_key_material.data() + index, &project_id, sizeof(project_id));
  index += sizeof(project_id);
  // Add metric_id
  std::memcpy(master_key_material.data() + index, &metric_id, sizeof(metric_id));
  index += sizeof(metric_id);
  // Add part_name_size
  std::memcpy(master_key_material.data() + index, &part_name_size, sizeof(part_name_size));
  index += sizeof(part_name_size);
  // Add metric_part_name
  std::memcpy(master_key_material.data() + index, metric_part_name.data(), metric_part_name.size());
  index += metric_part_name.size();
  // Add epoch_index
  std::memcpy(master_key_material.data() + index, &epoch_index, sizeof(epoch_index));
  index += sizeof(epoch_index);
  // Add threshold
  std::memcpy(master_key_material.data() + index, &threshold, sizeof(threshold));
  index += sizeof(threshold);
  // Add plaintext_size
  std::memcpy(master_key_material.data() + index, &plaintext_size, sizeof(plaintext_size));
  index += sizeof(plaintext_size);
  // Add plaintext
  std::memcpy(master_key_material.data() + index, plaintext.data(), plaintext.size());

  // Now invoke the random oracle. We use HMAC_0 as our random oracle.
  // TODO(rudominer) Replace this with PBKDF2. HMAC_0 is not actually slow
  // and we promised to be slow.
  std::vector<byte> master_key(crypto::hmac::TAG_SIZE);
  const uint8_t kZeroKey = 0;
  if (!HMAC(&kZeroKey, sizeof(kZeroKey), master_key_material.data(), master_key_material.size(),
            master_key.data())) {
    master_key.resize(0);
  }
  return master_key;
}

constexpr uint32_t kMinThreshold = 2;
constexpr uint32_t kMaxThreshold = 1000000;

}  // namespace

class ForculusConfigValidator {
 public:
  ForculusConfigValidator(const ForculusConfig& config, const ClientSecret& client_secret)
      : threshold_(config.threshold), epoch_type_(config.epoch_type) {
    valid_ = false;
    if (!client_secret.valid()) {
      return;
    }
    if (threshold_ < kMinThreshold || threshold_ >= kMaxThreshold) {
      return;
    }
    valid_ = true;
  }

  const uint32_t& threshold() { return threshold_; }

  const EpochType& epoch_type() { return epoch_type_; }

  bool valid() { return valid_; }

 private:
  bool valid_;
  uint32_t threshold_;
  EpochType epoch_type_;
};

ForculusEncrypter::ForculusEncrypter(const ForculusConfig& config, uint32_t customer_id,
                                     uint32_t project_id, uint32_t metric_id,
                                     std::string metric_part_name, ClientSecret client_secret)
    : config_(new ForculusConfigValidator(config, client_secret)),
      customer_id_(customer_id),
      project_id_(project_id),
      metric_id_(metric_id),
      metric_part_name_(std::move(metric_part_name)),
      client_secret_(std::move(client_secret)) {}

ForculusEncrypter::~ForculusEncrypter() = default;

ForculusEncrypter::Status ForculusEncrypter::EncryptValue(const ValuePart& value,
                                                          uint32_t observation_day_index,
                                                          ForculusObservation* observation_out) {
  std::string serialized_value;
  value.SerializeToString(&serialized_value);
  return Encrypt(serialized_value, observation_day_index, observation_out);
}

ForculusEncrypter::Status ForculusEncrypter::Encrypt(const std::string& plaintext,
                                                     uint32_t observation_day_index,
                                                     ForculusObservation* observation_out) {
  if (!config_->valid()) {
    return kInvalidConfig;
  }

  // Compute the epoch_index from the day_index.
  uint32_t epoch_index = EpochIndexFromDayIndex(observation_day_index, config_->epoch_type());

  const uint32_t& threshold = config_->threshold();

  // We now derive the Forculus master key by invoking a random oracle on
  // all of the following data: customer_id, project_id, metric_id,
  // metric_part_name, epoch_index, threshold and plaintext.
  std::vector<byte> master_key = DeriveMasterKey(
      customer_id_, project_id_, metric_id_, metric_part_name_, epoch_index, threshold, plaintext);
  if (master_key.empty()) {
    return kEncryptionFailed;
  }

  // We now derive |threshold| elements in the Forculus field to be the
  // coefficients of a polynomial of degree |threshold| - 1. We do this by
  // invoking HMAC(i) with successive values of i = 0, 1, ...
  // and using the master key as the HMAC key.
  std::vector<FieldElement> coefficients;
  for (uint32_t i = 0; i < threshold; i++) {
    std::vector<byte> coefficient_bytes(crypto::hmac::TAG_SIZE);
    if (!HMAC(master_key.data(), master_key.size(), reinterpret_cast<const byte*>(&i), sizeof(i),
              coefficient_bytes.data())) {
      return kEncryptionFailed;
    }
    coefficients.emplace_back(std::move(coefficient_bytes));
  }

  // We use coefficients[0] as the symmetric key to perform deterministic
  // encryption of the plaintext.
  SymmetricCipher cipher;
  cipher.set_key(coefficients[0].KeyBytes());
  // We use a zero-nonce to achieve deterministic encryption.
  static const byte kZeroNonce[SymmetricCipher::NONCE_SIZE] = {0};
  std::vector<byte> ciphertext;
  if (!cipher.Encrypt(kZeroNonce, reinterpret_cast<const byte*>(plaintext.data()), plaintext.size(),
                      &ciphertext)) {
    return kEncryptionFailed;
  }

  // We derive a field element to be the x-value of a point on the polynomial.
  // The derivation depends on both the master_key and the client secret.
  // We use the master_key as the HMAC key and the client_secret as the
  // HMAC argument.
  std::vector<byte> element_bytes(crypto::hmac::TAG_SIZE);
  if (!HMAC(master_key.data(), master_key.size(), client_secret_.data(),
            ClientSecret::kNumSecretBytes, element_bytes.data())) {
    return kEncryptionFailed;
  }
  FieldElement point_x(std::move(element_bytes));

  // Evaluate the polynomial at point_x to yield point_y.
  FieldElement point_y = Evaluate(coefficients, point_x);

  // Build the return value.
  point_x.CopyBytesToString(observation_out->mutable_point_x());
  point_y.CopyBytesToString(observation_out->mutable_point_y());
  observation_out->mutable_ciphertext()->assign(reinterpret_cast<const char*>(ciphertext.data()),
                                                ciphertext.size());
  return kOK;
}

}  // namespace cobalt::forculus

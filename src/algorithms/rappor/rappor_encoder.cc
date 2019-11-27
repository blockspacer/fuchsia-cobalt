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

#include "src/algorithms/rappor/rappor_encoder.h"

#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "src/lib/crypto_util/random.h"
#include "src/logging.h"
#include "src/tracing.h"

namespace cobalt::rappor {

using crypto::byte;
using system_data::ClientSecret;

namespace {

// Returns a human-readable string representation of |value| appropriate
// for debug messages.
std::string DebugString(const ValuePart& value) {
  std::ostringstream stream;
  switch (value.data_case()) {
    case ValuePart::kStringValue:
      stream << "'" << value.string_value() << "'";
      break;
    case ValuePart::kIntValue:
      stream << value.int_value();
      break;
    case ValuePart::kIndexValue:
      stream << "index-" << value.index_value();
      break;
    case ValuePart::kBlobValue:
      stream << "[blob value]";
    default:
      stream << "unexpected value type";
  }
  return stream.str();
}

// Flips the bits in |data| using the given probabilities and the given RNG.
//
// p = prob_0_becomes_1
// q = prob_1_stays_1
Status FlipBits(float p, float q, crypto::Random* random, std::string* data) {
  if (p <= 0.0 && q >= 1.0) {
    return kOK;
  }

  TRACE_DURATION("cobalt_core", "FlipBits", "sz", data->size());
  auto p_mask = std::make_unique<byte[]>(data->size());
  auto q_mask = std::make_unique<byte[]>(data->size());
  if (!random->RandomBits(p, p_mask.get(), data->size()) ||
      !random->RandomBits(q, q_mask.get(), data->size())) {
    return kInvalidInput;
  }

  for (size_t i = 0; i < data->size(); i++) {
    data->at(i) = (p_mask[i] & ~data->at(i)) | (q_mask[i] & data->at(i));
  }

  return kOK;
}

constexpr uint32_t kBitsPerByte = 8;

}  // namespace

BasicRapporEncoder::BasicRapporEncoder(const BasicRapporConfig& config, ClientSecret client_secret)
    : config_(new RapporConfigValidator(config)),
      random_(new crypto::Random()),
      client_secret_(std::move(client_secret)) {}

Status BasicRapporEncoder::Encode(const ValuePart& value, BasicRapporObservation* observation_out) {
  TRACE_DURATION("cobalt_core", "BasicRapporEncoder::Encode");
  std::string data;
  auto status = InitializeObservationData(&data);
  if (status != kOK) {
    return status;
  }

  auto bit_index = config_->bit_index(value);
  if (bit_index == -1) {
    LOG(ERROR) << "BasicRapporEncoder::Encode(): The given value was not one of "
               << "the categories: " << DebugString(value);
    return kInvalidInput;
  }
  // Indexed from the right, i.e. the least-significant bit.
  uint32_t byte_index = bit_index / kBitsPerByte;
  uint32_t bit_in_byte_index = bit_index % kBitsPerByte;

  // Set the appropriate bit.
  data[data.size() - (byte_index + 1)] = 1 << bit_in_byte_index;

  // TODO(rudominer) Consider supporting prr in future versions of Cobalt.

  // Randomly flip some of the bits based on the probabilities p and q.
  status = FlipBits(config_->prob_0_becomes_1(), config_->prob_1_stays_1(), random_.get(), &data);
  if (status != kOK) {
    return status;
  }

  observation_out->set_data(data);
  return kOK;
}

Status BasicRapporEncoder::EncodeNullObservation(BasicRapporObservation* observation_out) {
  std::string data;
  auto status = InitializeObservationData(&data);
  if (status != kOK) {
    return status;
  }
  // Randomly flip some of the bits based on the probabilities p and q.
  status = FlipBits(config_->prob_0_becomes_1(), config_->prob_1_stays_1(), random_.get(), &data);
  if (status != kOK) {
    return status;
  }
  observation_out->set_data(data);
  return kOK;
}

// Initialize |data| to a string of all zero bytes.
// (The C++ Protocol Buffer API uses string to represent an array of bytes.)
Status BasicRapporEncoder::InitializeObservationData(std::string* data) {
  if (!config_->valid()) {
    return kInvalidConfig;
  }
  if (!client_secret_.valid()) {
    LOG(ERROR) << "client_secret is not valid";
    return kInvalidConfig;
  }
  uint32_t num_bits = config_->num_bits();
  uint32_t num_bytes = (num_bits + kBitsPerByte - 1) / kBitsPerByte;
  *data = std::string(num_bytes, static_cast<char>(0));
  return kOK;
}

}  // namespace cobalt::rappor

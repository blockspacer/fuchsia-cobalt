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

#include "algorithms/forculus/forculus_decrypter.h"

#include <functional>
#include <vector>

#include "algorithms/forculus/polynomial_computations.h"
#include "util/crypto_util/cipher.h"

namespace cobalt::forculus {

using crypto::SymmetricCipher;

ForculusDecrypter::ForculusDecrypter(uint32_t threshold, std::string ciphertext)
    : threshold_(threshold), num_seen_(0), ciphertext_(std::move(ciphertext)) {}

ForculusDecrypter::Status ForculusDecrypter::AddObservation(
    const ForculusObservation& obs) {
  if (obs.ciphertext() != ciphertext_) {
    return kWrongCiphertext;
  }
  // Keep a copy of y so we can check it its the same as a previously added
  // point.
  FieldElement y(obs.point_y());
  auto result = points_.insert(std::make_pair(FieldElement(obs.point_x()), y));
  auto key_value_pair = result.first;
  auto success = result.second;
  if (!success) {
    if (key_value_pair->second != y) {
      return kInconsistentPoints;
    }
  }
  num_seen_++;
  return kOK;
}

uint32_t ForculusDecrypter::size() { return points_.size(); }

ForculusDecrypter::Status ForculusDecrypter::Decrypt(
    std::string* plain_text_out) {
  if (size() < threshold_) {
    return kNotEnoughPoints;
  }

  // Put pointers to the first |threshold_| x and y values into vectors.
  std::vector<const FieldElement*> x_values(threshold_);
  std::vector<const FieldElement*> y_values(threshold_);
  uint32_t point_index = 0;
  for (const auto& point : points_) {
    if (point_index >= threshold_) {
      break;
    }
    x_values[point_index] = &point.first;
    y_values[point_index++] = &point.second;
  }

  // The decryption key we need is the constant term of the unique polynomial of
  // degree (threshold_  - 1) that passes through the points given by the
  // x_values and y_values. We can find this using interpolation.
  FieldElement c0 = InterpolateConstant(x_values, y_values);

  // Now we have the key, decrypt.
  SymmetricCipher cipher;
  cipher.set_key(c0.KeyBytes());
  std::vector<byte> recoverd_text;
  // Our encryption scheme uses a zero nonce. (Note that C++11 initializes
  // the entire array to 0 with this syntax.)
  static const byte kZeroNonce[SymmetricCipher::NONCE_SIZE] = {0};
  if (!cipher.Decrypt(kZeroNonce,
                      reinterpret_cast<const byte*>(ciphertext_.data()),
                      ciphertext_.size(), &recoverd_text)) {
    // TODO(pseudorandom, rudominer) One reason that decryption might fail
    // is a ballot suppression attack. An adversary may intentionally flood
    // us with bad (x, y) values in order to keep us from decrypting a
    // ciphertext. Because we use authenticated encryption, the result of
    // invalid (x, y) values will be failure to decrypt. One way to combat
    // this attack might be to try different sets of points of size
    // |threshold| iteratively until decryption succeeds.
    return kDecryptionFailed;
  }
  plain_text_out->assign(recoverd_text.begin(), recoverd_text.end());
  return kOK;
}

}  // namespace cobalt::forculus

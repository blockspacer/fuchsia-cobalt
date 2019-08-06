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

#ifndef COBALT_ALGORITHMS_FORCULUS_FORCULUS_DECRYPTER_H_
#define COBALT_ALGORITHMS_FORCULUS_FORCULUS_DECRYPTER_H_

#include <map>
#include <string>
#include <utility>

#include "./observation.pb.h"
#include "config/encodings.pb.h"
#include "src/algorithms/forculus/field_element.h"

namespace cobalt {
namespace forculus {

// Decrypts a set of Forculus observations with the same ciphertext, if
// the number of such distinct observations exceeds the threshold. This is
// intended for use on the Cobalt Analyzer.
//
// Construct a ForculusDecrypter with a |threshold| and |ciphertext|. The
// |threshold| must be the same value as was used to produce the ciphertext
// in the Encrypter. Then invoke AddObservation() multiple times to add
// Observations that have that same ciphertext and were encrypted with that
// threshold. (Note that the fact that the observations all have the same
// ciphertext implies that they were encrypted with the same threshold as
// each other and that they are associated with the same metric_id, the same
// metric part name, and the same epoch index.)
//
// After adding at least |threshold| distinct points invoke Decrypt().
//
// An instance of ForculusDecrypter is not thread-safe.
class ForculusDecrypter {
 public:
  enum Status {
    kOK = 0,

    // Returned from AddObservation() to indicate that the same x-value has
    // been submitted twice with two different y-values. This indicates that
    // the set of Observations is inconsistent and can no longer be used.
    kInconsistentPoints,

    // Indicates that fewer than the threshold number of distinct points have
    // been added via AddObservation() and therefore Decrypt() may not yet
    // be invoked.
    kNotEnoughPoints,

    // Returned from AddObservation() if the observation doesn't have the
    // same ciphertext as was passed to the constructor.
    kWrongCiphertext,

    // Indicates that decryption failed for an uknown reason. One possible
    // reason would be if the given observations were in fact encrypted with
    // a different threshold.
    kDecryptionFailed
  };

  ForculusDecrypter(uint32_t threshold, std::string ciphertext);

  // Adds an additional observation to the set of observations. If the
  // observation's (x, y)-value has already been added then it will increment
  // |num_seen| but not |size|.
  //
  // Returns kInconsistentPoints if the observation has the same x-value as
  // a previous observation but a different y-value. Returns kWrongCiphertext
  // if the observation has the wrong ciphertext.
  Status AddObservation(const ForculusObservation& obs);

  // Returns the number of distinct (x, y) values that have been successfully
  // added. The Decrypt() method may only be invoked after the size is at
  // least the |threshold| passed to the constructor.
  uint32_t size();

  // Returns the number of times that AddObservation() was invoked and returned
  // kOK. This value differs from the value returned by size() in that if
  // the same (x, y)-value is added twice it will incrument |num_seen| but
  // not |size|.
  uint32_t num_seen() { return num_seen_; }

  // Decrypts the |ciphertext| that was passed to the constructor and writes
  // the plain text to *plain_text_out. Returns kOk on success. If there are
  // not enough points to perform the decryption, returns kNotEnoughPoints.
  // Returns kDecryptionFailed if the decryption failed for any reason.
  Status Decrypt(std::string* plain_text_out);

  // Returns the ciphertext associated with this Decrypter.
  const std::string& ciphertext() { return ciphertext_; }

 private:
  uint32_t threshold_;
  uint32_t num_seen_;

  std::string ciphertext_;

  // A map from x-values to y-values.
  std::map<FieldElement, FieldElement> points_;
};

}  // namespace forculus
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_FORCULUS_FORCULUS_DECRYPTER_H_

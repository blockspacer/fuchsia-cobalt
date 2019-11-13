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

#ifndef COBALT_SRC_ALGORITHMS_FORCULUS_FORCULUS_ENCRYPTER_H_
#define COBALT_SRC_ALGORITHMS_FORCULUS_FORCULUS_ENCRYPTER_H_

#include <memory>
#include <string>
#include <utility>

#include "src/algorithms/forculus/forculus_config.h"
#include "src/pb/observation.pb.h"
#include "src/system_data/client_secret.h"

namespace cobalt::forculus {

class ForculusConfigValidator;

// Encrypts a string value using Forculus threshold encryption. This API
// is intended for use in the Cobalt Encoder.
class ForculusEncrypter {
 public:
  enum Status { kOK = 0, kInvalidConfig, kEncryptionFailed };

  // Constructs a ForculusEncrypter with the given |config| for the specified
  // metric part.
  //
  // The |client_secret| is the entropy used while deriving a point on
  // the Forculus polynomial.
  ForculusEncrypter(const ForculusConfig& config, uint32_t customer_id, uint32_t project_id,
                    uint32_t metric_id, std::string metric_part_name,
                    encoder::ClientSecret client_secret);

  ~ForculusEncrypter();

  // Encrypts |plaintext| using Forculus threshold encryption and writes the
  // output to |*observation_out|.
  //
  // Forculus encryption consists of the following steps:
  //
  // (1) Generate a polynomial f(x) over the Forculus field. The degree of
  // the polynomial is |threshold| - 1.
  //
  // (2) Use the constant term from f(x) as the key with which to encrypt
  // the plaintext and produce a ciphertext.
  //
  // (3) Generate a point x in the Forculus field and compute y = f(x)
  //
  // (4) Return the triple (ciphertext, x, y)
  //
  // |observation_day_index| is used to determine the observation epoch
  //
  // The generated polynomial and ciphertext are deteriministic functions of
  // the following data: The plaintext, the epoch, the metric_id and
  // metric_part_name and the threshold. They do not depend on client_secret
  // and so are produced the same way by different clients.
  //
  // The generated x- and y-values are a deterministic function of all of the
  // above plus the client_secret. They therfore will be different on different
  // clients.
  //
  // Returns kOk on success, kInvalidConfig if the |config| passed to the
  // constructor is not valid, or kEncryptionFailed if the encryption fails
  // for any reason.
  Status Encrypt(const std::string& plaintext, uint32_t observation_day_index,
                 ForculusObservation* observation_out);

  // Serializes |value| into a plaintext string using standard protocol buffer
  // serialization and then invokes Encrypt() on the serialized bytes.
  //
  // The Cobalt Encoder invokes this method rather than directly invoking
  // Encrypt() in order to uniformly handle values of different data types.
  Status EncryptValue(const ValuePart& value, uint32_t observation_day_index,
                      ForculusObservation* observation_out);

 private:
  std::unique_ptr<ForculusConfigValidator> config_;
  uint32_t customer_id_, project_id_, metric_id_;
  std::string metric_part_name_;
  encoder::ClientSecret client_secret_;
};

}  // namespace cobalt::forculus

#endif  // COBALT_SRC_ALGORITHMS_FORCULUS_FORCULUS_ENCRYPTER_H_

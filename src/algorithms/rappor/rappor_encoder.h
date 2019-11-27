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

#ifndef COBALT_SRC_ALGORITHMS_RAPPOR_RAPPOR_ENCODER_H_
#define COBALT_SRC_ALGORITHMS_RAPPOR_RAPPOR_ENCODER_H_

#include <memory>
#include <string>
#include <utility>

#include "src/algorithms/rappor/rappor_config_validator.h"
#include "src/lib/crypto_util/random.h"
#include "src/pb/observation.pb.h"
#include "src/system_data/client_secret.h"

namespace cobalt::rappor {

enum Status {
  kOK = 0,
  kInvalidConfig,
  kInvalidInput,
};

// Performs encoding for Basic RAPPOR, a.k.a Categorical RAPPOR. No cohorts
// are used and the list of all candidates must be pre-specified as part
// of the BasicRapporConfig.
// The |client_secret| is used to determine the PRR.
class BasicRapporEncoder {
 public:
  BasicRapporEncoder(const BasicRapporConfig& config, system_data::ClientSecret client_secret);
  ~BasicRapporEncoder() = default;

  // Encodes |value| using Basic RAPPOR encoding. |value| must be one
  // of the categories listed in the |categories| field of the |config|
  // that was passed to the constructor. Returns kOK on success, kInvalidConfig
  // if the |config| passed to the constructor is not valid, and kInvalidInput
  // if |value| is not one of the |categories|.
  Status Encode(const ValuePart& value, BasicRapporObservation* observation_out);

  // Applies Basic RAPPOR encoding to a sequence of 0-bits whose length is equal
  // to the number of categories in the config which was passed to the
  // constructor. Returns kOK on success and kInvalidConfig if the config that
  // was passed to the constructor is not valid.
  Status EncodeNullObservation(BasicRapporObservation* observation_out);

 private:
  friend class BasicRapporAnalyzerTest;
  friend class BasicRapporDeterministicTest;

  // Initializes |data| with a number of 0 bytes determined by the |num_bits|
  // field of |config_| and returns |kOK|. Returns |kInvalidConfig| if |config_|
  // or |client_secret_| is invalid.
  Status InitializeObservationData(std::string* data);

  // Allows Friend classess to set a special RNG for use in tests.
  void SetRandomForTesting(std::unique_ptr<crypto::Random> random) { random_ = std::move(random); }

  std::unique_ptr<RapporConfigValidator> config_;
  std::unique_ptr<crypto::Random> random_;
  system_data::ClientSecret client_secret_;
};

}  // namespace cobalt::rappor

#endif  // COBALT_SRC_ALGORITHMS_RAPPOR_RAPPOR_ENCODER_H_

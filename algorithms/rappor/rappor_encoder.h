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

#ifndef COBALT_ALGORITHMS_RAPPOR_RAPPOR_ENCODER_H_
#define COBALT_ALGORITHMS_RAPPOR_RAPPOR_ENCODER_H_

#include <memory>
#include <string>
#include <utility>

#include "./observation.pb.h"
#include "algorithms/rappor/rappor_config_validator.h"
#include "config/encodings.pb.h"
#include "encoder/client_secret.h"
#include "util/crypto_util/hash.h"
#include "util/crypto_util/random.h"

namespace cobalt::rappor {

enum Status {
  kOK = 0,
  kInvalidConfig,
  kInvalidInput,
};

// Performs String RAPPOR encoding.
class RapporEncoder {
 public:
  // Constructor.
  // The |client_secret| is used to determine the cohort and the PRR.
  RapporEncoder(const RapporConfig& config,
                encoder::ClientSecret client_secret);
  virtual ~RapporEncoder() = default;

  // Encodes |value| using RAPPOR encoding. Returns kOK on success, or
  // kInvalidConfig if the |config| passed to the constructor is not valid.
  Status Encode(const ValuePart& value, RapporObservation* observation_out);

  [[nodiscard]] uint32_t cohort() const { return cohort_num_; }

 private:
  friend class StringRapporEncoderTest;
  friend class RapporAnalyzer;

  // Allows Friend classess to set a special RNG for use in tests.
  void SetRandomForTesting(std::unique_ptr<crypto::Random> random) {
    random_ = std::move(random);
  }

  // Computes a hash of the given |serialized value| and |cohort_num| and writes
  // the result to |hashed_value|. This plus ExtractBitIndex() are used by
  // MakeBloomBits() to form the Bloom filter. These two functions have been
  // extracted from MakeBloomBits() so that they can be shared by RaporAnalyzer.
  //
  // |num_hashes| indicates the the upper bound for the values of |hash_index|
  // that will be passed to ExtractBitIndex() after this method returns.
  //
  // Returns true for success or false if the hash operation fails for any
  // reason.
  static bool HashValueAndCohort(
      const std::string& serialized_value, uint32_t cohort_num,
      uint32_t num_hashes,
      crypto::byte hashed_value[crypto::hash::DIGEST_SIZE]);

  // Extracts a bit index from the given |hashed_value| for the given
  // |hash_index|. This plus HashValueAndCohort are used by MakeBloomBits()
  // to form the Bloom filter. These two functions have been extracted from
  // MakeBloomBits() so that they can be shared by RaporAnalyzer.
  //
  // IMPORTANT: We index bits "from the right." This means that bit number zero
  // is the least significant bit of the last byte of the Bloom filter.
  static uint32_t ExtractBitIndex(
      crypto::byte const hashed_value[crypto::hash::DIGEST_SIZE],
      size_t hash_index, uint32_t num_bits);

  // Generates the array of bloom bits derived from |value|. Returns the
  // empty string on error.
  std::string MakeBloomBits(const ValuePart& value);

  // Derives an integer in the range [0, config_.num_cohorts_2_power_) from
  // |client_secret_| and |attempt_number|. The distribution of values in this
  // range will be (approximately) uniform as the Client Secret and
  // |attempt_number| vary uniformly.
  //
  // This method is invoked iteratively from DeriveCohortFromSecret() with
  // increasing attempt_numbers until the returned value is less than
  // config_.num_cohorts_.
  //
  // Returns UINT32_MAX to indicate failure.
  uint32_t AttemptDeriveCohortFromSecret(size_t attempt_number);

  // Derives an integer in the range [0, config_.num_cohorts_) from
  // |client_secret_|. The distribution of values in this range will be
  // (approximately) uniform as the Client Secret varies uniformly.
  //
  // Returns UINT32_MAX to indicate failure.
  uint32_t DeriveCohortFromSecret();

  std::unique_ptr<RapporConfigValidator> config_;
  std::unique_ptr<crypto::Random> random_;
  encoder::ClientSecret client_secret_;
  uint32_t cohort_num_;
};

// Performs encoding for Basic RAPPOR, a.k.a Categorical RAPPOR. No cohorts
// are used and the list of all candidates must be pre-specified as part
// of the BasicRapporConfig.
// The |client_secret| is used to determine the PRR.
class BasicRapporEncoder {
 public:
  BasicRapporEncoder(const BasicRapporConfig& config,
                     encoder::ClientSecret client_secret);
  ~BasicRapporEncoder() = default;

  // Encodes |value| using Basic RAPPOR encoding. |value| must be one
  // of the categories listed in the |categories| field of the |config|
  // that was passed to the constructor. Returns kOK on success, kInvalidConfig
  // if the |config| passed to the constructor is not valid, and kInvalidInput
  // if |value| is not one of the |categories|.
  Status Encode(const ValuePart& value,
                BasicRapporObservation* observation_out);

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
  void SetRandomForTesting(std::unique_ptr<crypto::Random> random) {
    random_ = std::move(random);
  }

  std::unique_ptr<RapporConfigValidator> config_;
  std::unique_ptr<crypto::Random> random_;
  encoder::ClientSecret client_secret_;
};

}  // namespace cobalt::rappor

#endif  // COBALT_ALGORITHMS_RAPPOR_RAPPOR_ENCODER_H_

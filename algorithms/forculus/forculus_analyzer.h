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

#ifndef COBALT_ALGORITHMS_FORCULUS_FORCULUS_ANALYZER_H_
#define COBALT_ALGORITHMS_FORCULUS_FORCULUS_ANALYZER_H_

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "./observation.pb.h"
#include "algorithms/forculus/forculus_decrypter.h"
#include "config/encodings.pb.h"

namespace cobalt {
namespace forculus {

// A ForculusAnalyzer is constructed for the purpose of performing a single
// Forculus analysis.
//
// (1) Construct a ForculusAnalyzer passing in a ForculusConfig.
//
// (2) Repeatedly invoke AddObservation() to add the set of observations to
//     be analyzed. The observations must all be for the same metric part and
//     must have been encoded using the same encoding configuration. More
//     precisely this means they must be associated with the same customer_id,
//     project_id, metric_id, encoding_config_id and metric_part_name.
//
// (3) Invoke observation_errors() to check that all observations were added
//  without any errors. Invoke num_observations() for the count of observations
//  added.
//
// (4) Invoke TakeResults() to take the results.
//
// (5) Delete the ForculusAnalyzer as it should not be used any more.
//
// Note that the number of observations that are still left unencrypted may
// be computed as the value of num_observations() minus the sum of the values of
// |total_count| in each of the |ResultInfo|s in the map returned by
// TakeResults().
//
// An instance of ForculusAnalyzer is not thread-safe.
class ForculusAnalyzer {
 public:
  // Constructs a ForculusAnalyzer for the given config. All of the observations
  // added via AddObservation() must have been encoded using this config.
  explicit ForculusAnalyzer(const cobalt::ForculusConfig& config);

  // Adds an additional observation to be analyzed. All of the observations
  // added must be for the same metric part and must have been encoded using
  // the same encoding configuration. See comments at the top of this file for
  // more details. Furthermore the observations must have been encoded using
  // the ForculusConfig passed to the constructor.
  //
  // |day_index| is the index of the day that the observation was encoded at
  // the client. It is used to compute an epoch_index. The observations are
  // grouped by (epoch_index, ciphertext) before Forculus decryption is applied.
  //
  // Returns true to indicate the observation was added without error and
  // so num_observations() was incremented or false to indicate there was
  // an error and so observation_errors() was incremented.
  bool AddObservation(uint32_t day_index, const ForculusObservation& obs);

  // The number of times that AddObservation() was invoked minus the value
  // observation_errors().
  size_t num_observations() { return num_observations_; }

  // The number of times that AddObservation() was invoked and the observation
  // was discarded due to an error. If this number is not zero it indicates
  // that the Analyzer received data that was not created by a legitimate
  // Cobalt client. See the error logs for details of the errors.
  size_t observation_errors() { return observation_errors_; }

  // A ResultInfo contains info about one particular recovered plaintext.
  struct ResultInfo {
    explicit ResultInfo(size_t total_count)
        : total_count(total_count), num_epochs(1) {}

    // The total number of observations added to this ForculusAnalyzer that
    // decrypted to the plaintext. This is not the number of *distinct encoder
    // clients* that sent this value. For example if 100 observations from the
    // same encoder client that decrypt to this value were all added, then all
    // 100 will be included in the count. (But the number of observations from
    // distinct encoder clients must have been at least equal to the threshold
    // or the value would not have been decrypted at all.)
    size_t total_count;

    // The number of different epochs that were analyzed that contributed
    // to total_count. For example if the report period were one week and
    // the Forculus epoch were one day then the report period would contain 7
    // different Forculus epochs. Suppose that in 4 of the 7 epochs there
    // were more than the threshold number of observations that decrypted to
    // the plaintext but in the remaining three epochs there were not. Then this
    // value would be 4.
    size_t num_epochs;
  };

  // Returns the results of the Forculus analysis as a map.
  //
  // The keys to the map are all of the recovered plaintexts that were
  // successfully decrypted by the analysis. The values are pointers to
  // information about the recovered plaintext.
  //
  // After this method is invoked this ForculusAnalyzer should be deleted.
  // This is because the contents of the returned map have been moved out
  // of the ForculusAnalyzer leaving the ForculusAnalyzer in an undefined
  // state.
  std::map<std::string, std::unique_ptr<ResultInfo>> TakeResults() {
    return std::move(results_);
  }

 private:
  ForculusConfig config_;
  size_t num_observations_ = 0;
  size_t observation_errors_ = 0;
  std::map<std::string, std::unique_ptr<ResultInfo>> results_;

  // The type of the keys of |decryption_map_|. Represents a group of
  // observations that all come from the same epoch and have the same
  // ciphertext.
  struct DecrypterGroupKey {
    DecrypterGroupKey(uint32_t epoch_index, std::string ciphertext)
        : epoch_index(epoch_index), ciphertext(std::move(ciphertext)) {}

    bool operator==(const DecrypterGroupKey& other) const {
      return other.epoch_index == epoch_index && other.ciphertext == ciphertext;
    }

    // An eopch index. Forculus decryption operates on a set of observations
    // that are all from the same epoch.
    uint32_t epoch_index;

    // A ciphertext to be decrypted.
    std::string ciphertext;
  };

  // The type of the values of |decryption_map_|.
  struct DecrypterResult {
    // Constructs a new DecrypterResult with the given decrypter and a null
    // result_info.
    explicit DecrypterResult(std::unique_ptr<ForculusDecrypter>&& decrypter)
        : decrypter(std::move(decrypter)), result_info(nullptr) {}

    // The ForculusDecrypter corresponding to the key if the ciphertext has
    // not yet been decrypted, or NULL if the ciphertext has already been
    // decrypted or if the ForculusDecrypter was previously corrupted.
    std::unique_ptr<ForculusDecrypter> decrypter;

    // A pointer to the ResultInfo for the recovered plain text
    // corresponding to the key if the ciphertext has already been decrypted,
    // or NULL if the ciphertext has not yet been decrypted.
    ResultInfo* result_info;
  };

  // Hash function for DecrypterGroupKey.
  class KeyHasher {
   public:
    size_t operator()(const DecrypterGroupKey& key) const;
  };

  // A map from DecrypterGroupKeys to their DecrypterResults.
  std::unordered_map<DecrypterGroupKey, DecrypterResult, KeyHasher>
      decryption_map_;
};

}  // namespace forculus
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_FORCULUS_FORCULUS_ANALYZER_H_

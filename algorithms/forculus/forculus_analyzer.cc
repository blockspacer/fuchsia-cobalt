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

#include "algorithms/forculus/forculus_analyzer.h"

#include "./logging.h"
#include "algorithms/forculus/forculus_utils.h"
#include "util/crypto_util/base64.h"
#include "util/log_based_metrics.h"

namespace cobalt::forculus {

// Stackdriver metric constants
namespace {
constexpr char kAddObservationFailure[] =
    "forculus-analyzer-add-observation-failure";
}  // namespace

namespace {
// Produces a string used in an error message to describe the observation.
std::string ErrorString(const ForculusObservation& obs) {
  std::string ciphertext;
  std::string point_x;
  std::string point_y;
  crypto::Base64Encode(obs.ciphertext(), &ciphertext);
  crypto::Base64Encode(obs.point_x(), &point_x);
  crypto::Base64Encode(obs.point_y(), &point_y);
  return "ciphertext=" + ciphertext + " x=" + point_x + " y=" + point_y;
}

}  // namespace

ForculusAnalyzer::ForculusAnalyzer(cobalt::ForculusConfig config)
    : config_(std::move(config)) {}

bool ForculusAnalyzer::AddObservation(uint32_t day_index,
                                      const ForculusObservation& obs) {
  // Compute the epoch_index from the day_index.
  uint32_t epoch_index =
      EpochIndexFromDayIndex(day_index, config_.epoch_type());

  // Look in decryption_map for our (day_index, obs) pair.
  DecrypterGroupKey group_key(epoch_index, obs.ciphertext());
  auto decryption_map_iter = decryption_map_.find(group_key);

  if (decryption_map_iter == decryption_map_.end()) {
    // There was no entry for this group_key in decryption_map. Create a
    // new ForculusDecrypter and a new entry.
    std::unique_ptr<ForculusDecrypter> decrypter(
        new ForculusDecrypter(config_.threshold(), obs.ciphertext()));
    decrypter->AddObservation(obs);
    decryption_map_.emplace(group_key, DecrypterResult(std::move(decrypter)));
  } else {
    // There is already an entry in encryption map.
    DecrypterResult& decrypter_result = decryption_map_iter->second;
    if (decrypter_result.result_info) {
      // The ciphertext has already been decrypted. Just increment the count.
      decrypter_result.result_info->total_count++;
    } else {
      // The ciphertext has not yet been decrypted. Add this additional
      // observation and let's see if that pushes us over the threshold.
      if (!decrypter_result.decrypter) {
        // We have previously deleted the decrypter object because it was
        // in an inconsistent state.
        LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAddObservationFailure)
            << "Skipping decryption because of a previous error: "
            << "day_index=" << day_index << " " << ErrorString(obs);
        observation_errors_++;
        return false;
      }
      if (decrypter_result.decrypter->AddObservation(obs) !=
          ForculusDecrypter::kOK) {
        // Delete the Decrypter object. It is in an inconsistent state.
        LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAddObservationFailure)
            << "Found inconsistent observation. Deleting Decrypter: "
            << ErrorString(obs);
        decrypter_result.decrypter.reset();
        observation_errors_++;
        return false;
      }
      if (decrypter_result.decrypter->size() >= config_.threshold()) {
        // We are now able to decrypt the ciphertext.
        std::string recovered_text;
        if (decrypter_result.decrypter->Decrypt(&recovered_text) !=
            ForculusDecrypter::kOK) {
          // Delete the Decrypter object. It is in an inconsistent state.
          LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAddObservationFailure)
              << "Decryption failed. Deleting Decrypter: " << ErrorString(obs);
          decrypter_result.decrypter.reset();
          observation_errors_++;
          return false;
        }
        uint32_t num_seen = decrypter_result.decrypter->num_seen();

        // Delete the Decrypter object. It has done its job and we don't need
        // it anymore.
        VLOG(4) << "Decryption succeeded: '" << recovered_text
                << "' Deleting Decrypter: day_index=" << day_index << " "
                << ErrorString(obs);
        decrypter_result.decrypter.reset();
        auto results_iter = results_.find(recovered_text);
        if (results_iter == results_.end()) {
          // This is the first time this recovered_text has been seen. Make
          // a new ResultInfo.
          std::unique_ptr<ResultInfo> result_info(new ResultInfo(num_seen));
          // Keep a non-owned pointer to result_info in the decrypter_map
          // so we can find it quickly the next time we get another observation
          // with the same group_key.
          decrypter_result.result_info = result_info.get();
          // Keep the owned pointer in results_.
          results_.emplace(std::move(recovered_text), std::move(result_info));
        } else {
          // This recovered text has been seen before. This happens when
          // we are analyzing more than one Forculus epoch and this same
          // recovered text was seen in a different epoch.
          auto& result_info = results_iter->second;
          result_info->num_epochs++;
          result_info->total_count += num_seen;
          // Keep a non-owned pointer to result_info in the decrypter_map
          // so we can find it quickly the next time we get another observation
          // with the same group_key.
          decrypter_result.result_info = result_info.get();
        }
      }
    }
  }
  num_observations_++;
  return true;
}

size_t ForculusAnalyzer::KeyHasher::operator()(
    const DecrypterGroupKey& key) const {
  // The probability of having the same ciphertext with two different
  // epoch_indexes is negligably small since the epoch_index was one of
  // the ingredients that went into the master key during encryption. For
  // this reason we use the hash of the ciphertext alone as the hash of the
  // pair.
  return std::hash<std::string>()(key.ciphertext);
}

}  // namespace cobalt::forculus

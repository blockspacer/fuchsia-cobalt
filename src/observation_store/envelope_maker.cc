// Copyright 2017 The Fuchsia Authors
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

#include "src/observation_store/envelope_maker.h"

#include <memory>
#include <utility>

#include "src/logging.h"

namespace cobalt::observation_store {

EnvelopeMaker::EnvelopeMaker(size_t max_bytes_each_observation, size_t max_num_bytes)
    : max_bytes_each_observation_(max_bytes_each_observation), max_num_bytes_(max_num_bytes) {}

ObservationStore::StoreStatus EnvelopeMaker::CanAddObservation(
    const StoredObservation& observation) {
  size_t obs_size = observation.ByteSizeLong();
  if (obs_size > max_bytes_each_observation_) {
    VLOG(1) << "WARNING: An Observation that was too big was passed in to "
               "EnvelopeMaker::CanAddObservation(): "
            << obs_size;
    return ObservationStore::kObservationTooBig;
  }

  size_t new_num_bytes = num_bytes_ + obs_size;
  if (new_num_bytes > max_num_bytes_) {
    VLOG(6) << "new_num_bytes(" << new_num_bytes << ") > max_num_bytes_(" << max_num_bytes_ << ")";
    VLOG(4) << "Envelope full.";
    return ObservationStore::kStoreFull;
  }

  return ObservationStore::kOk;
}

ObservationStore::StoreStatus EnvelopeMaker::StoreObservation(
    std::unique_ptr<StoredObservation> observation, std::unique_ptr<ObservationMetadata> metadata) {
  auto status = CanAddObservation(*observation);
  if (status != ObservationStore::kOk) {
    return status;
  }

  num_bytes_ += observation->ByteSizeLong();
  // Put the encrypted observation into the appropriate ObservationBatch.
  GetBatch(std::move(metadata))->add_observation()->Swap(observation.get());
  return ObservationStore::kOk;
}

const Envelope& EnvelopeMaker::GetEnvelope(util::EncryptedMessageMaker* encrypter) {
  envelope_.Clear();

  auto num_batches = batch_map_.size();
  for (auto i = 0; i < num_batches; i++) {
    envelope_.add_batch();
  }

  auto i = num_batches;
  for (const auto& batch : batch_map_) {
    i -= 1;
    ObservationBatch* observation_batch = envelope_.mutable_batch(i);
    *observation_batch->mutable_meta_data() = batch.second->meta_data();
    for (const auto& obs : batch.second->observation()) {
      auto obs_out = observation_batch->add_encrypted_observation();
      if (obs.has_encrypted()) {
        *obs_out = obs.encrypted();
      } else if (obs.has_unencrypted()) {
        if (!encrypter->Encrypt(obs.unencrypted(), obs_out)) {
          LOG_FIRST_N(ERROR, 10) << "ERROR: Unable to encrypt observation on read.";
        }
      }
    }
  }

  return envelope_;
}

StoredObservationBatch* EnvelopeMaker::GetBatch(std::unique_ptr<ObservationMetadata> metadata) {
  // Serialize metadata.
  std::string serialized_metadata;
  (*metadata).SerializeToString(&serialized_metadata);

  // See if metadata is already in batch_map_. If so return it.
  auto iter = batch_map_.find(serialized_metadata);
  if (iter != batch_map_.end()) {
    return iter->second.get();
  }

  batch_map_[serialized_metadata] = std::make_unique<StoredObservationBatch>();
  batch_map_[serialized_metadata]->set_allocated_meta_data(metadata.release());
  return batch_map_[serialized_metadata].get();
}

void EnvelopeMaker::MergeWith(std::unique_ptr<ObservationStore::EnvelopeHolder> other_ref) {
  CHECK(other_ref);
  auto other = std::unique_ptr<EnvelopeMaker>(static_cast<EnvelopeMaker*>(other_ref.release()));
  // Iterate through the other's batch_map_. For each pair...
  for (auto& other_pair : other->batch_map_) {
    // see if we have a pair with the same key.
    auto iter = batch_map_.find(other_pair.first);
    if (iter != batch_map_.end()) {
      // We do have a pair with the same key. Move the EncryptedMessages
      // from the other's batch into our batch. Note that this process
      // reverses the order of the messages in other but the order of
      // the messages in a batch has no meaning so this doesn't matter.
      auto* other_messages = other_pair.second->mutable_observation();
      auto* this_messages = iter->second->mutable_observation();
      while (!other_messages->empty()) {
        this_messages->AddAllocated(other_messages->ReleaseLast());
      }
    } else {
      // We do not have a pair with the same key. Make one and swap the
      // contents of the others batch into it.
      auto observation_batch = std::make_unique<StoredObservationBatch>();
      observation_batch->Swap(other_pair.second.get());
      batch_map_[other_pair.first] = std::move(observation_batch);
    }
  }
  num_bytes_ += other->num_bytes_;
  other->Clear();
}

}  // namespace cobalt::observation_store

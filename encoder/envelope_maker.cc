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

#include "encoder/envelope_maker.h"

#include <memory>
#include <utility>

#include "./logging.h"

namespace cobalt::encoder {

EnvelopeMaker::EnvelopeMaker(size_t max_bytes_each_observation,
                             size_t max_num_bytes)
    : max_bytes_each_observation_(max_bytes_each_observation),
      max_num_bytes_(max_num_bytes) {}

ObservationStore::StoreStatus EnvelopeMaker::CanAddObservation(
    const EncryptedMessage& message) {
  // "+1" below is for the |scheme| field of EncryptedMessage.
  size_t obs_size =
      message.ciphertext().size() + message.public_key_fingerprint().size() + 1;
  if (obs_size > max_bytes_each_observation_) {
    VLOG(1) << "WARNING: An Observation that was too big was passed in to "
               "EnvelopeMaker::CanAddObservation(): "
            << obs_size;
    return ObservationStore::kObservationTooBig;
  }

  size_t new_num_bytes = num_bytes_ + obs_size;
  if (new_num_bytes > max_num_bytes_) {
    VLOG(6) << "new_num_bytes(" << new_num_bytes << ") > max_num_bytes_("
            << max_num_bytes_ << ")";
    VLOG(4) << "Envelope full.";
    return ObservationStore::kStoreFull;
  }

  return ObservationStore::kOk;
}

ObservationStore::StoreStatus EnvelopeMaker::AddEncryptedObservation(
    std::unique_ptr<EncryptedMessage> message,
    std::unique_ptr<ObservationMetadata> metadata) {
  auto status = CanAddObservation(*message);
  if (status != ObservationStore::kOk) {
    return status;
  }

  // "+1" below is for the |scheme| field of EncryptedMessage.
  num_bytes_ += message->ciphertext().size() +
                message->public_key_fingerprint().size() + 1;
  // Put the encrypted observation into the appropriate ObservationBatch.
  GetBatch(std::move(metadata))
      ->add_encrypted_observation()
      ->Swap(message.get());
  return ObservationStore::kOk;
}

ObservationBatch* EnvelopeMaker::GetBatch(
    std::unique_ptr<ObservationMetadata> metadata) {
  // Serialize metadata.
  std::string serialized_metadata;
  (*metadata).SerializeToString(&serialized_metadata);

  // See if metadata is already in batch_map_. If so return it.
  auto iter = batch_map_.find(serialized_metadata);
  if (iter != batch_map_.end()) {
    return iter->second;
  }

  // Create a new ObservationBatch and add it to both envelope_ and batch_map_.
  ObservationBatch* observation_batch = envelope_.add_batch();
  observation_batch->set_allocated_meta_data(metadata.release());
  batch_map_[serialized_metadata] = observation_batch;
  return observation_batch;
}

void EnvelopeMaker::MergeWith(
    std::unique_ptr<ObservationStore::EnvelopeHolder> other_ref) {
  CHECK(other_ref);
  auto other = std::unique_ptr<EnvelopeMaker>(
      static_cast<EnvelopeMaker*>(other_ref.release()));
  // Iterate through the other's batch_map_. For each pair...
  for (auto& other_pair : other->batch_map_) {
    // see if we have a pair with the same key.
    auto iter = batch_map_.find(other_pair.first);
    if (iter != batch_map_.end()) {
      // We do have a pair with the same key. Move the EncryptedMessages
      // from the other's batch into our batch. Note that this process
      // reverses the order of the messages in other but the order of
      // the messages in a batch has no meaning so this doesn't matter.
      auto* other_messages = other_pair.second->mutable_encrypted_observation();
      auto* this_messages = iter->second->mutable_encrypted_observation();
      while (!other_messages->empty()) {
        this_messages->AddAllocated(other_messages->ReleaseLast());
      }
    } else {
      // We do not have a pair with the same key. Make one and swap the
      // contents of the others batch into it.
      ObservationBatch* observation_batch = envelope_.add_batch();
      observation_batch->Swap(other_pair.second);
      batch_map_[other_pair.first] = observation_batch;
    }
  }
  num_bytes_ += other->num_bytes_;
  other->Clear();
}

}  // namespace cobalt::encoder

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_OBSERVATION_STORE_ENVELOPE_MAKER_H_
#define COBALT_SRC_OBSERVATION_STORE_ENVELOPE_MAKER_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "src/lib/util/encrypted_message_util.h"
#include "src/logging.h"
#include "src/observation_store/observation_store.h"
#include "src/pb/encrypted_message.pb.h"
#include "src/pb/observation.pb.h"

namespace cobalt {
namespace encoder {

// EnvelopeMaker is an implementation of ObservationStore::EnvelopeHolder that
// holds its Envelope in memory. This implementation is used by
// MemoryObservationStore.
//
// In addition to the interface for EnvelopeHolder, EnvelopeMaker also
// includes methods for building up an in-memory Envelope by incrementally
// adding additional EncryptedMessages to it.
class EnvelopeMaker : public ObservationStore::EnvelopeHolder {
 public:
  // Constructor
  //
  // |max_bytes_each_observation|. If specified then AddObservation() will
  // return kObservationTooBig if the provided observation's serialized,
  // encrypted size is greater than this value.
  //
  // |max_num_bytes|. If specified then AddObservation() will return kStoreFull
  // if the provided observation's serialized, encrypted size is not too large
  // by itself, but adding the additional observation would cause the sum of the
  // sizes of all added Observations to be greater than this value.
  explicit EnvelopeMaker(size_t max_bytes_each_observation = SIZE_MAX,
                         size_t max_num_bytes = SIZE_MAX);

  // CanAddObservation returns the status that EnvelopeMaker would return if you
  // passed the |message| into AddEncryptedObservation. This allows the user to
  // check if a call will succeed before moving the unique_ptr into
  // AddEncryptedObservation.
  ObservationStore::StoreStatus CanAddObservation(const EncryptedMessage& message);

  // AddEncryptedObservation adds a message and its associated metadata to the
  // store. This should return the same value as CanAddObservation.
  ObservationStore::StoreStatus AddEncryptedObservation(
      std::unique_ptr<EncryptedMessage> message, std::unique_ptr<ObservationMetadata> metadata);

  const Envelope& GetEnvelope() override { return envelope_; }

  bool Empty() const { return envelope_.batch_size() == 0; }

  void Clear() {
    envelope_.Clear();
    batch_map_.clear();
    num_bytes_ = 0;
  }

  void MergeWith(std::unique_ptr<ObservationStore::EnvelopeHolder> other) override;

  // Returns an approximation to the size of the Envelope in bytes. This value
  // is the sum of the sizes of the serialized, encrypted Observations contained
  // in the Envelope. But the size of the EncryptedMessage produced by the
  // method MakeEncryptedEnvelope() may be somewhat larger than this because
  // the Envelope itself may be encrypted to the Shuffler.
  size_t Size() override { return num_bytes_; }

 private:
  friend class EnvelopeMakerTest;

  // Returns the ObservationBatch containing the given |metadata|. If
  // this is the first time we have seen the given |metadata| then a
  // new ObservationBatch is created.
  ObservationBatch* GetBatch(std::unique_ptr<ObservationMetadata> metadata);

  Envelope envelope_;

  // The keys of the map are serialized ObservationMetadata. The values
  // are the ObservationBatch containing that Metadata
  std::unordered_map<std::string, ObservationBatch*> batch_map_;

  // Keeps a running total of the sum of the sizes of the encrypted Observations
  // contained in |envelope_|;
  size_t num_bytes_ = 0;

  const size_t max_bytes_each_observation_;
  const size_t max_num_bytes_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_SRC_OBSERVATION_STORE_ENVELOPE_MAKER_H_

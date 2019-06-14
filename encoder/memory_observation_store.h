// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_MEMORY_OBSERVATION_STORE_H_
#define COBALT_ENCODER_MEMORY_OBSERVATION_STORE_H_

#include <deque>
#include <memory>

#include "encoder/envelope_maker.h"
#include "encoder/observation_store.h"
#include "logger/internal_metrics.h"

namespace cobalt {
namespace encoder {

// MemoryObservationStore is an ObservationStore that stores its data in memory.
class MemoryObservationStore : public ObservationStore {
 public:
  MemoryObservationStore(size_t max_bytes_per_observation,
                         size_t max_bytes_per_envelope, size_t max_bytes_total,
                         logger::LoggerInterface* internal_logger = nullptr);

  StoreStatus AddEncryptedObservation(
      std::unique_ptr<EncryptedMessage> message,
      std::unique_ptr<ObservationMetadata> metadata) override;
  std::unique_ptr<EnvelopeHolder> TakeNextEnvelopeHolder() override;
  void ReturnEnvelopeHolder(std::unique_ptr<EnvelopeHolder> envelopes) override;

  size_t Size() const override;
  bool Empty() const override;
  size_t num_observations_added() override;

  void ResetObservationCounter() override;

 private:
  std::unique_ptr<EnvelopeMaker> NewEnvelopeMaker();
  size_t SizeLocked() const;
  void ReturnEnvelopeHolderLocked(std::unique_ptr<EnvelopeHolder> envelope);

  std::unique_ptr<EnvelopeHolder> TakeOldestEnvelopeHolderLocked();
  void AddEnvelopeToSend(std::unique_ptr<EnvelopeHolder> holder,
                         bool back = true);

  const size_t envelope_send_threshold_size_;

  mutable std::mutex envelope_mutex_;
  std::unique_ptr<EnvelopeMaker> current_envelope_;
  std::deque<std::unique_ptr<EnvelopeHolder>> finalized_envelopes_;
  size_t finalized_envelopes_size_;
  size_t num_observations_added_;

  std::unique_ptr<logger::InternalMetrics> internal_metrics_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_MEMORY_OBSERVATION_STORE_H_

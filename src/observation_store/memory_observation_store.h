// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_OBSERVATION_STORE_MEMORY_OBSERVATION_STORE_H_
#define COBALT_SRC_OBSERVATION_STORE_MEMORY_OBSERVATION_STORE_H_

#include <deque>
#include <memory>

#include "src/logger/internal_metrics.h"
#include "src/observation_store/envelope_maker.h"
#include "src/observation_store/observation_store.h"

namespace cobalt {
namespace observation_store {

// MemoryObservationStore is an ObservationStore that stores its data in memory.
class MemoryObservationStore : public ObservationStore {
 public:
  MemoryObservationStore(size_t max_bytes_per_observation, size_t max_bytes_per_envelope,
                         size_t max_bytes_total,
                         logger::LoggerInterface* internal_logger = nullptr);

  using ObservationStore::StoreObservation;
  StoreStatus StoreObservation(std::unique_ptr<StoredObservation> observation,
                               std::unique_ptr<ObservationMetadata> metadata) override;
  std::unique_ptr<EnvelopeHolder> TakeNextEnvelopeHolder() override;
  void ReturnEnvelopeHolder(std::unique_ptr<EnvelopeHolder> envelopes) override;

  size_t Size() const override;
  bool Empty() const override;

  void ResetInternalMetrics(logger::LoggerInterface* internal_logger) override {
    internal_metrics_ = logger::InternalMetrics::NewWithLogger(internal_logger);
  }

 private:
  std::unique_ptr<EnvelopeMaker> NewEnvelopeMaker();
  size_t SizeLocked() const;
  void ReturnEnvelopeHolderLocked(std::unique_ptr<EnvelopeHolder> envelope);

  std::unique_ptr<EnvelopeHolder> TakeOldestEnvelopeHolderLocked();
  void AddEnvelopeToSend(std::unique_ptr<EnvelopeHolder> holder, bool back = true);

  const size_t envelope_send_threshold_size_;

  mutable std::mutex envelope_mutex_;
  std::unique_ptr<EnvelopeMaker> current_envelope_;
  std::deque<std::unique_ptr<EnvelopeHolder>> finalized_envelopes_;
  size_t finalized_envelopes_size_;

  std::unique_ptr<logger::InternalMetrics> internal_metrics_;
};

}  // namespace observation_store
}  // namespace cobalt

#endif  // COBALT_SRC_OBSERVATION_STORE_MEMORY_OBSERVATION_STORE_H_

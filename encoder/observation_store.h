// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_OBSERVATION_STORE_H_
#define COBALT_ENCODER_OBSERVATION_STORE_H_

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "./envelope.pb.h"
#include "./observation.pb.h"
#include "./observation_batch.pb.h"
#include "logger/logger_interface.h"

namespace cobalt {
namespace encoder {

// ObservationStoreWriterInterface is an abstract interface to a store
// of Observations, to be used by code that writes to this store. This is
// isolated into an abstract interface that can be mocked out in tests.
class ObservationStoreWriterInterface {
 public:
  virtual ~ObservationStoreWriterInterface() = default;

  enum StoreStatus {
    // AddEncryptedObservation() succeeded.
    kOk = 0,
    // The Observation was not added to the store because it is too big.
    kObservationTooBig,
    // The observation was not added to the store because it is full. The
    // Observation itself is not too big to be added otherwise.
    kStoreFull,
    // The Observation was not added to the store because of an unspecified
    // writing error. It may be a file system error, or some other reason.
    kWriteFailed,
  };

  virtual StoreStatus AddEncryptedObservation(std::unique_ptr<EncryptedMessage> message,
                                              std::unique_ptr<ObservationMetadata> metadata) = 0;
};

// ObservationStore is an abstract interface to an underlying store of encrypted
// observations and their metadata. These are organized within the store into
// Envelopes. Individual (encrypted observation, metadata) pairs are added
// one-at-a-time via the method AddEncryptedObservation(). These pairs are
// pooled together and will eventually be combined into an Envelope. These
// Envelopes are then collected into a list, and will be returned one-at-a-time
// from calls to TakeNextEnvelopeHolder(). If there are no envelopes available
// to return, TakeNextEnvelopeHolder() will return nullptr.
//
// The EnvelopeHolders that are returned from this method should be treated as
// "owned" by the caller. When the EnvelopeHolder is destroyed, its underlying
// data is also deleted. If the underlying data should not be deleted (e.g. if
// the upload failed), the EnvelopeHolder should be placed back into the
// ObservationStore using the ReturnEnvelopeHolder() method.
class ObservationStore : public ObservationStoreWriterInterface {
 public:
  // EnvelopeHolder holds a reference to a single Envelope and its underlying
  // data storage. An instance of EnvelopeHolder is considered to own its
  // Envelope. When EnvelopeHolder is deleted, the underlying data storage for
  // the owned Envelope will be deleted. The ObservationStore considers the
  // envelopes owned by EnvelopeHolders to no longer be in the store.
  class EnvelopeHolder {
   public:
    EnvelopeHolder() = default;

    // When this EnvelopeHolder is deleted, the underlying data will be deleted.
    virtual ~EnvelopeHolder() = default;

    // MergeWith takes posession of the Envelope owned by |other| and merges
    // that EnvelopeHolder's underlying data with that of its own. After the
    // call completes, |other| no longer owns any Envelope and it is deleted
    // without deleting any underlying data.
    virtual void MergeWith(std::unique_ptr<EnvelopeHolder> other) = 0;

    // Returns a const reference to the Envelope owned by this EnvelopeHolder.
    // This is not necessarily a cheap operation and may involve reading from
    // disk.
    virtual const Envelope& GetEnvelope() = 0;

    // Returns an estimated size on the wire of the resulting Envelope owned by
    // thes EnvelopeHolder.
    virtual size_t Size() = 0;

   private:
    EnvelopeHolder(const EnvelopeHolder&) = delete;
    EnvelopeHolder& operator=(const EnvelopeHolder&) = delete;
  };

  // max_bytes_per_observation. AddEncryptedObservation() will return
  // kObservationTooBig if the given encrypted Observation's serialized size is
  // bigger than this.
  //
  // max_bytes_per_envelope. When pooling together observations into an
  // Envelope, the ObservationStore will try not to form envelopes larger than
  // this size. This should be used to avoid sending messages over gRPC or HTTP
  // that are too large.
  //
  // max_bytes_total. This is the maximum size of the Observations in the store.
  // If the size of the accumulated Observation data reaches this value then
  // ObservationStore will not accept any more Observations:
  // AddEncryptedObservation() will return kStoreFull, until enough observations
  // are removed from the store.
  //
  // REQUIRED:
  // 0 <= max_bytes_per_observation <= max_bytes_per_envelope <= max_bytes_total
  // 0 <= max_bytes_per_envelope
  explicit ObservationStore(size_t max_bytes_per_observation, size_t max_bytes_per_envelope,
                            size_t max_bytes_total);

  ~ObservationStore() override = default;

  // Returns a human-readable name for the StoreStatus.
  static std::string StatusDebugString(StoreStatus status);

  // Adds the given (encrypted observation, metadata) pair into the store. If
  // this causes the pool of observations to exceed max_bytes_per_envelope, then
  // the ObservationStore will construct an EnvelopeHolder to be returned from
  // TakeNextEnvelopeHolder().
  StoreStatus AddEncryptedObservation(std::unique_ptr<EncryptedMessage> message,
                                      std::unique_ptr<ObservationMetadata> metadata) override = 0;

  // Returns the next EnvelopeHolder from the list of EnvelopeHolders in the
  // store. If there are no more EnvelopeHolders available, this will return
  // nullptr. A given EnvelopeHolder will only be returned from this function
  // *once* unless it is subsequently returned using ReturnEnvelopeHolder.
  virtual std::unique_ptr<EnvelopeHolder> TakeNextEnvelopeHolder() = 0;

  // ReturnEnvelopeHolder takes an EnvelopeHolder and adds it back to the store
  // so that it may be returned by a later call to TakeNextEnvelopeHolder(). Use
  // this when an envelope failed to upload, so the underlying data should not
  // be deleted.
  virtual void ReturnEnvelopeHolder(std::unique_ptr<EnvelopeHolder> envelope) = 0;

  // Resets the internal metrics to use the provided logger.
  virtual void ResetInternalMetrics(logger::LoggerInterface* internal_logger) = 0;

  // Returns true when the size of the data in the ObservationStore exceeds 60%
  // of max_bytes_total.
  [[nodiscard]] bool IsAlmostFull() const;

  // Returns an approximation of the size of all the data in the store.
  [[nodiscard]] virtual size_t Size() const = 0;

  // Returns wether or not the store is entirely empty.
  [[nodiscard]] virtual bool Empty() const = 0;

  // Returns the number of Observations that have been added to the
  // ObservationStore.
  [[nodiscard]] uint64_t num_observations_added() const;

  // Returns a vector containing the number of Observations that have been added
  // to the ObservationStore for each specified report ID.
  [[nodiscard]] std::vector<uint64_t> num_observations_added_for_reports(
      const std::vector<uint32_t>& report_ids) const;

  // Resets the count of Observations that have been added to the
  // ObservationStore.
  void ResetObservationCounter();

 protected:
  // NOLINTNEXTLINE misc-non-private-member-variables-in-classes
  const size_t max_bytes_per_observation_;
  // NOLINTNEXTLINE misc-non-private-member-variables-in-classes
  const size_t max_bytes_per_envelope_;
  // NOLINTNEXTLINE misc-non-private-member-variables-in-classes
  const size_t max_bytes_total_;
  // NOLINTNEXTLINE misc-non-private-member-variables-in-classes
  const size_t almost_full_threshold_;
  // NOLINTNEXTLINE misc-non-private-member-variables-in-classes
  std::map<uint32_t, uint64_t> num_obs_per_report_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_OBSERVATION_STORE_H_

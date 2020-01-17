// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/observation_store/memory_observation_store.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::observation_store {

namespace {

const uint32_t kCustomerId = 11;
const uint32_t kProjectId = 12;
const uint32_t kMetricId = 13;

const size_t kMaxBytesPerObservation = 100;
const size_t kMaxBytesPerEnvelope = 400;
const size_t kMaxBytesTotal = 10000;

class MemoryObservationStoreTest : public ::testing::Test {
 public:
  MemoryObservationStoreTest() : encrypt_(util::EncryptedMessageMaker::MakeUnencrypted()) {
    MakeStore();
  }

  void MakeStore() {
    store_ = std::make_unique<MemoryObservationStore>(kMaxBytesPerObservation, kMaxBytesPerEnvelope,
                                                      kMaxBytesTotal);
  }

  // Adds an Observation to the MemoryObservationStore with the given |metric_id|
  // and such that MemoryObservationStore will consider the size of the
  // Observation to be equal to  |num_bytes|.
  ObservationStore::StoreStatus AddObservation(size_t num_bytes, uint32_t metric_id = kMetricId) {
    auto message = std::make_unique<EncryptedMessage>();
    // We subtract 1 from |num_bytes| because MemoryObservationStore adds one
    // to its definition of size.
    CHECK(num_bytes > 4);
    message->set_ciphertext(std::string(num_bytes - 4, 'x'));
    auto metadata = std::make_unique<ObservationMetadata>();
    metadata->set_customer_id(kCustomerId);
    metadata->set_project_id(kProjectId);
    metadata->set_metric_id(metric_id);
    return store_->StoreObservation(std::move(message), std::move(metadata));
  }

 protected:
  std::unique_ptr<MemoryObservationStore> store_;
  std::unique_ptr<util::EncryptedMessageMaker> encrypt_;
};

}  // namespace

TEST_F(MemoryObservationStoreTest, Disable) {
  EXPECT_EQ(ObservationStore::kOk, AddObservation(50));
  store_->Disable(true);
  auto envelope = store_->TakeNextEnvelopeHolder();
  // The observation that was added before the store started ignoring, should still be returned.
  EXPECT_NE(envelope, nullptr);

  EXPECT_EQ(ObservationStore::kOk, AddObservation(50));
  envelope = store_->TakeNextEnvelopeHolder();
  // Since the observation was ignored, there should be no envelope to return.
  EXPECT_EQ(envelope, nullptr);

  store_->Disable(false);
  // This should still be null, even though the observation store accepts data again.
  EXPECT_EQ(envelope, nullptr);

  EXPECT_EQ(ObservationStore::kOk, AddObservation(50));

  envelope = store_->TakeNextEnvelopeHolder();
  // There should now be data stored.
  EXPECT_NE(envelope, nullptr);
}

TEST_F(MemoryObservationStoreTest, DeleteData) {
  // Note that kMaxBytesPerObservation = 100 and kMaxBytesPerEnvelope = 400.
  //
  // This will add enough to finalize one envelope, and have another in progress.
  for (int i = 0; i < 6; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(kMaxBytesPerObservation));
  }

  EXPECT_NE(store_->Size(), 0);

  store_->DeleteData();

  EXPECT_EQ(store_->Size(), 0);
  EXPECT_EQ(store_->TakeNextEnvelopeHolder(), nullptr);
}

}  // namespace cobalt::observation_store

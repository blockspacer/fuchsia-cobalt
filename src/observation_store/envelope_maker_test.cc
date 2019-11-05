// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/observation_store/envelope_maker.h"

#include <utility>

#include "src/logging.h"
#include "third_party/gflags/include/gflags/gflags.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::observation_store {

namespace {

constexpr uint32_t kCustomerId = 1;
constexpr uint32_t kProjectId = 1;
constexpr uint32_t kFirstMetricId = 1;
constexpr uint32_t kSecondMetricId = 2;
constexpr uint32_t kThirdMetricId = 3;

// This is the day index for Friday Dec 2, 2016
const uint32_t kUtcDayIndex = 17137;
}  // namespace

class EnvelopeMakerTest : public ::testing::Test {
 public:
  EnvelopeMakerTest()
      : envelope_maker_(new EnvelopeMaker()),
        encrypt_(util::EncryptedMessageMaker::MakeUnencrypted()) {}

  // Returns the current value of envelope_maker_ and resets envelope_maker_
  // to a new EnvelopeMaker constructed using the given optional arguments.
  std::unique_ptr<EnvelopeMaker> ResetEnvelopeMaker(size_t max_bytes_each_observation = SIZE_MAX,
                                                    size_t max_num_bytes = SIZE_MAX) {
    std::unique_ptr<EnvelopeMaker> return_val = std::move(envelope_maker_);
    envelope_maker_ = std::make_unique<EnvelopeMaker>(max_bytes_each_observation, max_num_bytes);
    return return_val;
  }

  // Adds an Observation
  // (as represented by an EncryptedMessage and an ObservationMetadata) of size
  // |num_bytes|, with the specified |metric_id|, to the EnvelopeMaker.
  //
  // |expected_num_batches|is the expected number of batches in the
  // EnvelopeMaker after the new Observation has been added.
  //
  // |expected_this_batch_index| The expected index of the batch that the
  // new Observation should have been put into.
  //
  // |expected_this_batch_size| The expected number of Observations in the
  // batch, including the new Observation.
  void AddObservation(size_t num_bytes, uint32_t metric_id, int expected_num_batches,
                      size_t expected_this_batch_index, int expected_this_batch_size,
                      ObservationStore::StoreStatus expected_status) {
    // Create an EncryptedMessage and an ObservationMetadata
    auto message = std::make_unique<EncryptedMessage>();
    // We subtract 1 from |num_bytes| because EnvelopeMaker adds one
    // to its definition of size.
    CHECK(num_bytes > 1);
    message->set_ciphertext(std::string(num_bytes - 1, 'x'));
    auto metadata = std::make_unique<ObservationMetadata>();
    metadata->set_customer_id(kCustomerId);
    metadata->set_project_id(kProjectId);
    metadata->set_metric_id(metric_id);
    metadata->set_day_index(kUtcDayIndex);

    ASSERT_NE(nullptr, envelope_maker_);
    // Add the Observation to the EnvelopeMaker
    size_t size_before_add = envelope_maker_->Size();
    ASSERT_EQ(expected_status,
              envelope_maker_->AddEncryptedObservation(std::move(message), std::move(metadata)));
    size_t size_after_add = envelope_maker_->Size();
    size_t expected_size_change = (expected_status == ObservationStore::kOk ? num_bytes : 0);
    EXPECT_EQ(expected_size_change, size_after_add - size_before_add);

    // Check the number of batches currently in the envelope.
    ASSERT_EQ(expected_num_batches, envelope_maker_->GetEnvelope(encrypt_.get()).batch_size());

    if (expected_status != ObservationStore::kOk) {
      return;
    }

    // Check the ObservationMetadata of the expected batch.
    const auto& batch =
        envelope_maker_->GetEnvelope(encrypt_.get()).batch(expected_this_batch_index);
    EXPECT_EQ(kCustomerId, batch.meta_data().customer_id());
    EXPECT_EQ(kProjectId, batch.meta_data().project_id());
    EXPECT_EQ(metric_id, batch.meta_data().metric_id());
    EXPECT_EQ(kUtcDayIndex, batch.meta_data().day_index());

    // Check the size of the expected batch.
    ASSERT_EQ(expected_this_batch_size, batch.encrypted_observation_size())
        << "batch_index=" << expected_this_batch_index << "; metric_id=" << metric_id;

    // The Observation we just added should be the last one in the batch.
    EXPECT_EQ(num_bytes - 1,
              batch.encrypted_observation(expected_this_batch_size - 1).ciphertext().size());
  }

  // Adds multiple observations to the EnvelopeMaker for the given
  // metric_id. The Observations will have sizes 10 + i for i in [first, limit).
  //
  // expected_num_batches: How many batches do we expecte the EnvelopeMaker to
  // contain after the first add.
  //
  // expected_this_batch_index: Which batch index do we expect this add to
  // have gone into.
  //
  // expected_this_batch_size: What is the expected number of Observations in
  //  the current batch *before* the first add.
  void AddManyObservations(int first, int limit, uint32_t metric_id, int expected_num_batches,
                           size_t expected_this_batch_index, int expected_this_batch_size) {
    for (int i = first; i < limit; i++) {
      AddObservation(10 + i, metric_id, expected_num_batches, expected_this_batch_index,
                     ++expected_this_batch_size, ObservationStore::kOk);
    }
  }

  // Adds multiple encoded Observations to two different metrics. Test that
  // the EnvelopeMaker behaves correctly.
  void DoTest() {
    // Add 10 observations for metric 1
    size_t expected_num_batches = 1;
    size_t expected_this_batch_index = 0;
    size_t expected_batch0_size = 0;
    AddManyObservations(10, 20, kFirstMetricId, expected_num_batches, expected_this_batch_index,
                        expected_batch0_size);
    expected_batch0_size += 10;
    // Add 5 more.
    AddManyObservations(15, 20, kFirstMetricId, expected_num_batches, expected_this_batch_index,
                        expected_batch0_size);
    expected_batch0_size += 5;

    // Add 5 observations for metric 2
    expected_num_batches = 2;
    expected_this_batch_index = 1;
    size_t expected_batch1_size = 0;
    AddManyObservations(15, 20, kSecondMetricId, expected_num_batches, expected_this_batch_index,
                        expected_batch1_size);
    expected_batch1_size += 5;
    // Add 5 more observations for metric 2
    AddManyObservations(20, 25, kSecondMetricId, expected_num_batches, expected_this_batch_index,
                        expected_batch1_size);
    expected_batch1_size += 5;

    // Add 12 more observations for metric 1
    expected_this_batch_index = 0;
    AddManyObservations(11, 23, kFirstMetricId, expected_num_batches, expected_this_batch_index,
                        expected_batch0_size);
    expected_batch0_size += 12;
    // Add 13 more observations for metric 1
    AddManyObservations(11, 24, kFirstMetricId, expected_num_batches, expected_this_batch_index,
                        expected_batch0_size);
    expected_batch0_size += 13;

    // Add 10 more observations for metric 2
    expected_this_batch_index = 1;
    AddManyObservations(10, 20, kSecondMetricId, expected_num_batches, expected_this_batch_index,
                        expected_batch1_size);
    expected_batch1_size += 10;
    // Add 10 more.
    AddManyObservations(10, 20, kSecondMetricId, expected_num_batches, expected_this_batch_index,
                        expected_batch1_size);
    expected_batch1_size += 10;

    const Envelope& envelope = envelope_maker_->GetEnvelope(encrypt_.get());
    EXPECT_EQ(2, envelope.batch_size());
    for (size_t i = 0; i < 2; i++) {
      EXPECT_EQ(i + 1, envelope.batch(i).meta_data().metric_id());
      auto expected_batch_size = (i == 0 ? expected_batch0_size : expected_batch1_size);
      EXPECT_EQ(expected_batch_size, envelope.batch(i).encrypted_observation_size());
    }
  }

 protected:
  std::unique_ptr<EnvelopeMaker> envelope_maker_;
  std::unique_ptr<util::EncryptedMessageMaker> encrypt_;
};

// We perform DoTest() three times with a Clear() between each turn.
// This last tests that Clear() works correctly.
TEST_F(EnvelopeMakerTest, TestThrice) {
  for (int i = 0; i < 3; i++) {
    DoTest();
    envelope_maker_->Clear();
  }
}

// Tests the MergeWith() method.
TEST_F(EnvelopeMakerTest, MergeWith) {
  // Add metric 1 batch to EnvelopeMaker 1 with strings 0..9
  uint32_t metric_id = kFirstMetricId;
  int expected_num_batches = 1;
  size_t expected_this_batch_index = 0;
  int expected_this_batch_size = 0;
  AddManyObservations(10, 20, metric_id, expected_num_batches, expected_this_batch_index,
                      expected_this_batch_size);

  // Add metric 2 batch to EnvelopeMaker 1 with strings 0..9
  metric_id = kSecondMetricId;
  expected_num_batches = 2;
  expected_this_batch_index = 1;
  AddManyObservations(10, 20, metric_id, expected_num_batches, expected_this_batch_index,
                      expected_this_batch_size);

  // Take EnvelopeMaker 1 and create EnvelopeMaker 2.
  auto envelope_maker1 = ResetEnvelopeMaker();

  // Add metric 2 batch to EnvelopeMaker 2 with strings 10..19
  metric_id = kSecondMetricId;
  expected_num_batches = 1;
  expected_this_batch_index = 0;
  AddManyObservations(10, 20, metric_id, expected_num_batches, expected_this_batch_index,
                      expected_this_batch_size);

  // Add metric 3 to EnvelopeMaker 2 with strings 0..9
  metric_id = kThirdMetricId;
  expected_num_batches = 2;
  expected_this_batch_index = 1;
  AddManyObservations(10, 20, metric_id, expected_num_batches, expected_this_batch_index,
                      expected_this_batch_size);
  // Take EnvelopeMaker 2,
  auto envelope_maker2 = ResetEnvelopeMaker();

  // Now invoke MergeWith to merge EnvelopeMaker 2 into EnvelopeMaker 1.
  envelope_maker1->MergeWith(std::move(envelope_maker2));

  // EnvelopeMaker 2 should be null.
  EXPECT_EQ(envelope_maker2, nullptr);

  // EnvelopeMaker 1 should have three batches for Metrics 1, 2, 3
  EXPECT_FALSE(envelope_maker1->Empty());
  ASSERT_EQ(3, envelope_maker1->GetEnvelope(encrypt_.get()).batch_size());

  // Iterate through each of the batches and check it.
  for (uint index = 0; index < 3; index++) {
    // Batch 0 and 2 should have 10 encrypted observations and batch
    // 1 should have 20 because batch 1 from EnvelopeMaker 2 was merged
    // into batch 1 of EnvelopeMaker 1.
    auto& batch = envelope_maker1->GetEnvelope(encrypt_.get()).batch(index);
    EXPECT_EQ(index + 1, batch.meta_data().metric_id());
    auto expected_num_observations = (index == 1 ? 20 : 10);
    ASSERT_EQ(expected_num_observations, batch.encrypted_observation_size());

    // Check each one of the observations.
    for (int i = 0; i < expected_num_observations; i++) {
      // Extract the serialized observation.
      // TODO(rudominer)
    }
  }

  // Now we want to test that after the MergeWith() operation the EnvelopeMaker
  // is still usable. Put EnvelopeMaker 1 back as the test EnvelopeMaker.
  envelope_maker_ = std::move(envelope_maker1);

  // Add string observations 10..19 to metric ID 1 batches 1, 2 and 3.
  for (int metric_id = 1; metric_id <= 3; metric_id++) {
    expected_num_batches = 3;
    expected_this_batch_index = metric_id - 1;
    expected_this_batch_size = (metric_id == 2 ? 20 : 10);
    AddManyObservations(10, 20, metric_id, expected_num_batches, expected_this_batch_index,
                        expected_this_batch_size);
  }
}

// Tests that EnvelopeMaker returns kObservationTooBig when it is supposed to.
TEST_F(EnvelopeMakerTest, ObservationTooBig) {
  // Set max_bytes_each_observation = 105.
  ResetEnvelopeMaker(105);

  size_t expected_num_batches = 1;
  size_t expected_this_batch_index = 0;
  size_t expected_this_batch_size = 1;

  // Add an observation that is not too big.
  AddObservation(105, kFirstMetricId, expected_num_batches, expected_this_batch_index,
                 expected_this_batch_size, ObservationStore::kOk);

  // Try to add an observation that is too big.
  // We expect the Observation to not be added to the Envelope and so for
  // the Envelope size to not change.
  AddObservation(106, kFirstMetricId, expected_num_batches, expected_this_batch_index,
                 expected_this_batch_size, ObservationStore::kObservationTooBig);

  // Add another observation that is not too big.
  expected_this_batch_size = 2;
  AddObservation(104, kFirstMetricId, expected_num_batches, expected_this_batch_index,
                 expected_this_batch_size, ObservationStore::kOk);

  // Try to add another observation that is too big.
  // We expect the Observation to not be added to the Envelope and so for
  // the Envelope size to not change.
  AddObservation(107, kFirstMetricId, expected_num_batches, expected_this_batch_index,
                 expected_this_batch_size, ObservationStore::kObservationTooBig);
}

// Tests that EnvelopeMaker returns kStoreFull when it is supposed to.
TEST_F(EnvelopeMakerTest, EnvelopeFull) {
  // Set max_bytes_each_observation = 100, max_num_bytes=1000.
  ResetEnvelopeMaker(100, 1000);

  int expected_this_batch_size = 1;
  int expected_num_batches = 1;
  size_t expected_this_batch_index = 0;
  for (int i = 0; i < 19; i++) {
    // Add 19 observations of size 50 for a total of 950 bytes
    AddObservation(50, kFirstMetricId, expected_num_batches, expected_this_batch_index,
                   expected_this_batch_size++, ObservationStore::kOk);
  }
  EXPECT_EQ(950u, envelope_maker_->Size());

  // If we try to add an observation of more than 100 bytes we should
  // get kObservationTooBig.

  // We expect the Observation to not be added to the Envelope and so for
  // the Envelope size to not change.

  AddObservation(101, kFirstMetricId, expected_num_batches, expected_this_batch_index,
                 expected_this_batch_size, ObservationStore::kObservationTooBig);

  // If we try to add an observation of 65 bytes we should
  // get kStoreFull

  AddObservation(65, kFirstMetricId, expected_num_batches, expected_this_batch_index,
                 expected_this_batch_size, ObservationStore::kStoreFull);

  // We can add one more observation of size 50.
  AddObservation(50, kFirstMetricId, expected_num_batches, expected_this_batch_index,
                 expected_this_batch_size, ObservationStore::kOk);
}

}  // namespace cobalt::observation_store

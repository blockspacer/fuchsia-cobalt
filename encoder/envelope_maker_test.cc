// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoder/envelope_maker.h"

#include <utility>

#include "./gtest.h"
#include "./logging.h"
#include "config/client_config.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/fake_system_data.h"
#include "encoder/project_context.h"
#include "encoder/system_data.h"
// Generated from envelope_maker_test_config.yaml
#include "encoder/envelope_maker_test_config.h"
#include "third_party/gflags/include/gflags/gflags.h"
#include "util/encrypted_message_util.h"

namespace cobalt {
namespace encoder {

using config::ClientConfig;
using util::EncryptedMessageMaker;

namespace {

// These values must match the values specified in the invocation of
// generate_test_config_h() in CMakeLists.txt. and in the invocation of
// cobalt_config_header("generate_envelope_maker_test_config") in BUILD.gn.
static const uint32_t kCustomerId = 1;
static const uint32_t kProjectId = 1;

static const char* kAnalyzerPublicKey = "analyzer-public-key";
static const char* kShufflerPublicKey = "shuffler-public-key";

// This unix timestamp corresponds to Friday Dec 2, 2016 in UTC
// and Thursday Dec 1, 2016 in Pacific time.
const time_t kSomeTimestamp = 1480647356;
// This is the day index for Friday Dec 2, 2016
const uint32_t kUtcDayIndex = 17137;
const size_t kNoOpEncodingByteOverhead = 30;

// Returns a ProjectContext obtained by parsing the configuration specified
// in envelope_maker_test_config.yaml
std::shared_ptr<ProjectContext> GetTestProject() {
  // Parse the base64-encoded, serialized CobaltRegistry in
  // envelope_maker_test_config.h. This is generated from
  // envelope_maker_test_config.yaml. Edit that yaml file to make changes. The
  // variable name below, |kCobaltRegistryBase64|, must match what is
  // specified in the build files.
  std::unique_ptr<ClientConfig> client_config =
      ClientConfig::CreateFromCobaltRegistryBase64(kCobaltRegistryBase64);
  EXPECT_NE(nullptr, client_config);

  return std::shared_ptr<ProjectContext>(new ProjectContext(
      kCustomerId, kProjectId,
      std::shared_ptr<ClientConfig>(client_config.release())));
}

}  // namespace

class EnvelopeMakerTest : public ::testing::Test {
 public:
  EnvelopeMakerTest()
      : encrypt_to_shuffler_(kShufflerPublicKey, EncryptedMessage::NONE),
        encrypt_to_analyzer_(kAnalyzerPublicKey, EncryptedMessage::NONE),
        envelope_maker_(new EnvelopeMaker()),
        project_(GetTestProject()),
        encoder_(project_, ClientSecret::GenerateNewSecret(),
                 &fake_system_data_) {
    // Set a static current time so we can test the day_index computation.
    encoder_.set_current_time(kSomeTimestamp);
  }

  // Returns the current value of envelope_maker_ and resets envelope_maker_
  // to a new EnvelopeMaker constructed using the given optional arguments.
  std::unique_ptr<EnvelopeMaker> ResetEnvelopeMaker(
      size_t max_bytes_each_observation = SIZE_MAX,
      size_t max_num_bytes = SIZE_MAX) {
    std::unique_ptr<EnvelopeMaker> return_val = std::move(envelope_maker_);
    envelope_maker_.reset(
        new EnvelopeMaker(max_bytes_each_observation, max_num_bytes));
    return return_val;
  }

  // The metric is expected to have a single string part named "Part1" and
  // to use the UTC timezone.
  // expected_size_change: What is the expected change in the size of the
  // envelope in bytes due to the AddObservation()?
  void AddStringObservation(std::string value, uint32_t metric_id,
                            uint32_t encoding_config_id,
                            int expected_num_batches,
                            size_t expected_this_batch_index,
                            int expected_this_batch_size,
                            size_t expected_size_change,
                            ObservationStore::StoreStatus expected_status) {
    // Encode an Observation
    Encoder::Result result =
        encoder_.EncodeString(metric_id, encoding_config_id, value);
    ASSERT_EQ(Encoder::kOK, result.status);
    ASSERT_NE(nullptr, result.observation);
    ASSERT_NE(nullptr, result.metadata);

    ASSERT_NE(nullptr, envelope_maker_);
    // Add the Observation to the EnvelopeMaker
    size_t size_before_add = envelope_maker_->Size();
    auto encrypted_message = std::make_unique<EncryptedMessage>();
    ASSERT_TRUE(encrypt_to_analyzer_.Encrypt(*result.observation,
                                             encrypted_message.get()));
    ASSERT_EQ(expected_status,
              envelope_maker_->AddEncryptedObservation(
                  std::move(encrypted_message), std::move(result.metadata)));
    size_t size_after_add = envelope_maker_->Size();
    EXPECT_EQ(expected_size_change, size_after_add - size_before_add) << value;

    // Check the number of batches currently in the envelope.
    ASSERT_EQ(expected_num_batches,
              envelope_maker_->GetEnvelope().batch_size());

    if (expected_status != ObservationStore::kOk) {
      return;
    }

    // Check the ObservationMetadata of the expected batch.
    const auto& batch =
        envelope_maker_->GetEnvelope().batch(expected_this_batch_index);
    const auto& metadata = batch.meta_data();
    EXPECT_EQ(kCustomerId, metadata.customer_id());
    EXPECT_EQ(kProjectId, metadata.project_id());
    EXPECT_EQ(metric_id, metadata.metric_id());
    EXPECT_EQ(kUtcDayIndex, metadata.day_index());

    // Check the size of the expected batch.
    ASSERT_EQ(expected_this_batch_size, batch.encrypted_observation_size())
        << "batch_index=" << expected_this_batch_index
        << "; metric_id=" << metric_id;

    // Deserialize the most recently added observation from the
    // expected batch.
    EXPECT_EQ(
        EncryptedMessage::NONE,
        batch.encrypted_observation(expected_this_batch_size - 1).scheme());
    std::string serialized_observation =
        batch.encrypted_observation(expected_this_batch_size - 1).ciphertext();
    Observation recovered_observation;
    ASSERT_TRUE(recovered_observation.ParseFromString(serialized_observation));
    // Check that it looks right.
    ASSERT_EQ(1u, recovered_observation.parts().size());
    auto iter = recovered_observation.parts().find("Part1");
    ASSERT_TRUE(iter != recovered_observation.parts().cend());
    const auto& part = iter->second;
    ASSERT_EQ(encoding_config_id, part.encoding_config_id());
  }

  // Adds multiple string observations to the EnvelopeMaker for the given
  // metric_id and for encoding_config_id=3, the NoOp encoding. The string
  // values will be "value<i>" for i in [first, limit).
  // expected_num_batches: How many batches do we expecte the EnvelopeMaker to
  // contain after the first add.
  // expected_this_batch_index: Which batch index do we expect this add to
  // have gone into.
  // expected_this_batch_size: What is the expected size of the current batch
  // *before* the first add.
  void AddManyStringsNoOp(int first, int limit, uint32_t metric_id,
                          int expected_num_batches,
                          size_t expected_this_batch_index,
                          int expected_this_batch_size) {
    for (int i = first; i < limit; i++) {
      std::ostringstream stream;
      stream << "value " << i;
      size_t expected_observation_num_bytes =
          kNoOpEncodingByteOverhead + (i >= 10 ? 8 : 7);
      expected_this_batch_size++;
      AddStringObservation(
          stream.str(), metric_id, kNoOpEncodingId, expected_num_batches,
          expected_this_batch_index, expected_this_batch_size,
          expected_observation_num_bytes, ObservationStore::kOk);
    }
  }

  // Adds multiple encoded Observations to two different metrics. Test that
  // the EnvelopeMaker behaves correctly.
  void DoTest() {
    // Add two observations for metric 1
    size_t expected_num_batches = 1;
    size_t expected_this_batch_index = 0;
    size_t expected_this_batch_size = 1;
    // NOTE(rudominer) The values of expected_observation_num_bytes for
    // the Forculus and Basic RAPPOR encodings in this test are obtained from
    // experimentation rather than calculation. We are therefore not testing
    // that the values are correct but rather testing that there is no
    // regression in the size() functionality. Also just eybealling the numbers
    // serves as a sanity test. Notice that the Forculus Observations are
    // rather large compared to the Basic RAPPOR observations with 3 categories.
    size_t expected_observation_num_bytes = 121;
    AddStringObservation("a value", kFirstMetricId, kForculusEncodingId,
                         expected_num_batches, expected_this_batch_index,
                         expected_this_batch_size,
                         expected_observation_num_bytes, ObservationStore::kOk);
    expected_this_batch_size = 2;
    expected_observation_num_bytes = 29;
    AddStringObservation("Apple", kFirstMetricId, kBasicRapporEncodingId,
                         expected_num_batches, expected_this_batch_index,
                         expected_this_batch_size,
                         expected_observation_num_bytes, ObservationStore::kOk);

    // Add two observations for metric 2
    expected_num_batches = 2;
    expected_this_batch_index = 1;
    expected_this_batch_size = 1;
    expected_observation_num_bytes = 122;
    AddStringObservation("a value2", kSecondMetricId, kForculusEncodingId,
                         expected_num_batches, expected_this_batch_index,
                         expected_this_batch_size,
                         expected_observation_num_bytes, ObservationStore::kOk);
    expected_this_batch_size = 2;
    expected_observation_num_bytes = 29;
    AddStringObservation("Banana", kSecondMetricId, kBasicRapporEncodingId,
                         expected_num_batches, expected_this_batch_index,
                         expected_this_batch_size,
                         expected_observation_num_bytes, ObservationStore::kOk);

    // Add two more observations for metric 1
    expected_this_batch_index = 0;
    expected_this_batch_size = 3;
    expected_observation_num_bytes = 122;
    AddStringObservation("a value3", kFirstMetricId, kForculusEncodingId,
                         expected_num_batches, expected_this_batch_index,
                         expected_this_batch_size,
                         expected_observation_num_bytes, ObservationStore::kOk);
    expected_this_batch_size = 4;
    expected_observation_num_bytes = 29;
    AddStringObservation("Banana", kFirstMetricId, kBasicRapporEncodingId,
                         expected_num_batches, expected_this_batch_index,
                         expected_this_batch_size,
                         expected_observation_num_bytes, ObservationStore::kOk);

    // Add two more observations for metric 2
    expected_this_batch_index = 1;
    expected_this_batch_size = 3;
    expected_observation_num_bytes = 123;
    AddStringObservation("a value40", kSecondMetricId, kForculusEncodingId,
                         expected_num_batches, expected_this_batch_index,
                         expected_this_batch_size,
                         expected_observation_num_bytes, ObservationStore::kOk);
    expected_this_batch_size = 4;
    expected_observation_num_bytes = 29;
    AddStringObservation("Cantaloupe", kSecondMetricId, kBasicRapporEncodingId,
                         expected_num_batches, expected_this_batch_index,
                         expected_this_batch_size,
                         expected_observation_num_bytes, ObservationStore::kOk);

    // Make the encrypted Envelope.
    EncryptedMessage encrypted_message;
    EXPECT_TRUE(encrypt_to_shuffler_.Encrypt(envelope_maker_->GetEnvelope(),
                                             &encrypted_message));

    // Decrypt encrypted_message. (No actual decryption is involved since
    // we used the NONE encryption scheme.)
    util::MessageDecrypter decrypter("");
    Envelope recovered_envelope;
    EXPECT_TRUE(
        decrypter.DecryptMessage(encrypted_message, &recovered_envelope));

    // Check that it looks right.
    EXPECT_EQ(2, recovered_envelope.batch_size());
    for (size_t i = 0; i < 2; i++) {
      EXPECT_EQ(i + 1, recovered_envelope.batch(i).meta_data().metric_id());
      EXPECT_EQ(4, recovered_envelope.batch(i).encrypted_observation_size());
    }
  }

 protected:
  EncryptedMessageMaker encrypt_to_shuffler_;
  EncryptedMessageMaker encrypt_to_analyzer_;
  FakeSystemData fake_system_data_;
  std::unique_ptr<EnvelopeMaker> envelope_maker_;
  std::shared_ptr<ProjectContext> project_;
  Encoder encoder_;
};

// We perform DoTest() three times with a Clear() between each turn.
// This last tests that Clear() works correctly.
TEST_F(EnvelopeMakerTest, TestAll) {
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
  AddManyStringsNoOp(0, 10, metric_id, expected_num_batches,
                     expected_this_batch_index, expected_this_batch_size);

  // Add metric 2 batch to EnvelopeMaker 1 with strings 0..9
  metric_id = kSecondMetricId;
  expected_num_batches = 2;
  expected_this_batch_index = 1;
  AddManyStringsNoOp(0, 10, metric_id, expected_num_batches,
                     expected_this_batch_index, expected_this_batch_size);

  // Take EnvelopeMaker 1 and create EnvelopeMaker 2.
  auto envelope_maker1 = ResetEnvelopeMaker();

  // Add metric 2 batch to EnvelopeMaker 2 with strings 10..19
  metric_id = kSecondMetricId;
  expected_num_batches = 1;
  expected_this_batch_index = 0;
  AddManyStringsNoOp(10, 20, metric_id, expected_num_batches,
                     expected_this_batch_index, expected_this_batch_size);

  // Add metric 3 to EnvelopeMaker 2 with strings 0..9
  metric_id = kThirdMetricId;
  expected_num_batches = 2;
  expected_this_batch_index = 1;
  AddManyStringsNoOp(0, 10, metric_id, expected_num_batches,
                     expected_this_batch_index, expected_this_batch_size);

  // Take EnvelopeMaker 2,
  auto envelope_maker2 = ResetEnvelopeMaker();

  // Now invoke MergeWith to merge EnvelopeMaker 2 into EnvelopeMaker 1.
  envelope_maker1->MergeWith(std::move(envelope_maker2));

  // EnvelopeMaker 2 should be null.
  EXPECT_EQ(envelope_maker2, nullptr);

  // EnvelopeMaker 1 should have three batches for Metrics 1, 2, 3
  EXPECT_FALSE(envelope_maker1->Empty());
  ASSERT_EQ(3, envelope_maker1->GetEnvelope().batch_size());

  // Iterate through each of the batches and check it.
  for (uint index = 0; index < 3; index++) {
    // Batch 0 and 2 should have 10 encrypted observations and batch
    // 1 should have 20 because batch 1 from EnvelopeMaker 2 was merged
    // into batch 1 of EnvelopeMaker 1.
    auto& batch = envelope_maker1->GetEnvelope().batch(index);
    EXPECT_EQ(index + 1, batch.meta_data().metric_id());
    auto expected_num_observations = (index == 1 ? 20 : 10);
    ASSERT_EQ(expected_num_observations, batch.encrypted_observation_size());

    // Check each one of the observations.
    for (int i = 0; i < expected_num_observations; i++) {
      // Extract the serialized observation.
      auto& encrypted_message = batch.encrypted_observation(i);
      EXPECT_EQ(EncryptedMessage::NONE, encrypted_message.scheme());
      std::string serialized_observation = encrypted_message.ciphertext();
      Observation recovered_observation;
      ASSERT_TRUE(
          recovered_observation.ParseFromString(serialized_observation));

      // Check that it looks right.
      ASSERT_EQ(1u, recovered_observation.parts().size());
      auto iter = recovered_observation.parts().find("Part1");
      ASSERT_TRUE(iter != recovered_observation.parts().cend());
      const auto& part = iter->second;
      ASSERT_EQ(3u, part.encoding_config_id());
      ASSERT_TRUE(part.has_unencoded());

      // Check the string values. Batches 0 and 2 are straightforward. The
      // values should be {"value 0", "value 1", .. "value 9"}. But
      // batch 1 is more complicated. Because of the way merge is implemented
      // we expect to see:
      // {"value 0", "value 1", .. "value 9", "value 19",
      //                                           "value 18", ... "value 10"}
      // This is because when we merged batch 1 of Envelope 2 into batch
      // 1 of Envelope 1 we reversed the order of the observations in
      // Ennvelope 2.
      std::ostringstream stream;
      int expected_value_index = i;
      if (index == 1 && i >= 10) {
        expected_value_index = 29 - i;
      }
      stream << "value " << expected_value_index;
      auto expected_string_value = stream.str();
      EXPECT_EQ(expected_string_value,
                part.unencoded().unencoded_value().string_value());
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
    AddManyStringsNoOp(10, 20, metric_id, expected_num_batches,
                       expected_this_batch_index, expected_this_batch_size);
  }
}

// Tests that EnvelopeMaker returns kObservationTooBig when it is supposed to.
TEST_F(EnvelopeMakerTest, ObservationTooBig) {
  // Set max_bytes_each_observation = 105.
  ResetEnvelopeMaker(105);

  // Build an input string of length 75 bytes.
  std::string value(75, 'x');

  size_t expected_observation_num_bytes = 75 + kNoOpEncodingByteOverhead;

  // Invoke AddStringObservation() and expect kOk
  int expected_num_batches = 1;
  size_t expected_this_batch_index = 0;
  int expected_this_batch_size = 1;
  AddStringObservation(value, kFirstMetricId, kNoOpEncodingId,
                       expected_num_batches, expected_this_batch_index,
                       expected_this_batch_size, expected_observation_num_bytes,
                       ObservationStore::kOk);

  // Build an input string of length 101 bytes.
  value = std::string(101, 'x');
  // We expect the Observation to not be added to the Envelope and so for
  // the Envelope size to not change.
  expected_observation_num_bytes = 0;

  // Invoke AddStringObservation() and expect kObservationTooBig
  AddStringObservation(value, kFirstMetricId, kNoOpEncodingId,
                       expected_num_batches, expected_this_batch_index,
                       expected_this_batch_size, expected_observation_num_bytes,
                       ObservationStore::kObservationTooBig);

  // Build an input string of length 75 bytes again.
  value = std::string(75, 'x');
  expected_observation_num_bytes = 75 + kNoOpEncodingByteOverhead;
  expected_this_batch_size = 2;
  // Invoke AddStringObservation() and expect kOk.
  AddStringObservation(value, kFirstMetricId, kNoOpEncodingId,
                       expected_num_batches, expected_this_batch_index,
                       expected_this_batch_size, expected_observation_num_bytes,
                       ObservationStore::kOk);
}

// Tests that EnvelopeMaker returns kStoreFull when it is supposed to.
TEST_F(EnvelopeMakerTest, EnvelopeFull) {
  // Set max_bytes_each_observation = 100, max_num_bytes=1000.
  ResetEnvelopeMaker(100, 1000);

  int expected_this_batch_size = 1;
  int expected_num_batches = 1;
  size_t expected_this_batch_index = 0;
  for (int i = 0; i < 19; i++) {
    // Build an input string of length 20 bytes.
    std::string value(20, 'x');
    size_t expected_observation_num_bytes = 20 + kNoOpEncodingByteOverhead;

    // Invoke AddStringObservation() and expect kOk
    AddStringObservation(value, kFirstMetricId, kNoOpEncodingId,
                         expected_num_batches, expected_this_batch_index,
                         expected_this_batch_size++,
                         expected_observation_num_bytes, ObservationStore::kOk);
  }
  EXPECT_EQ(950u, envelope_maker_->Size());

  // If we try to add an observation of more than 100 bytes we should
  // get kObservationTooBig.
  std::string value(101, 'x');
  // We expect the Observation to not be added to the Envelope and so for
  // the Envelope size to not change.
  size_t expected_observation_num_bytes = 0;
  AddStringObservation(
      value, kFirstMetricId, kNoOpEncodingId, expected_num_batches,
      expected_this_batch_index, expected_this_batch_size++,
      expected_observation_num_bytes, ObservationStore::kObservationTooBig);

  // If we try to add an observation of 65 bytes we should
  // get kStoreFull
  value = std::string(65, 'x');
  AddStringObservation(
      value, kFirstMetricId, kNoOpEncodingId, expected_num_batches,
      expected_this_batch_index, expected_this_batch_size++,
      expected_observation_num_bytes, ObservationStore::kStoreFull);
}

}  // namespace encoder
}  // namespace cobalt

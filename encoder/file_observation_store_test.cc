// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoder/file_observation_store.h"

#include <random>
#include <utility>

#include "./gtest.h"
#include "./logging.h"
#include "encoder/client_secret.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "util/posix_file_system.h"

namespace cobalt {
namespace encoder {

using ::testing::MatchesRegex;
using util::PosixFileSystem;

namespace {

const uint32_t kCustomerId = 11;
const uint32_t kProjectId = 12;
const uint32_t kMetricId = 13;

const size_t kMaxBytesPerObservation = 100;
const size_t kMaxBytesPerEnvelope = 400;
const size_t kMaxBytesTotal = 10000;

constexpr char test_dir_base[] = "/tmp/fos_test";

std::string GetTestDirName(const std::string &base) {
  std::stringstream fname;
  fname << base << "_"
        << std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
               .count();
  return fname.str();
}

class FileObservationStoreTest : public ::testing::Test {
 public:
  FileObservationStoreTest() : test_dir_name_(GetTestDirName(test_dir_base)) {
    MakeStore();
  }

  void MakeStore() {
    store_ = std::make_unique<FileObservationStore>(
        kMaxBytesPerObservation, kMaxBytesPerEnvelope, kMaxBytesTotal,
        std::make_unique<PosixFileSystem>(), test_dir_name_);
  }

  void TearDown() override { store_->Delete(); }

  // Adds an Observation to the FileObservationStore with the given |metric_id|
  // and such that FileObservationStore will consider the size of the
  // Observation to be equal to  |num_bytes|.
  ObservationStore::StoreStatus AddObservation(size_t num_bytes,
                                               uint32_t metric_id = kMetricId) {
    auto message = std::make_unique<EncryptedMessage>();
    // We subtract 1 from |num_bytes| because FileObservationStore adds one
    // to its definition of size.
    CHECK(num_bytes > 1);
    message->set_ciphertext(std::string(num_bytes - 1, 'x'));
    auto metadata = std::make_unique<ObservationMetadata>();
    metadata->set_customer_id(kCustomerId);
    metadata->set_project_id(kProjectId);
    metadata->set_metric_id(metric_id);
    return store_->AddEncryptedObservation(std::move(message),
                                           std::move(metadata));
  }

 protected:
  // NOLINTNEXTLINE misc-non-private-member-variables-in-classes
  std::string test_dir_name_;
  // NOLINTNEXTLINE misc-non-private-member-variables-in-classes
  std::unique_ptr<FileObservationStore> store_;
};

}  // namespace

// Adds some small Observations and checks that the count of received
// Observations is incremented correctly. Checks that ResetObservationCount()
// zeros the count.
TEST_F(FileObservationStoreTest, UpdateObservationCount) {
  EXPECT_EQ(store_->num_observations_added(), 0u);
  EXPECT_EQ(ObservationStore::kOk, AddObservation(40));
  EXPECT_EQ(store_->num_observations_added(), 1u);
  EXPECT_EQ(ObservationStore::kOk, AddObservation(40));
  EXPECT_EQ(store_->num_observations_added(), 2u);
  store_->ResetObservationCounter();
  EXPECT_EQ(store_->num_observations_added(), 0u);
  EXPECT_EQ(ObservationStore::kObservationTooBig,
            AddObservation(kMaxBytesPerObservation + 1));
  EXPECT_EQ(store_->num_observations_added(), 0u);
}

// Adds a too-big Observation. Checks that a |kObservationTooBig| status is
// returned and that the count of received Observations is not incremented.
TEST_F(FileObservationStoreTest, UpdateObservationCountTooBig) {
  ASSERT_EQ(store_->num_observations_added(), 0u);
  EXPECT_EQ(ObservationStore::kObservationTooBig,
            AddObservation(kMaxBytesPerObservation + 1));
  EXPECT_EQ(store_->num_observations_added(), 0u);
}

TEST_F(FileObservationStoreTest, AddRetrieveSingleObservation) {
  EXPECT_EQ(ObservationStore::kOk, AddObservation(50));
  auto envelope = store_->TakeNextEnvelopeHolder();
  // Since we haven't written kMaxBytesPerEnvelope yet, there are no finalized
  // envelopes, TakeNextEnvelopeHolder should force the active file to finalize.
  EXPECT_NE(envelope, nullptr);
}

TEST_F(FileObservationStoreTest, AddRetrieveFullEnvelope) {
  // Note that kMaxBytesPerObservation = 100 and kMaxBytesPerEnvelope = 400.
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(kMaxBytesPerObservation));
  }

  auto envelope = store_->TakeNextEnvelopeHolder();
  ASSERT_NE(envelope, nullptr);
  auto read_env = envelope->GetEnvelope();
  EXPECT_EQ(read_env.batch_size(), 1);
  EXPECT_EQ(read_env.batch(0).encrypted_observation_size(), 4);
}

TEST_F(FileObservationStoreTest, AddRetrieveMultipleFullEnvelopes) {
  static const int num_envelopes = 5;
  static const int envelope_size = 4;
  for (int i = 0; i < num_envelopes * envelope_size; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(100)) << "i=" << i;
  }

  for (int i = 0; i < num_envelopes; i++) {
    auto envelope = store_->TakeNextEnvelopeHolder();
    ASSERT_NE(envelope, nullptr);
    auto read_env = envelope->GetEnvelope();
    EXPECT_EQ(read_env.batch_size(), 1);
    EXPECT_EQ(read_env.batch(0).encrypted_observation_size(), envelope_size);
  }
}

TEST_F(FileObservationStoreTest, Add2FullAndReturn1) {
  for (int i = 0; i < 2 * 4; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(100));
  }

  auto first_envelope = store_->TakeNextEnvelopeHolder();
  ASSERT_NE(first_envelope, nullptr);
  auto second_envelope = store_->TakeNextEnvelopeHolder();
  ASSERT_NE(second_envelope, nullptr);
  EXPECT_TRUE(store_->Empty());

  // Delete the second envelope
  second_envelope = nullptr;
  EXPECT_TRUE(store_->Empty());

  store_->ReturnEnvelopeHolder(std::move(first_envelope));
  EXPECT_FALSE(store_->Empty());
}

// Tests that kStoreFull is returned when the store becomes full.
TEST_F(FileObservationStoreTest, StoreFull) {
  constexpr int kObservationSize = 100;

  // Note that kNumObservationsThatWillFit is discovered by experiment
  // since the precise size of an observation is awkward to arrange since
  // it depends on protobuf serialization.
  constexpr int kNumObservationsThatWillFit = 94;

  // Fill the store until its full.
  for (int i = 0; i < kNumObservationsThatWillFit; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(kObservationSize)) << "i=" << i;
  }

  // Check that kStoreFull is returned repeatedly.
  for (int i = 0; i < 100; i++) {
    EXPECT_EQ(ObservationStore::kStoreFull, AddObservation(kObservationSize)) << "i=" << i;
  }

  // Now let's empty the store
  for (int i = 0; i < 100; i++) {
    if (store_->TakeNextEnvelopeHolder() == nullptr) {
      break;
    }
  }
  ASSERT_TRUE(store_->Empty());
  ASSERT_TRUE(store_->TakeNextEnvelopeHolder() == nullptr);

  // Now we do a second slightly more complicated experiment. This time
  // as we are filling the store we also periodically make some withdrawels,
  // but not enough withdrawels to keep the store from becoming full.

  // For each iteration we add kNumStepsPerIteration observations and then
  // we take one envelope. At some point in this process we expect the
  // store to become full. Again these constants are determined by
  // experimentation.
  constexpr int kExpectedFullIteration = 18;
  constexpr int kExpectedFullStep = 4;
  constexpr int kNumStepsPerIteration = 10;

  int iteration = 0;
  int step = 0;
  while (true) {
    if (step == kExpectedFullStep && iteration == kExpectedFullIteration) {
      break;
    }
    ASSERT_EQ(ObservationStore::kOk, AddObservation(kObservationSize))
        << "iteration=" << iteration << " step=" << step;
    if (++step == kNumStepsPerIteration - 1) {
      step = 0;
      iteration++;
      ASSERT_TRUE(store_->TakeNextEnvelopeHolder() != nullptr);
    }
  }

  // Check that kStoreFull is returned repeatedly.
  for (int i = 0; i < 100; i++) {
    EXPECT_EQ(ObservationStore::kStoreFull, AddObservation(kObservationSize)) << "i=" << i;
  }
}

TEST_F(FileObservationStoreTest, RecoverAfterCrashWithNoObservations) {
  EXPECT_TRUE(store_->Empty());

  // Simulate the store crashing.
  store_ = nullptr;

  // Store restarts.
  MakeStore();

  // The store should still be empty.
  EXPECT_TRUE(store_->Empty());
}

TEST_F(FileObservationStoreTest, RecoverAfterCrash) {
  // Add some observations, but not enough to finalize.
  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(100));
    EXPECT_EQ(store_->ListFinalizedFiles().size(), 0u);
  }

  // Simulate the store crashing.
  store_ = nullptr;

  // Store restarts.
  MakeStore();

  // The store should finalize the in-progress envelope.
  EXPECT_FALSE(store_->Empty());
  EXPECT_EQ(store_->ListFinalizedFiles().size(), 1u);
}

TEST_F(FileObservationStoreTest, IgnoresUnexpectedFiles) {
  { std::ofstream dummy(test_dir_name_ + "/BAD_FILE"); }
  EXPECT_EQ(store_->ListFinalizedFiles().size(), 0u);
  EXPECT_EQ(store_->TakeNextEnvelopeHolder(), nullptr);

  { std::ofstream empty_invalid(test_dir_name_ + "/10000000-100000000.data"); }
  EXPECT_EQ(store_->ListFinalizedFiles().size(), 0u);
  EXPECT_EQ(store_->TakeNextEnvelopeHolder(), nullptr);

  { std::ofstream empty_valid(test_dir_name_ + "/1234567890123-1234567890.data"); }
  EXPECT_EQ(store_->ListFinalizedFiles().size(), 1u);
  EXPECT_NE(store_->TakeNextEnvelopeHolder(), nullptr);
}

TEST_F(FileObservationStoreTest, HandlesCorruptFiles) {
  {
    std::ofstream file(test_dir_name_ + "/1234567890123-1234567890.data");
    file << "CORRUPT DATA!!!";
  }
  EXPECT_EQ(store_->ListFinalizedFiles().size(), 1u);
  auto env = store_->TakeNextEnvelopeHolder();
  ASSERT_NE(env, nullptr);

  auto read_env = env->GetEnvelope();
  EXPECT_EQ(read_env.batch_size(), 0);
}

TEST_F(FileObservationStoreTest, StressTest) {
  std::random_device rd;
  for (int i = 0; i < 5000; i++) {  // NOLINT
    // Between 5-15 observations.
    auto observations = (rd() % 10) + 5;
    // Between 50-100 bytes per observation.
    auto size = (rd() % 50) + 50;  // NOLINT
    for (auto j = 0u; j < observations; j++) {
      EXPECT_EQ(ObservationStore::kOk, AddObservation(size));
    }

    while (true) {
      auto holder = store_->TakeNextEnvelopeHolder();
      if (holder == nullptr) {
        break;
      }

      auto should_return = rd() % 2;
      if (should_return == 1) {
        store_->ReturnEnvelopeHolder(std::move(holder));
      } else {
        auto env = holder->GetEnvelope();
        ASSERT_GT(env.batch_size(), 0);
      }
    }

    ASSERT_EQ(store_->Size(), 0u);
  }
}

TEST(FilenameGenerator, PadsTimestamp) {
  EXPECT_THAT(FileObservationStore::FilenameGenerator([] { return 1234; }).GenerateFilename(),
              MatchesRegex(R"(0000000001234-[0-9]{10}.data)"));
  EXPECT_THAT(FileObservationStore::FilenameGenerator([] { return 1234567; }).GenerateFilename(),
              MatchesRegex(R"(0000001234567-[0-9]{10}.data)"));
  EXPECT_THAT(
      FileObservationStore::FilenameGenerator([] { return 1234567890123; }).GenerateFilename(),
      MatchesRegex(R"(1234567890123-[0-9]{10}.data)"));
  EXPECT_THAT(
      FileObservationStore::FilenameGenerator([] { return 12345678901239; }).GenerateFilename(),
      MatchesRegex(R"(1234567890123-[0-9]{10}.data)"));
}

}  // namespace encoder
}  // namespace cobalt

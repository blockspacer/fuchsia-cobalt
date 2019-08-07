// Copyright 2016 The Fuchsia Authors
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

#include "src/algorithms/forculus/forculus_analyzer.h"

#include <gflags/gflags.h>

#include "src/algorithms/forculus/forculus_encrypter.h"
#include "src/logging.h"
#include "src/system_data/client_secret.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace forculus {

using encoder::ClientSecret;

static const uint32_t kThreshold = 20;

namespace {
// Encrypts the plaintext using Forculus encryption with the given day_index
// and EpochType and threshold = kThreshold and default values for the other
// parameters. A fresh ClientSecret will be generated each time this function
// is invoked. Returns a ForculusObservation containing the ciphertext.
ForculusObservation Encrypt(uint32_t day_index, const EpochType& epoch_type,
                            const std::string& plaintext) {
  // Make a config with the given threshold
  ForculusConfig config;
  config.set_threshold(kThreshold);
  config.set_epoch_type(epoch_type);

  // Construct an Encrypter.
  ForculusEncrypter encrypter(config, 0, 0, 0, "", ClientSecret::GenerateNewSecret());

  // Invoke Encrypt() and check the status.
  ForculusObservation obs;
  EXPECT_EQ(ForculusEncrypter::kOK, encrypter.Encrypt(plaintext, day_index, &obs));
  return obs;
}

// Creates and adds observations to |forculus_analyzer| based on the parameters.
void AddObservations(ForculusAnalyzer* forculus_analyzer, uint32_t day_index,
                     const EpochType& epoch_type, const std::string& plaintext, int num_clients,
                     int num_copies_per_client) {
  // Simulate num_clients different clients.
  for (int i = 0; i < num_clients; i++) {
    auto observation = Encrypt(day_index, epoch_type, plaintext);
    // Each client adds the same observation |num_copies_per_client| times.
    for (int i = 0; i < num_copies_per_client; i++) {
      EXPECT_TRUE(forculus_analyzer->AddObservation(day_index, observation));
    }
  }
}

}  // namespace

// Tests the use of a ForculusAnalyzer when used properly with no
// errors. We simulate multiple clients encrypting multpile plaintexts
// on multiple days. Each client can encrypt the same plaintext multiple
// times on the same day.
TEST(ForculusAnalyzerTest, NoErrors) {
  ForculusConfig forculus_config;
  forculus_config.set_threshold(kThreshold);
  ForculusAnalyzer forculus_analyzer(forculus_config);

  const std::string plaintext1("The woods are lovely, dark and deep,");
  const std::string plaintext2("But I have promises to keep,");
  const std::string plaintext3("And miles to go before I sleep,");
  const std::string plaintext4("And miles to go before I sleep.");

  // 20 * 5 observations of plaintext1 on day 0. (This means 20 different
  // clients each encrypting plaintext1 5 times on day 0.)
  AddObservations(&forculus_analyzer, 0, DAY, plaintext1, kThreshold, 5);
  // 20 * 5 observations of plaintext1 on day 1.
  AddObservations(&forculus_analyzer, 1, DAY, plaintext1, kThreshold, 5);

  // 21 * 6 observations of plaintext2 on day 0.
  AddObservations(&forculus_analyzer, 0, DAY, plaintext2, kThreshold + 1, 6);
  // 19 * 6 observations of plaintext2 on day 1. These will not be decrypted.
  AddObservations(&forculus_analyzer, 1, DAY, plaintext2, kThreshold - 1, 6);

  // 19 * 7 observations of plaintext3 on day 0. These will not be decrypted.
  AddObservations(&forculus_analyzer, 0, DAY, plaintext3, kThreshold - 1, 7);
  // 19 * 7 observations of plaintext3 on day 1. These will not be decrypted.
  AddObservations(&forculus_analyzer, 1, DAY, plaintext3, kThreshold - 1, 7);

  // 22 * 8 observations of plaintext4 on day 3.
  AddObservations(&forculus_analyzer, 3, DAY, plaintext4, kThreshold + 2, 8);

  EXPECT_EQ(0u, forculus_analyzer.observation_errors());
  static const size_t kExpectedNumObservations =
      kThreshold * 5 + kThreshold * 5 + (kThreshold + 1) * 6 + (kThreshold - 1) * 6 +
      (kThreshold - 1) * 7 + (kThreshold - 1) * 7 + (kThreshold + 2) * 8;
  EXPECT_EQ(kExpectedNumObservations, forculus_analyzer.num_observations());
  auto results = forculus_analyzer.TakeResults();

  // We should have decrypted plaintexts 1, 2 and 4.
  EXPECT_EQ(3u, results.size());

  // Check plaintext1
  EXPECT_EQ(kThreshold * 5 + kThreshold * 5, results[plaintext1]->total_count);
  EXPECT_EQ(2u, results[plaintext1]->num_epochs);

  // Check plaintext2
  EXPECT_EQ((kThreshold + 1) * 6, results[plaintext2]->total_count);
  EXPECT_EQ(1u, results[plaintext2]->num_epochs);

  // Check plaintext4
  EXPECT_EQ((kThreshold + 2) * 8, results[plaintext4]->total_count);
  EXPECT_EQ(1u, results[plaintext4]->num_epochs);

  // Plaintext3 should not be decrypted.
  EXPECT_EQ(nullptr, results[plaintext3]);
}

// We test Forculus encryption and analysis using DAY, WEEK and MONTH epochs.
TEST(ForculusAnalyzerTest, TestEpochTypes) {
  ForculusConfig forculus_config;
  forculus_config.set_threshold(kThreshold);
  std::unique_ptr<ForculusAnalyzer> forculus_analyzer(new ForculusAnalyzer(forculus_config));

  const std::string plaintext("Some text");

  // First we test with a DAY epoch, the default.

  // Add 10 observations on day 0,
  AddObservations(forculus_analyzer.get(), 0, DAY, plaintext, kThreshold - 10, 1);
  // and 10 observations on day 1.
  AddObservations(forculus_analyzer.get(), 1, DAY, plaintext, 10, 1);

  // Since day 0 and day 1 are different epochs we should not have decrypted the
  // plaintext.
  auto results = forculus_analyzer->TakeResults();
  EXPECT_EQ(0u, results.size());

  // Next we test with a WEEK epoch
  forculus_config.set_epoch_type(WEEK);
  forculus_analyzer = std::make_unique<ForculusAnalyzer>(forculus_config);

  // Add 10 observations on day 0,
  AddObservations(forculus_analyzer.get(), 0, WEEK, plaintext, kThreshold - 10, 1);
  // and 10 observations on day 1.
  AddObservations(forculus_analyzer.get(), 1, WEEK, plaintext, 10, 1);

  // Since day 0 and day 1 are in the same epoch we should have decrypted the
  // plaintext.
  results = forculus_analyzer->TakeResults();
  EXPECT_EQ(1u, results.size());

  // Next we test with a WEEK epoch but two days in different weeks.
  forculus_analyzer = std::make_unique<ForculusAnalyzer>(forculus_config);

  // Add 10 observations on day 0,
  AddObservations(forculus_analyzer.get(), 0, WEEK, plaintext, kThreshold - 10, 1);
  // and 10 observations on day 7.
  AddObservations(forculus_analyzer.get(), 7, WEEK, plaintext, 10, 1);

  // Since day 0 and day 7 are different epochs we should not have decrypted the
  // plaintext.
  results = forculus_analyzer->TakeResults();
  EXPECT_EQ(0u, results.size());

  // Next we test with a MONTH epoch
  forculus_config.set_epoch_type(MONTH);
  forculus_analyzer = std::make_unique<ForculusAnalyzer>(forculus_config);

  // Add 10 observations on day 0,
  AddObservations(forculus_analyzer.get(), 0, MONTH, plaintext, kThreshold - 10, 1);
  // and 10 observations on day 7.
  AddObservations(forculus_analyzer.get(), 7, MONTH, plaintext, 10, 1);

  // Since day 0 and day 7 are in the same epoch we should have decrypted the
  // plaintext.
  results = forculus_analyzer->TakeResults();
  EXPECT_EQ(1u, results.size());

  // Finally we test with a MONTH epoch but two days in different months.
  forculus_config.set_epoch_type(MONTH);
  forculus_analyzer = std::make_unique<ForculusAnalyzer>(forculus_config);

  // Add 10 observations on day 0,
  AddObservations(forculus_analyzer.get(), 0, MONTH, plaintext, kThreshold - 10, 1);
  // and 10 observations on day 31.
  // NOLINTNEXTLINE
  AddObservations(forculus_analyzer.get(), 31, MONTH, plaintext, 10, 1);

  // Since day 0 and day 31 are in different epochs we should not have decrypted
  // the  plaintext.
  results = forculus_analyzer->TakeResults();
  EXPECT_EQ(0u, results.size());
}

// Tests the use of a ForculusAnalyzer when fed observations with errors.
TEST(ForculusAnalyzerTest, WithErrors) {
  ForculusConfig forculus_config;
  forculus_config.set_threshold(3);
  ForculusAnalyzer forculus_analyzer(forculus_config);

  ForculusObservation obs;
  obs.set_ciphertext("1 ciphertext fake");
  obs.set_point_x("1 x fake");
  obs.set_point_y("1 y fake");

  // Add an observation.
  EXPECT_TRUE(forculus_analyzer.AddObservation(0, obs));
  EXPECT_EQ(1u, forculus_analyzer.num_observations());
  EXPECT_EQ(0u, forculus_analyzer.observation_errors());

  // Now add another observation with the same ciphertext and x-value
  // but a different y-value. This causes an error.
  obs.set_point_y("2 y fake");
  EXPECT_FALSE(forculus_analyzer.AddObservation(0, obs));
  EXPECT_EQ(1u, forculus_analyzer.num_observations());
  EXPECT_EQ(1u, forculus_analyzer.observation_errors());

  // The whole ciphertext is considered corupt now so even changing to
  // a differnt x value still yields an error.
  obs.set_point_x("2 x fake");
  EXPECT_FALSE(forculus_analyzer.AddObservation(0, obs));
  EXPECT_EQ(1u, forculus_analyzer.num_observations());
  EXPECT_EQ(2u, forculus_analyzer.observation_errors());

  // Adding an observation for a different epoch succeeds.
  obs.set_point_x("1 x fake");
  obs.set_point_y("1 y fake");
  EXPECT_TRUE(forculus_analyzer.AddObservation(1, obs));
  EXPECT_EQ(2u, forculus_analyzer.num_observations());
  EXPECT_EQ(2u, forculus_analyzer.observation_errors());

  // Adding a second obseration for epoch 1 also succeeds.
  obs.set_point_x("2 x fake");
  obs.set_point_y("2 y fake");
  EXPECT_TRUE(forculus_analyzer.AddObservation(1, obs));
  EXPECT_EQ(3u, forculus_analyzer.num_observations());
  EXPECT_EQ(2u, forculus_analyzer.observation_errors());

  // Adding a third obseration for epoch 1 invokes a decryption which fails.
  obs.set_point_x("3 x fake");
  obs.set_point_y("3 y fake");
  EXPECT_FALSE(forculus_analyzer.AddObservation(1, obs));
  EXPECT_EQ(3u, forculus_analyzer.num_observations());
  EXPECT_EQ(3u, forculus_analyzer.observation_errors());

  // Adding a fourth obseration for epoch 1 also fails.
  obs.set_point_x("4 x fake");
  obs.set_point_y("4 y fake");
  EXPECT_FALSE(forculus_analyzer.AddObservation(1, obs));
  EXPECT_EQ(3u, forculus_analyzer.num_observations());
  EXPECT_EQ(4u, forculus_analyzer.observation_errors());

  // There should be no results.
  auto results = forculus_analyzer.TakeResults();
  EXPECT_EQ(0u, results.size());
}

}  // namespace forculus
}  // namespace cobalt

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  INIT_LOGGING(argv[0]);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

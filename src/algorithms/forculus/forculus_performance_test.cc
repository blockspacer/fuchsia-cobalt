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

#include <chrono>
#include <ctime>
#include <fstream>
#include <streambuf>

#include <gflags/gflags.h>

#include "./logging.h"
#include "encoder/client_secret.h"
#include "src/algorithms/forculus/forculus_analyzer.h"
#include "src/algorithms/forculus/forculus_encrypter.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace forculus {

using encoder::ClientSecret;

static const uint32_t kThreshold = 20;

namespace {

ForculusObservation Encrypt(const std::string& plaintext, double* wall_timer, double* cpu_timer) {
  // Make a config with the given threshold
  ForculusConfig config;
  config.set_threshold(kThreshold);
  config.set_epoch_type(DAY);

  // Construct an Encrypter.
  ForculusEncrypter encrypter(config, 0, 0, 0, "", ClientSecret::GenerateNewSecret());

  // Invoke Encrypt() and check the status.
  ForculusObservation obs;
  auto c_start = std::clock();
  auto t_start = std::chrono::high_resolution_clock::now();
  EXPECT_EQ(ForculusEncrypter::kOK, encrypter.Encrypt(plaintext, 0, &obs));
  std::clock_t c_end = std::clock();
  auto t_end = std::chrono::high_resolution_clock::now();
  *wall_timer += std::chrono::duration<double>(t_end - t_start).count();
  *cpu_timer += static_cast<double>(c_end - c_start);
  return obs;
}

void AddObservations(ForculusAnalyzer* forculus_analyzer, const std::string& plaintext,
                     int num_clients, double* encryption_wall_time, double* encryption_cpu_time,
                     double* decryption_wall_time, double* decryption_cpu_time) {
  // Simulate num_clients different clients.
  for (int i = 0; i < num_clients; i++) {
    auto observation = Encrypt(plaintext, encryption_wall_time, encryption_cpu_time);
    auto c_start = std::clock();
    auto t_start = std::chrono::high_resolution_clock::now();
    EXPECT_TRUE(forculus_analyzer->AddObservation(0, observation));
    std::clock_t c_end = std::clock();
    auto t_end = std::chrono::high_resolution_clock::now();
    *decryption_wall_time += std::chrono::duration<double>(t_end - t_start).count();
    *decryption_cpu_time += static_cast<double>(c_end - c_start);
  }
}

}  // namespace

// TODO(rudominer) The tests in this file find the text files to read using
// file paths that are expressed relative to the Cobalt source root directory.
// This technique works because when the tests are run via the Python script
// cobaltb.py then the current working directory is that root directory.
// This technique is fragile. We should instead pass the path to the root
// directory as a command-line argument into the test. Currently there is no
// Cobalt infrastructure for passing command-line arguments to unit tests.

// Reads the text file word_counts.txt containing words and counts. For each
// (word, count) pair constructs |count| independent Forculus Observations
// of |word|. Passes all of these Observations to a Forculus Analyzer and
// obtains the results. All together there will be one million Forculus
// Observations passed to the Forculus Analyzer. Prints out timing
// statistics at the end.
TEST(ForculusPerformanceTest, OneMillionObservations) {
  ForculusConfig forculus_config;
  forculus_config.set_threshold(kThreshold);
  ForculusAnalyzer forculus_analyzer(forculus_config);
  std::ifstream stream("src/algorithms/forculus/word_counts.txt", std::ifstream::in);
  std::string line;
  double encryption_wall_time = 0;
  double encryption_cpu_time = 0;
  double decryption_wall_time = 0;
  double decryption_cpu_time = 0;
  int num_rows = 0;
  while (std::getline(stream, line)) {
    num_rows++;
    std::istringstream line_stream(line);
    std::string word;
    line_stream >> word;
    int count;
    line_stream >> count;
    AddObservations(&forculus_analyzer, word, count, &encryption_wall_time, &encryption_cpu_time,
                    &decryption_wall_time, &decryption_cpu_time);
  }

  // The number of rows in the file word_counts.txt.
  static const int kExpectedNumRows = 57792;
  // There are one million observations
  static const size_t kExpectedNumObservations = 1000000;
  // The number of rows of word_counts.txt in which the count is at least 20.
  static const size_t kExpectedNumResults = 5331;

  EXPECT_EQ(kExpectedNumRows, num_rows);
  EXPECT_EQ(kExpectedNumObservations, forculus_analyzer.num_observations());
  auto results = forculus_analyzer.TakeResults();

  EXPECT_EQ(kExpectedNumResults, results.size());

  EXPECT_EQ(0u, forculus_analyzer.observation_errors());

  std::cout << "\n=================================================\n";
  std::cout << "Rows read: " << num_rows << std::endl;
  std::cout << "Plaintexts encrypted: " << kExpectedNumObservations << std::endl;
  std::cout << "Ciphertexts decrypted: " << results.size() << std::endl;
  std::cout << "Total encryption wall time: " << encryption_wall_time << " seconds.\n";
  std::cout << "Total encryption cpu time: " << encryption_cpu_time / CLOCKS_PER_SEC
            << " seconds.\n";
  std::cout << "Total decryption wall time: " << decryption_wall_time << " seconds.\n";
  std::cout << "Total decryption cpu time: " << decryption_cpu_time / CLOCKS_PER_SEC
            << " seconds.\n";
  std::cout << "\n=================================================\n";
}

}  // namespace forculus
}  // namespace cobalt

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  INIT_LOGGING(argv[0]);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "algorithms/rappor/rappor_analyzer_test.h"

namespace cobalt::rappor {

// Comparison of Analyze and simple least squares.
// It invokes Analyze() in a few very simple cases, checks that the the
// algorithm converges and that the result vector has the correct size. For each
// case, it also computes the least squares solution using QR for exactly the
// same system and prints both solutions (note that the least squares solution
// is not always unique).
TEST_F(RapporAnalyzerTest, CompareAnalyzeToRegression) {
  static const uint32_t kNumCandidates = 10;
  static const uint32_t kNumCohorts = 2;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 8;

  std::vector<int> candidate_indices(100, 5);  // NOLINT
  std::vector<int> true_candidate_counts = {0,   0, 0, 0, 0,
                                            100, 0, 0, 0, 0};  // NOLINT
  CompareAnalyzeToSimpleRegression("p=0, q=1, only candidate 5", kNumCandidates,
                                   kNumBloomBits, kNumCohorts, kNumHashes,
                                   candidate_indices, true_candidate_counts);

  candidate_indices = std::vector<int>(20, 1);                // NOLINT
  candidate_indices.insert(candidate_indices.end(), 20, 4);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 60, 9);   // NOLINT
  true_candidate_counts = {0, 20, 0, 0, 20, 0, 0, 0, 0, 60};  // NOLINT
  CompareAnalyzeToSimpleRegression(
      "p=0, q=1, several candidates", kNumCandidates, kNumBloomBits,
      kNumCohorts, kNumHashes, candidate_indices, true_candidate_counts);

  prob_0_becomes_1_ = 0.1;  // NOLINT
  prob_1_stays_1_ = 0.9;    // NOLINT

  candidate_indices = std::vector<int>(100, 5);              // NOLINT
  true_candidate_counts = {0, 0, 0, 0, 0, 100, 0, 0, 0, 0};  // NOLINT
  CompareAnalyzeToSimpleRegression(
      "p=0.1, q=0.9, only candidate 5", kNumCandidates, kNumBloomBits,
      kNumCohorts, kNumHashes, candidate_indices, true_candidate_counts);

  candidate_indices = std::vector<int>(20, 1);                // NOLINT
  candidate_indices.insert(candidate_indices.end(), 20, 4);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 60, 9);   // NOLINT
  true_candidate_counts = {0, 20, 0, 0, 20, 0, 0, 0, 0, 60};  // NOLINT
  CompareAnalyzeToSimpleRegression(
      "p=0.1, q=0.9, several candidates", kNumCandidates, kNumBloomBits,
      kNumCohorts, kNumHashes, candidate_indices, true_candidate_counts);
}

// Runs LongExperimentWithAnalyze; the true candidate counts are
// distributed according to the power law; we specify the number of
// observations and the exponent parameter of the power law. The ids are then
// shuffled so that it is not true that large ids are more frequent.
// Note: encoding observations is time consuming so large tests may take long.
TEST_F(RapporAnalyzerTest, PowerLawExperiment) {
  static const uint32_t kNumCandidates = 20000;
  static const uint32_t kNumCohorts = 128;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 128;
  static const uint32_t kNumObservations = 1e+6;
  static const bool print_estimates = false;
  const double exponent = 30;
  const auto max_id = static_cast<const int>(kNumCandidates - 1);

  std::vector<int> candidate_indices(kNumObservations);
  std::vector<int> true_candidate_counts(kNumCandidates, 0);

  // Create a "map" of shuffled ids to randomize the observed id values
  std::vector<int> candidate_ids_list_shuffled(kNumCandidates);
  std::iota(candidate_ids_list_shuffled.begin(),
            candidate_ids_list_shuffled.end(), 0);
  std::mt19937 g(random_dev_());
  std::shuffle(candidate_ids_list_shuffled.begin(),
               candidate_ids_list_shuffled.end(), g);

  // Generate observations from the power law distribution on
  // [0,kNumCandidates-1]
  const double left = 0.0;
  const auto right = static_cast<const double>(max_id);
  for (uint32_t i = 0; i < kNumObservations; i++) {
    double random_power_law_number =
        GenerateNumberFromPowerLaw(left, right, exponent);
    auto observed_candidate_id = static_cast<int>(random_power_law_number);
    observed_candidate_id = std::min(observed_candidate_id, max_id);
    observed_candidate_id = candidate_ids_list_shuffled[observed_candidate_id];
    candidate_indices[i] = observed_candidate_id;
    true_candidate_counts[observed_candidate_id]++;
  }

  prob_0_becomes_1_ = 0.05;  // NOLINT
  prob_1_stays_1_ = 0.95;    // NOLINT

  LongExperimentWithAnalyze("p=0.05, q=0.95, power-law distribution",
                            kNumCandidates, kNumBloomBits, kNumCohorts,
                            kNumHashes, candidate_indices,
                            true_candidate_counts, print_estimates);

  prob_0_becomes_1_ = 0.25;  // NOLINT
  prob_1_stays_1_ = 0.75;    // NOLINT

  LongExperimentWithAnalyze("p=0.25, q=0.75, power-law distribution",
                            kNumCandidates, kNumBloomBits, kNumCohorts,
                            kNumHashes, candidate_indices,
                            true_candidate_counts, print_estimates);
}

// This is the same as PowerLawExperiment but the distribution of observations
// is exponential.
TEST_F(RapporAnalyzerTest, ExponentialExperiment) {
  static const uint32_t kNumCandidates = 300;
  static const uint32_t kNumCohorts = 2;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 128;
  static const uint32_t kNumObservations = 1e+6;
  static const bool print_estimates = true;
  static const double lambda = 1.0;  // the support of pdf for lambda == 1.0 ...
  static const double approximate_max_generated_num =
      6.0;  // ... is roughly [0, 6.0]
  const auto max_id = static_cast<const int>(kNumCandidates - 1);

  std::vector<int> candidate_indices(kNumObservations);
  std::vector<int> true_candidate_counts(kNumCandidates, 0);

  // Create a "map" of shuffled ids to randomize the observed id values
  std::vector<int> candidate_ids_list_shuffled =
      GenerateRandomMapOfIds(kNumCandidates);

  // Generate observations from the exponential distribution on
  // [0,kNumCandidates-1]
  std::exponential_distribution<double> exp_distribution(lambda);
  for (uint32_t i = 0; i < kNumObservations; i++) {
    double random_exponential_number = exp_distribution(random_dev_);
    random_exponential_number /= approximate_max_generated_num;
    random_exponential_number *= static_cast<double>(kNumCandidates);
    auto observed_candidate_id = static_cast<int>(random_exponential_number);
    observed_candidate_id = std::min(observed_candidate_id, max_id);
    observed_candidate_id = candidate_ids_list_shuffled[observed_candidate_id];
    candidate_indices[i] = observed_candidate_id;
    true_candidate_counts[observed_candidate_id]++;
  }

  prob_0_becomes_1_ = 0.05;  // NOLINT
  prob_1_stays_1_ = 0.95;    // NOLINT

  LongExperimentWithAnalyze("p=0.05, q=0.95, exponential distribution",
                            kNumCandidates, kNumBloomBits, kNumCohorts,
                            kNumHashes, candidate_indices,
                            true_candidate_counts, print_estimates);

  prob_0_becomes_1_ = 0.25;  // NOLINT
  prob_1_stays_1_ = 0.75;    // NOLINT

  LongExperimentWithAnalyze("p=0.25, q=0.75, exponential distribution",
                            kNumCandidates, kNumBloomBits, kNumCohorts,
                            kNumHashes, candidate_indices,
                            true_candidate_counts, print_estimates);
}

// This is the same as PowerLawExperiment but the distribution of observations
// comes from normal distribution.
TEST_F(RapporAnalyzerTest, NormalDistExperiment) {
  static const uint32_t kNumCandidates = 100;
  static const uint32_t kNumCohorts = 2;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 32;
  static const uint32_t kNumObservations = 1e+5;
  static const bool print_estimates = true;
  const auto mean = static_cast<double>(kNumCandidates) / 2.0;
  // Most probablility weight is within +/- 3 standard deviations.
  const auto sd = static_cast<const double>(mean / 10);
  const auto max_id = static_cast<const int>(kNumCandidates - 1);

  std::vector<int> candidate_indices(kNumObservations);
  std::vector<int> true_candidate_counts(kNumCandidates, 0);

  // Create a "map" of shuffled ids to randomize the observed id values.
  std::vector<int> candidate_ids_list_shuffled =
      GenerateRandomMapOfIds(kNumCandidates);

  // Generate observations from the normal distribution.
  std::normal_distribution<double> nrm_distribution(mean, sd);
  for (uint32_t i = 0; i < kNumObservations; i++) {
    double random_normal_number = nrm_distribution(random_dev_);
    int observed_candidate_id = static_cast<int>(random_normal_number);
    observed_candidate_id =
        std::max(0, std::min(observed_candidate_id, max_id));
    observed_candidate_id = candidate_ids_list_shuffled[observed_candidate_id];
    candidate_indices[i] = observed_candidate_id;
    true_candidate_counts[observed_candidate_id]++;
  }

  prob_0_becomes_1_ = 0.05;  // NOLINT
  prob_1_stays_1_ = 0.95;    // NOLINT

  LongExperimentWithAnalyze("p=0.05, q=0.95, normal distribution",
                            kNumCandidates, kNumBloomBits, kNumCohorts,
                            kNumHashes, candidate_indices,
                            true_candidate_counts, print_estimates);

  prob_0_becomes_1_ = 0.25;  // NOLINT
  prob_1_stays_1_ = 0.75;    // NOLINT

  LongExperimentWithAnalyze("p=0.25, q=0.75, normal distribution",
                            kNumCandidates, kNumBloomBits, kNumCohorts,
                            kNumHashes, candidate_indices,
                            true_candidate_counts, print_estimates);
}

// This is the same as PowerLawExperiment but the observations come from a
// uniform distribution supported on some small set of candidates.
TEST_F(RapporAnalyzerTest, KOutOfNExperiment) {
  // For this test to be meaningful we  should have kNumObservedCandidates <<
  // kNumCandidates
  static const uint32_t kNumCandidates = 2000;
  static const uint32_t kNumObservedCandidates = 10;
  static const uint32_t kNumCohorts = 50;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 32;
  static const uint32_t kNumObservations = 1e+5;
  static const bool print_estimates = true;
  const int max_id = static_cast<const int>(kNumCandidates - 1);

  std::vector<int> candidate_indices(kNumObservations);
  std::vector<int> true_candidate_counts(kNumCandidates, 0);

  // Create a "map" of shuffled ids to randomize the observed id values
  std::vector<int> candidate_ids_list_shuffled =
      GenerateRandomMapOfIds(kNumCandidates);

  // Generate observations
  for (uint32_t i = 0; i < kNumObservations; i++) {
    int observed_candidate_id = static_cast<int>(i % kNumObservedCandidates);
    observed_candidate_id = std::min(observed_candidate_id, max_id);
    observed_candidate_id = candidate_ids_list_shuffled[observed_candidate_id];
    candidate_indices[i] = observed_candidate_id;
    true_candidate_counts[observed_candidate_id]++;
  }

  prob_0_becomes_1_ = 0.05;  // NOLINT
  prob_1_stays_1_ = 0.95;    // NOLINT

  LongExperimentWithAnalyze("p=0.05, q=0.95, k out of N distribution",
                            kNumCandidates, kNumBloomBits, kNumCohorts,
                            kNumHashes, candidate_indices,
                            true_candidate_counts, print_estimates);

  prob_0_becomes_1_ = 0.25;  // NOLINT
  prob_1_stays_1_ = 0.75;    // NOLINT

  LongExperimentWithAnalyze("p=0.25, q=0.75, k out of N distribution",
                            kNumCandidates, kNumBloomBits, kNumCohorts,
                            kNumHashes, candidate_indices,
                            true_candidate_counts, print_estimates);
}

}  // namespace cobalt::rappor

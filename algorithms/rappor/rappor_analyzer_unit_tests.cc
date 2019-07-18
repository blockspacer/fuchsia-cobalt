// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "algorithms/rappor/rappor_analyzer_test.h"

namespace cobalt::rappor {

using encoder::ClientSecret;

// Tests the function BuildCandidateMap. We build one small CandidateMap and
// then we explicitly check every value against a known value. We have not
// independently verified the SHA-256 hash values and so rather than a test
// of correctness this is firstly a sanity test: we can eyeball the values
// and confirm they look sane, and secondly a regression test.
TEST_F(RapporAnalyzerTest, BuildCandidateMapSmallTest) {
  static const uint32_t kNumCandidates = 5;
  static const uint32_t kNumCohorts = 3;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 8;

  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);
  BuildCandidateMap();

  // clang-format off
  int expected_bit_indices[kNumCandidates][kNumCohorts*kNumHashes] = {
  // cihj means cohort = i and hash-index = j.
  // c0h0 c0h1 c1h0 c1h1 c2h0 c2h2
      // NOLINTNEXTLINE
      {3,   5,   2,   6,   3,   6},  // candidate 0
      // NOLINTNEXTLINE
      {1,   5,   4,   7,   2,   0},  // candidate 1
      // NOLINTNEXTLINE
      {3,   0,   2,   0,   1,   4},  // candidate 2
      // NOLINTNEXTLINE
      {5,   1,   2,   4,   2,   4},  // candidate 3
      // NOLINTNEXTLINE
      {1,   4,   3,   1,   2,   6},  // candidate 4
  };
  // clang-format on

  for (size_t candidate = 0; candidate < kNumCandidates; candidate++) {
    for (size_t cohort = 0; cohort < kNumCohorts; cohort++) {
      for (size_t hash = 0; hash < kNumHashes; hash++) {
        EXPECT_EQ(expected_bit_indices[candidate][cohort * kNumHashes + hash],
                  GetCandidateMapValue(candidate, cohort, hash))
            << "(" << candidate << "," << cohort * kNumHashes + hash << ")";
      }
    }
  }

  // Check the associated sparse matrix.
  std::ostringstream stream;
  stream << candidate_matrix().block(0, 0, kNumCohorts * kNumBloomBits,
                                     kNumCandidates);
  const char* kExpectedMatrixString =
      "0 0 0 0 0 \n"
      "0 0 0 0 0 \n"
      "1 1 0 1 0 \n"
      "0 0 0 0 1 \n"
      "1 0 1 0 0 \n"
      "0 0 0 0 0 \n"
      "0 1 0 1 1 \n"
      "0 0 1 0 0 \n"
      "0 1 0 0 0 \n"
      "1 0 0 0 0 \n"
      "0 0 0 0 0 \n"
      "0 1 0 1 0 \n"
      "0 0 0 0 1 \n"
      "1 0 1 1 0 \n"
      "0 0 0 0 1 \n"
      "0 0 1 0 0 \n"
      "0 0 0 0 0 \n"
      "1 0 0 0 1 \n"
      "0 0 0 0 0 \n"
      "0 0 1 1 0 \n"
      "1 0 0 0 0 \n"
      "0 1 0 1 1 \n"
      "0 0 1 0 0 \n"
      "0 1 0 0 0 \n";
  EXPECT_EQ(kExpectedMatrixString, stream.str());
}

// This test is identical to the previous test except that kNumBloomBits = 4
// instead of 8. The purpose of this test is to force the situation in which
// the two hash functions for a given cohort and a given candidate give the
// same value. For example below we see that for candidate 0, cohort 1, both
// hash functions yielded a 2. We want to test that the associated sparse
// matrix has a "1" in the corresponding position (in this case that is
// row 5, column 0) and does not have a "2" in that position. In other words
// we want to test that we correctly added only one entry to the list of
// triples that defined the sparse matrix and not two entries.
TEST_F(RapporAnalyzerTest, BuildCandidateMapSmallTestWithDuplicates) {
  static const uint32_t kNumCandidates = 5;
  static const uint32_t kNumCohorts = 3;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 4;

  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);
  BuildCandidateMap();

  // clang-format off
  int expected_bit_indices[kNumCandidates][kNumCohorts*kNumHashes] = {
  // cihj means cohort = i and hash-index = j.
  // c0h0 c0h1 c1h0 c1h1 c2h0 c2h2
      {3,   1,   2,   2,   3,   2},  // candidate 0
      {1,   1,   0,   3,   2,   0},  // candidate 1
      {3,   0,   2,   0,   1,   0},  // candidate 2
      {1,   1,   2,   0,   2,   0},  // candidate 3
      {1,   0,   3,   1,   2,   2},  // candidate 4
  };
  // clang-format on

  for (size_t candidate = 0; candidate < kNumCandidates; candidate++) {
    for (size_t cohort = 0; cohort < kNumCohorts; cohort++) {
      for (size_t hash = 0; hash < kNumHashes; hash++) {
        EXPECT_EQ(expected_bit_indices[candidate][cohort * kNumHashes + hash],
                  GetCandidateMapValue(candidate, cohort, hash))
            << "(" << candidate << "," << cohort * kNumHashes + hash << ")";
      }
    }
  }

  // Check the associated sparse matrix.
  std::ostringstream stream;
  stream << candidate_matrix().block(0, 0, kNumCohorts * kNumBloomBits,
                                     kNumCandidates);
  const char* kExpectedMatrixString =
      "1 0 1 0 0 \n"
      "0 0 0 0 0 \n"
      "1 1 0 1 1 \n"
      "0 0 1 0 1 \n"
      "0 1 0 0 1 \n"
      "1 0 1 1 0 \n"
      "0 0 0 0 1 \n"
      "0 1 1 1 0 \n"
      "1 0 0 0 0 \n"
      "1 1 0 1 1 \n"
      "0 0 1 0 0 \n"
      "0 1 1 1 0 \n";
  EXPECT_EQ(kExpectedMatrixString, stream.str());
}

// Tests the function BuildCandidateMap. We build many different CandidateMaps
// with many different parameters. We are testing firstly that the procedure
// completes without error, secondly that the shape of the produced
// data structure is correct and thirdly that the bit indexes are in the range
// [0, num_bloom_bits). The latter two checks occur inside of
// BuildCandidateMap.
TEST_F(RapporAnalyzerTest, BuildCandidateMapSmokeTest) {
  for (auto num_candidates : {11, 51, 99}) {
    for (auto num_cohorts : {23, 45}) {
      for (auto num_hashes : {2, 6, 7}) {
        for (auto num_bloom_bits : {16, 128}) {
          SetAnalyzer(num_candidates, num_bloom_bits, num_cohorts, num_hashes);
          BuildCandidateMap();
        }
      }
    }
  }
}

// Tests the function BuildCandidateMap. We test that the map that is built
// is consistent with the Bloom filters that are built by an encoder.
TEST_F(RapporAnalyzerTest, BuildCandidateMapCompareWithEncoder) {
  static const uint32_t kNumCandidates = 10;
  static const uint32_t kNumCohorts = 20;
  static const uint32_t kNumHashes = 5;
  static const uint32_t kNumBloomBits = 64;

  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);
  BuildCandidateMap();

  for (size_t candidate = 0; candidate < kNumCandidates; candidate++) {
    // Construct a new encoder with a new ClientSecret so that a random
    // cohort is selected.
    RapporEncoder encoder(config_, ClientSecret::GenerateNewSecret());

    // Encode the current candidate string using |encoder|.
    ValuePart value_part;
    value_part.set_string_value(CandidateString(candidate));
    RapporObservation observation;
    encoder.Encode(value_part, &observation);

    // Since p=0 and q=1 the RapporObservation contains the raw Bloom filter
    // with no noise added. Confirm that the BloomFilter is the same as
    // the one implied by the CandidateMap at the appropriate candidate
    // and cohort.
    EXPECT_EQ(BuildBitString(candidate, encoder.cohort()),
              DataToBinaryString(observation.data()));
  }
}

// Tests the function ExtractEstimatedBitCountRatios(). We build one small
// estimated bit count ratio vector and explicitly check its values. We
// use no-randomness: p = 0, q = 1 so that the estimated bit counts are
// identical to the true bit counts.
TEST_F(RapporAnalyzerTest, ExtractEstimatedBitCountRatiosSmallNonRandomTest) {
  static const uint32_t kNumCandidates = 10;
  static const uint32_t kNumCohorts = 3;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 8;
  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);
  AddObservation(0, "00001010");
  AddObservation(0, "00010010");
  AddObservation(1, "00001010");
  AddObservation(1, "00010010");
  AddObservation(1, "00100010");
  AddObservation(2, "00001010");
  AddObservation(2, "00010010");
  AddObservation(2, "00010010");
  AddObservation(2, "00100010");

  Eigen::VectorXd est_bit_count_ratios;
  ExtractEstimatedBitCountRatios(&est_bit_count_ratios);

  std::ostringstream stream;
  stream << est_bit_count_ratios.block(0, 0, kNumCohorts * kNumBloomBits, 1);

  const char* kExpectedVectorString =
      "       0\n"
      "       0\n"
      "       0\n"
      "     0.5\n"
      "     0.5\n"
      "       0\n"
      "       1\n"
      "       0\n"
      "       0\n"
      "       0\n"
      "0.333333\n"
      "0.333333\n"
      "0.333333\n"
      "       0\n"
      "       1\n"
      "       0\n"
      "       0\n"
      "       0\n"
      "    0.25\n"
      "     0.5\n"
      "    0.25\n"
      "       0\n"
      "       1\n"
      "       0";
  EXPECT_EQ(kExpectedVectorString, stream.str());
}

// This test invokes Analyze() in a few very simple cases, checks that the the
// algorithm converges and that the result vector has the correct size.
TEST_F(RapporAnalyzerTest, SimpleAnalyzeTest) {
  static const uint32_t kNumCandidates = 10;
  static const uint32_t kNumCohorts = 3;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 8;
  static const bool print_estimates = false;

  std::vector<int> candidate_indices(100, 5);  // NOLINT
  std::vector<int> true_candidate_counts = {0,   0, 0, 0, 0,
                                            100, 0, 0, 0, 0};  // NOLINT
  ShortExperimentWithAnalyze(
      "p=0, q=1, only candidate 5", kNumCandidates, kNumBloomBits, kNumCohorts,
      kNumHashes, candidate_indices, true_candidate_counts, print_estimates);

  candidate_indices = std::vector<int>(20, 1);                // NOLINT
  candidate_indices.insert(candidate_indices.end(), 20, 4);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 60, 9);   // NOLINT
  true_candidate_counts = {0, 20, 0, 0, 20, 0, 0, 0, 0, 60};  // NOLINT
  ShortExperimentWithAnalyze("p=0, q=1, several candidates", kNumCandidates,
                             kNumBloomBits, kNumCohorts, kNumHashes,
                             candidate_indices, true_candidate_counts,
                             print_estimates);

  prob_0_becomes_1_ = 0.1;  // NOLINT
  prob_1_stays_1_ = 0.9;    // NOLINT

  candidate_indices = std::vector<int>(100, 5);              // NOLINT
  true_candidate_counts = {0, 0, 0, 0, 0, 100, 0, 0, 0, 0};  // NOLINT
  ShortExperimentWithAnalyze("p=0.1, q=0.9, only candidate 5", kNumCandidates,
                             kNumBloomBits, kNumCohorts, kNumHashes,
                             candidate_indices, true_candidate_counts,
                             print_estimates);

  candidate_indices = std::vector<int>(20, 1);                // NOLINT
  candidate_indices.insert(candidate_indices.end(), 20, 4);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 60, 9);   // NOLINT
  true_candidate_counts = {0, 20, 0, 0, 20, 0, 0, 0, 0, 60};  // NOLINT
  ShortExperimentWithAnalyze("p=0.1, q=0.9, several candidates", kNumCandidates,
                             kNumBloomBits, kNumCohorts, kNumHashes,
                             candidate_indices, true_candidate_counts,
                             print_estimates);
}

// Runs Analyze() on the corner-case of one candidate and one cohort.
// The result should be exact.
TEST_F(RapporAnalyzerTest, SingleCandidateTest) {
  static const uint32_t kNumCandidates = 1;
  static const uint32_t kNumCohorts = 1;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 8;

  // No noise introduced
  prob_0_becomes_1_ = 0.0;
  prob_1_stays_1_ = 1.0;

  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);

  std::vector<int> candidate_indices(100, 0);  // NOLINT
  AddObservationsForCandidates(candidate_indices);

  std::vector<CandidateResult> results;
  auto status = analyzer_->Analyze(&results);

  EXPECT_EQ(results.size(), kNumCandidates);
  EXPECT_GE(results[0].count_estimate, 99);
  EXPECT_LE(results[0].count_estimate, 101);
}

// Runs Analyze with one cohort and the number of bits much larger than the
// number of candidates. No noise is introduced. The heavy hitter should be
// reproduced nearly exactly (assuming second step of RAPPOR is "on" and
// working). Note(bazyli): this test is based on the assumption that the number
// of bits in the cohort is so large that no or almost no of the candidates will
// share a non-zero bit with the heavy hitter.
TEST_F(RapporAnalyzerTest, ExactSolutionTest) {
  // The probability that the heaviest hitter will not share a nonzero bit with
  // any other candidate when kNumCandidates = 10, kNumCohorts = 1, kNumHashes =
  // 2, kNumBloomBits = 128, is (125 * 126 / (127 * 128))^ 9 = 0.75. In that
  // case, the heaviest hitter must be solved exactly. Even if it does share a
  // bit or two with some other candidate(s), this is unlikely to alter the
  // result significantly.
  static const uint32_t kNumCandidates = 10;
  static const uint32_t kNumCohorts = 1;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 128;

  // No noise introduced.
  prob_0_becomes_1_ = 0.0;
  prob_1_stays_1_ = 1.0;

  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);

  std::vector<int> candidate_indices(10, 0);                  // NOLINT
  candidate_indices.insert(candidate_indices.end(), 10, 1);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 10, 2);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 10, 3);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 10, 4);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 100, 9);  // NOLINT
  AddObservationsForCandidates(candidate_indices);

  std::vector<CandidateResult> results;
  auto status = analyzer_->Analyze(&results);

  // Check that the heavy hitter is reproduced almost exactly
  EXPECT_GE(results[9].count_estimate, 95);
  EXPECT_LE(results[9].count_estimate, 105);

  // Candidates that are not observed should have zero counts
  EXPECT_EQ(results[5].count_estimate, 0);
  EXPECT_EQ(results[6].count_estimate, 0);
  EXPECT_EQ(results[7].count_estimate, 0);
  EXPECT_EQ(results[8].count_estimate, 0);

  // All standard errors should be numerically 0
  for (uint32_t i = 0; i < kNumCandidates; i++) {
    EXPECT_LE(results[i].std_error, 1e-12);
  }
}

// Runs Analyzer with two cohorts and large number of bits. Very small noise is
// introduced. The heaviest hitter should be reproduced very well (assuming
// second step of RAPPOR is "on" and working). This test is based on similar
// assumptions that ExactSolutionTest but the check is relaxed because of noise
// and multiple cohorts.
TEST_F(RapporAnalyzerTest, HighAccuracySolutionTest) {
  static const uint32_t kNumCandidates = 10;
  static const uint32_t kNumCohorts = 2;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 64;

  // Small noise introduced.
  prob_0_becomes_1_ = 0.1;  // NOLINT
  prob_1_stays_1_ = 0.9;    // NOLINT

  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);

  std::vector<int> candidate_indices(10, 0);                  // NOLINT
  candidate_indices.insert(candidate_indices.end(), 10, 1);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 10, 2);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 10, 3);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 10, 4);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 100, 9);  // NOLINT
  AddObservationsForCandidates(candidate_indices);

  std::vector<CandidateResult> results;
  auto status = analyzer_->Analyze(&results);

  EXPECT_GE(results[9].count_estimate, 70);
  EXPECT_LE(results[9].count_estimate, 120);
}

// Runs Analyzer on a moderately under-determined system (number of rows <
// number of columns) with more than one cohort. Small noise is introduced.
// There are a number of (equal) heavy hitters, which should be identified.
// TODO(bazyli): this test is heuristic.
TEST_F(RapporAnalyzerTest, KHeavyHittersTest) {
  static const uint32_t kNumCandidates = 100;
  static const uint32_t kNumCohorts = 4;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 16;

  // Small noise introduced
  prob_0_becomes_1_ = 0.1;  // NOLINT
  prob_1_stays_1_ = 0.9;    // NOLINT

  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);

  std::vector<int> candidate_indices;
  candidate_indices.insert(candidate_indices.end(), 100, 1);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 100, 2);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 100, 4);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 100, 8);   // NOLINT
  candidate_indices.insert(candidate_indices.end(), 100, 16);  // NOLINT
  candidate_indices.insert(candidate_indices.end(), 100, 32);  // NOLINT
  candidate_indices.insert(candidate_indices.end(), 100, 64);  // NOLINT
  AddObservationsForCandidates(candidate_indices);

  std::vector<CandidateResult> results;
  auto status = analyzer_->Analyze(&results);

  // Check if the heavy hitters are identified
  EXPECT_GE(results[1].count_estimate, 50);
  EXPECT_LE(results[1].count_estimate, 150);
  EXPECT_GE(results[2].count_estimate, 50);
  EXPECT_LE(results[2].count_estimate, 150);
  EXPECT_GE(results[4].count_estimate, 50);
  EXPECT_LE(results[4].count_estimate, 150);
  EXPECT_GE(results[8].count_estimate, 50);
  EXPECT_LE(results[8].count_estimate, 150);
  EXPECT_GE(results[16].count_estimate, 50);
  EXPECT_LE(results[16].count_estimate, 150);
  EXPECT_GE(results[32].count_estimate, 50);
  EXPECT_LE(results[32].count_estimate, 150);
}

// Runs a small instance of RAPPOR with large noise.
// The standard errors of identified heavy hitters should be nonzero.
// Note(bazyli): this test is heuristic test.
TEST_F(RapporAnalyzerTest, StandardErrorTest) {
  static const uint32_t kNumCandidates = 50;
  static const uint32_t kNumCohorts = 2;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 32;

  // Large noise introduced.
  prob_0_becomes_1_ = 0.4;  // NOLINT
  prob_1_stays_1_ = 0.6;    // NOLINT

  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);

  std::vector<int> candidate_indices(1000, 0);                  // NOLINT
  candidate_indices.insert(candidate_indices.end(), 1000, 10);  // NOLINT
  candidate_indices.insert(candidate_indices.end(), 1000, 20);  // NOLINT
  candidate_indices.insert(candidate_indices.end(), 1000, 30);  // NOLINT
  candidate_indices.insert(candidate_indices.end(), 1000, 40);  // NOLINT
  AddObservationsForCandidates(candidate_indices);

  std::vector<CandidateResult> results;
  auto status = analyzer_->Analyze(&results);

  EXPECT_GT(results[0].std_error, 0);
  EXPECT_GT(results[10].std_error, 0);
  EXPECT_GT(results[20].std_error, 0);
  EXPECT_GT(results[30].std_error, 0);
  EXPECT_GT(results[40].std_error, 0);
}

}  // namespace cobalt::rappor

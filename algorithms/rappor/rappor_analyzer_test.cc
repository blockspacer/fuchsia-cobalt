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
#include "algorithms/rappor/rappor_analyzer_test.h"

namespace cobalt {
namespace rappor {

using encoder::ClientSecret;

void RapporAnalyzerTest::SetAnalyzer(uint32_t num_candidates, uint32_t num_bloom_bits,
                                     uint32_t num_cohorts, uint32_t num_hashes) {
  PopulateRapporCandidateList(num_candidates, &candidate_list_);
  config_ = Config(num_bloom_bits, num_cohorts, num_hashes, prob_0_becomes_1_, prob_1_stays_1_);
  analyzer_ = std::make_unique<RapporAnalyzer>(config_, &candidate_list_);
}

void RapporAnalyzerTest::BuildCandidateMap() {
  EXPECT_EQ(grpc::OK, analyzer_->BuildCandidateMap().error_code());

  const uint32_t num_candidates = analyzer_->candidate_map_.candidate_list->candidates_size();
  const uint32_t num_cohorts = analyzer_->config_->num_cohorts();
  const uint32_t num_hashes = analyzer_->config_->num_hashes();
  const uint32_t num_bits = analyzer_->config_->num_bits();

  // Expect the number of candidates to be correct,
  EXPECT_EQ(num_candidates, analyzer_->candidate_map_.candidate_cohort_maps.size());

  // and for each candidate...
  for (size_t candidate = 0; candidate < num_candidates; candidate++) {
    // expect the number of cohorts to be correct,
    EXPECT_EQ(num_cohorts,
              analyzer_->candidate_map_.candidate_cohort_maps[candidate].cohort_hashes.size());

    // and for each cohort...
    for (size_t cohort = 0; cohort < num_cohorts; cohort++) {
      // expect the number of hashes to be correct,
      EXPECT_EQ(num_hashes, analyzer_->candidate_map_.candidate_cohort_maps[candidate]
                                .cohort_hashes[cohort]
                                .bit_indices.size());

      // and for each hash...
      for (size_t hash = 0; hash < num_hashes; hash++) {
        // Expect the bit index to be in the range [0, num_bits).
        auto bit_index = GetCandidateMapValue(candidate, cohort, hash);
        EXPECT_GE(bit_index, 0u);
        EXPECT_LT(bit_index, num_bits);
      }
    }
  }

  // Validate the associated sparse matrix.
  EXPECT_EQ(num_candidates, candidate_matrix().cols());
  EXPECT_EQ(num_cohorts * num_bits, candidate_matrix().rows());
  EXPECT_LE(num_candidates * num_cohorts, candidate_matrix().nonZeros());
  EXPECT_GE(num_candidates * num_cohorts * num_hashes, candidate_matrix().nonZeros());
}

uint16_t RapporAnalyzerTest::GetCandidateMapValue(uint16_t candidate_index, uint16_t cohort_index,
                                                  uint16_t hash_index) {
  EXPECT_GT(analyzer_->candidate_map_.candidate_cohort_maps.size(), candidate_index);
  EXPECT_GT(analyzer_->candidate_map_.candidate_cohort_maps[candidate_index].cohort_hashes.size(),
            cohort_index);
  EXPECT_GT(analyzer_->candidate_map_.candidate_cohort_maps[candidate_index]
                .cohort_hashes[cohort_index]
                .bit_indices.size(),
            hash_index);
  return analyzer_->candidate_map_.candidate_cohort_maps[candidate_index]
      .cohort_hashes[cohort_index]
      .bit_indices[hash_index];
}

std::string RapporAnalyzerTest::BuildBitString(uint16_t candidate_index, uint16_t cohort_index) {
  return BuildBinaryString(analyzer_->config_->num_bits(),
                           analyzer_->candidate_map_.candidate_cohort_maps[candidate_index]
                               .cohort_hashes[cohort_index]
                               .bit_indices);
}

void RapporAnalyzerTest::AddObservation(uint32_t cohort, const std::string& binary_string) {
  EXPECT_TRUE(analyzer_->AddObservation(RapporObservationFromString(cohort, binary_string)));
}

void RapporAnalyzerTest::ExtractEstimatedBitCountRatios(Eigen::VectorXd* est_bit_count_ratios) {
  std::vector<double> est_std_errors_not_used;
  EXPECT_TRUE(analyzer_
                  ->ExtractEstimatedBitCountRatiosAndStdErrors(est_bit_count_ratios,
                                                               &est_std_errors_not_used)
                  .ok());
}

void RapporAnalyzerTest::ExtractEstimatedBitCountRatiosAndStdErrors(
    Eigen::VectorXd* est_bit_count_ratios, std::vector<double>* est_std_errors) {
  EXPECT_TRUE(
      analyzer_->ExtractEstimatedBitCountRatiosAndStdErrors(est_bit_count_ratios, est_std_errors)
          .ok());
}

void RapporAnalyzerTest::AddObservationsForCandidates(const std::vector<int>& candidate_indices) {
  for (auto index : candidate_indices) {
    // Construct a new encoder with a new ClientSecret so that a random
    // cohort is selected.
    RapporEncoder encoder(config_, ClientSecret::GenerateNewSecret());

    // Encode the current candidate string using |encoder|.
    ValuePart value_part;
    value_part.set_string_value(CandidateString(index));
    RapporObservation observation;
    encoder.Encode(value_part, &observation);
    EXPECT_TRUE(analyzer_->AddObservation(observation));
  }
}

int RapporAnalyzerTest::GenerateNumberFromPowerLaw(const double left, const double right,
                                                   const double exponent) {
  // double precision must be used because of potentially large powers taken
  std::uniform_real_distribution<double> uniform_0_1_distribution(0.0, 1.0);
  double random_between_0_1 = uniform_0_1_distribution(random_dev_);
  double left_to_exponent_plus_1 = std::pow(left, exponent + 1);
  double random_power_law_number =
      (std::pow(right, exponent + 1) - left_to_exponent_plus_1) * random_between_0_1 +
      left_to_exponent_plus_1;
  random_power_law_number = std::pow(random_power_law_number, 1.0f / (exponent + 1));
  return static_cast<int>(random_power_law_number);
}

std::vector<int> RapporAnalyzerTest::GenerateRandomMapOfIds(const int num_candidates) {
  // Create a "map" of shuffled ids to randomize the observed id values
  std::vector<int> candidate_ids_list_shuffled(num_candidates);
  std::iota(candidate_ids_list_shuffled.begin(), candidate_ids_list_shuffled.end(), 0);
  std::mt19937 g(random_dev_());
  std::shuffle(candidate_ids_list_shuffled.begin(), candidate_ids_list_shuffled.end(), g);
  return candidate_ids_list_shuffled;
}

std::vector<int> RapporAnalyzerTest::CountsEstimatesFromResults(
    const std::vector<CandidateResult>& results) {
  uint32_t num_candidates = results.size();
  std::vector<int> count_estimates(num_candidates);
  for (size_t i = 0; i < num_candidates; i++) {
    count_estimates[i] = static_cast<int>(round(results[i].count_estimate));
  }
  return count_estimates;
}

Eigen::VectorXd RapporAnalyzerTest::VectorFromCounts(const std::vector<int>& counts) {
  uint32_t num_candidates = counts.size();
  Eigen::VectorXd counts_vector(num_candidates);
  for (size_t i = 0; i < num_candidates; i++) {
    counts_vector(i) = static_cast<double>(counts[i]);
  }
  return counts_vector;
}

void RapporAnalyzerTest::CheckExactSolution(const std::vector<int>& exact_candidate_counts) {
  Eigen::VectorXd est_bit_count_ratios;
  ExtractEstimatedBitCountRatios(&est_bit_count_ratios);
  Eigen::VectorXd exact_count_vector = VectorFromCounts(exact_candidate_counts);
  EXPECT_EQ(analyzer_->candidate_matrix_.cols(),
            static_cast<const int64_t>(exact_candidate_counts.size()));
  Eigen::VectorXd rhs = analyzer_->candidate_matrix_ * exact_count_vector;
  rhs /= exact_count_vector.sum();
  Eigen::VectorXd difference = rhs - est_bit_count_ratios;
  LOG(ERROR) << "How well does the exact solution reproduce the right hand side?"
             << difference.norm() / rhs.norm();
}

void RapporAnalyzerTest::PrintTrueCountsAndEstimates(
    const std::string& case_label, uint32_t num_candidates,
    const std::vector<CandidateResult>& results, const std::vector<int>& true_candidate_counts) {
  std::vector<int> count_estimates = CountsEstimatesFromResults(results);
  std::ostringstream true_stream;
  for (size_t i = 0; i < num_candidates; i++) {
    if (true_candidate_counts[i] > 0) {
      true_stream << "beta(" << i << ") == " << true_candidate_counts[i] << std::endl;
    }
  }
  LOG(ERROR) << "-------------------------------------";
  LOG(ERROR) << case_label;
  LOG(ERROR) << "True counts: " << true_stream.str();
  std::ostringstream estimate_stream;
  for (size_t i = 0; i < num_candidates; i++) {
    if (count_estimates[i] > 0) {
      estimate_stream << "beta(" << i << ") == " << count_estimates[i] << std::endl;
    }
  }
  LOG(ERROR) << "  Estimates: " << estimate_stream.str();
}

void RapporAnalyzerTest::AssessUtility(const std::vector<CandidateResult>& results,
                                       const std::vector<int>& true_candidate_counts) {
  // Get the estimates vector as well as the number of nonzero estimates
  int num_candidates = results.size();
  std::vector<int> estimates_vector = CountsEstimatesFromResults(results);
  int how_many_nonzeros = std::count_if(estimates_vector.begin(), estimates_vector.end(),
                                        [](const int a) { return a > 0; });

  // Sort candidate ids in an ascending order according to their real and
  // estimated values
  std::vector<int> estimated_id_order(num_candidates);
  std::iota(estimated_id_order.begin(), estimated_id_order.end(), 0);
  std::vector<int> true_id_order = estimated_id_order;

  std::sort(estimated_id_order.begin(), estimated_id_order.end(),
            [&estimates_vector](const int a, const int b) {
              return estimates_vector[a] > estimates_vector[b];
            });

  std::sort(true_id_order.begin(), true_id_order.end(),
            [&true_candidate_counts](const int a, const int b) {
              return true_candidate_counts[a] > true_candidate_counts[b];
            });

  // Compute the false positive rates for a grid of values
  LOG(ERROR) << "Identified " << how_many_nonzeros << " nonzero estimates.";
  LOG(ERROR) << "The measure of false positives for identified top n hitters:";
  const std::vector<int> top_hitters_analyzed = {10, 20, 50, 100, 200, 300, 500, 1000, 2000, 5000};
  int num_hitters = 0;
  for (auto num_hitters : top_hitters_analyzed) {
    if (num_hitters > num_candidates) {
      break;
    }
    float false_positives = 0;
    auto after_nth_element_it = true_id_order.begin() + num_hitters;
    for (int i = 0; i < num_hitters; i++) {
      auto where_ith_estimated_element =
          std::find(true_id_order.begin(), after_nth_element_it, estimated_id_order[i]);
      if (where_ith_estimated_element == after_nth_element_it) {
        false_positives += 1.0;
      }
    }
    LOG(ERROR) << "The false positive rate at n = " << num_hitters << " is "
               << false_positives / num_hitters;
  }
}

grpc::Status RapporAnalyzerTest::ComputeLeastSquaresFitQR(
    const Eigen::VectorXd& est_bit_count_ratios, std::vector<CandidateResult>* results) {
  // cast from smaller to larger type for comparisons
  const auto num_candidates = static_cast<const size_t>(analyzer_->candidate_matrix_.cols());
  EXPECT_EQ(results->size(), num_candidates);
  // define the QR solver and perform the QR decomposition followed by
  // least squares solve
  Eigen::SparseQR<Eigen::SparseMatrix<double, Eigen::ColMajor>, Eigen::COLAMDOrdering<int>>
      qrsolver;

  EXPECT_EQ(analyzer_->candidate_matrix_.rows(), est_bit_count_ratios.size());
  EXPECT_GT(analyzer_->candidate_matrix_.rows(), 0);
  // explicitly construct Eigen::ColMajor matrix from candidate_matrix_
  // (the documentation for Eigen::SparseQR requires it)
  // compute() as well as Eigen::COLAMDOrdering require compressed
  // matrix
  Eigen::SparseMatrix<double, Eigen::ColMajor> candidate_matrix_col_major =
      analyzer_->candidate_matrix_;
  candidate_matrix_col_major.makeCompressed();
  qrsolver.compute(candidate_matrix_col_major);
  if (qrsolver.info() != Eigen::Success) {
    std::string message = "Eigen::SparseQR decomposition was unsuccessfull";
    return grpc::Status(grpc::INTERNAL, message);
  }
  Eigen::VectorXd result_vals = qrsolver.solve(est_bit_count_ratios);
  if (qrsolver.info() != Eigen::Success) {
    std::string message = "Eigen::SparseQR solve was unsuccessfull";
    return grpc::Status(grpc::INTERNAL, message);
  }

  // write to the results vector
  EXPECT_EQ(num_candidates, results->size());
  for (size_t i = 0; i < num_candidates; i++) {
    results->at(i).count_estimate = result_vals[i] * analyzer_->bit_counter_.num_observations();
    results->at(i).std_error = 0;
  }
  return grpc::Status::OK;
}

void RapporAnalyzerTest::RunSimpleLinearRegressionReference(
    const std::string& case_label, uint32_t num_candidates, uint32_t num_bloom_bits,
    uint32_t num_cohorts, uint32_t num_hashes, const std::vector<int>& candidate_indices,
    const std::vector<int>& true_candidate_counts) {
  SetAnalyzer(num_candidates, num_bloom_bits, num_cohorts, num_hashes);
  AddObservationsForCandidates(candidate_indices);

  // set up the matrix
  auto status = analyzer_->BuildCandidateMap();
  if (!status.ok()) {
    EXPECT_EQ(grpc::OK, status.error_code());
    return;
  }
  // set up the right hand side of the equation
  Eigen::VectorXd est_bit_count_ratios;
  ExtractEstimatedBitCountRatios(&est_bit_count_ratios);

  std::vector<CandidateResult> results(num_candidates);
  status = ComputeLeastSquaresFitQR(est_bit_count_ratios, &results);
  if (!status.ok()) {
    EXPECT_EQ(grpc::OK, status.error_code());
    return;
  }

  PrintTrueCountsAndEstimates(case_label, num_candidates, results, true_candidate_counts);
}

void RapporAnalyzerTest::ShortExperimentWithAnalyze(
    const std::string& case_label, uint32_t num_candidates, uint32_t num_bloom_bits,
    uint32_t num_cohorts, uint32_t num_hashes, const std::vector<int>& candidate_indices,
    const std::vector<int>& true_candidate_counts, const bool print_estimates) {
  SetAnalyzer(num_candidates, num_bloom_bits, num_cohorts, num_hashes);
  AddObservationsForCandidates(candidate_indices);

  std::vector<CandidateResult> results;
  auto status = analyzer_->Analyze(&results);
  if (!status.ok()) {
    EXPECT_EQ(grpc::OK, status.error_code());
    return;
  }

  if (results.size() != num_candidates) {
    EXPECT_EQ(num_candidates, results.size());
    return;
  }

  if (print_estimates) {
    PrintTrueCountsAndEstimates(case_label, num_candidates, results, true_candidate_counts);
  }
}

void RapporAnalyzerTest::LongExperimentWithAnalyze(const std::string& case_label,
                                                   uint32_t num_candidates, uint32_t num_bloom_bits,
                                                   uint32_t num_cohorts, uint32_t num_hashes,
                                                   const std::vector<int>& candidate_indices,
                                                   const std::vector<int>& true_candidate_counts,
                                                   const bool print_estimates) {
  SetAnalyzer(num_candidates, num_bloom_bits, num_cohorts, num_hashes);
  AddObservationsForCandidates(candidate_indices);

  std::vector<CandidateResult> results;
  auto start_analyze_time = std::chrono::steady_clock::now();
  auto status = analyzer_->Analyze(&results);
  if (!status.ok()) {
    EXPECT_EQ(grpc::OK, status.error_code());
    return;
  }

  auto end_analyze_time = std::chrono::steady_clock::now();
  LOG(ERROR) << "Analyze() took "
             << std::chrono::duration_cast<std::chrono::seconds>(end_analyze_time -
                                                                 start_analyze_time)
                    .count()
             << " seconds.";

  if (results.size() != num_candidates) {
    EXPECT_EQ(num_candidates, results.size());
    return;
  }
  CheckExactSolution(true_candidate_counts);

  if (print_estimates) {
    PrintTrueCountsAndEstimates(case_label, num_candidates, results, true_candidate_counts);
  }
  AssessUtility(results, true_candidate_counts);
}

void RapporAnalyzerTest::CompareAnalyzeToSimpleRegression(
    const std::string& case_label, uint32_t num_candidates, uint32_t num_bloom_bits,
    uint32_t num_cohorts, uint32_t num_hashes, const std::vector<int>& candidate_indices,
    const std::vector<int>& true_candidate_counts) {
  SetAnalyzer(num_candidates, num_bloom_bits, num_cohorts, num_hashes);
  AddObservationsForCandidates(candidate_indices);

  // compute and print the results of Analyze()
  std::vector<CandidateResult> results_analyze;
  auto status = analyzer_->Analyze(&results_analyze);

  if (!status.ok()) {
    EXPECT_EQ(grpc::OK, status.error_code());
    return;
  }

  if (results_analyze.size() != num_candidates) {
    EXPECT_EQ(num_candidates, results_analyze.size());
    return;
  }

  std::string print_label = case_label + " analyze ";
  PrintTrueCountsAndEstimates(print_label, num_candidates, results_analyze, true_candidate_counts);

  // compute and print the results for simple linear regression
  Eigen::VectorXd est_bit_count_ratios;
  ExtractEstimatedBitCountRatios(&est_bit_count_ratios);

  print_label = case_label + " least squares ";
  std::vector<CandidateResult> results_ls(num_candidates);
  status = ComputeLeastSquaresFitQR(est_bit_count_ratios, &results_ls);
  if (!status.ok()) {
    EXPECT_EQ(grpc::OK, status.error_code());
    return;
  }
  PrintTrueCountsAndEstimates(print_label, num_candidates, results_ls, true_candidate_counts);
}

RapporConfig config_;
std::unique_ptr<RapporAnalyzer> analyzer_;

RapporCandidateList candidate_list_;

// By default this test uses p=0, q=1. Individual tests may override this.
double prob_0_becomes_1_ = 0.0;
double prob_1_stays_1_ = 1.0;

// Random device
std::random_device random_dev_;

}  // namespace rappor
}  // namespace cobalt

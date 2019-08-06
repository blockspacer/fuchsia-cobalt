// Copyright 2017 The Fuchsia Authors
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

#ifndef COBALT_ALGORITHMS_RAPPOR_RAPPOR_ANALYZER_H_
#define COBALT_ALGORITHMS_RAPPOR_RAPPOR_ANALYZER_H_

#include <memory>
#include <random>
#include <string>
#include <vector>

#include "./observation.pb.h"
#include "config/encodings.pb.h"
#include "config/report_configs.pb.h"
#include "src/algorithms/rappor/bloom_bit_counter.h"
#include "src/algorithms/rappor/lasso_runner.h"
#include "src/algorithms/rappor/rappor_analyzer_utils.h"
#include "src/algorithms/rappor/rappor_config_validator.h"

#include "grpc++/grpc++.h"
#include "third_party/eigen/Eigen/SparseCore"

namespace cobalt {
namespace rappor {

// A string RAPPOR analysis result for a single candidate. The method
// RapporAnalyzer::Analyze() populates a vector of CandidateResults, one for
// each candidate.
struct CandidateResult {
  double count_estimate;

  double std_error;
};

// A RapporAnalyzer is constructed for the purpose of performing a single
// string RAPPOR analysis.
//
// (1) Construct a RapporAnalyzer passing in a RapporConfig and a
// RapporCandidateList.
//
// (2) Repeatedly invoke AddObservation() to add the set of observations to
//     be analyzed. The observations must all be for the same metric part and
//     must have been encoded using the same encoding configuration. More
//     precisely this means they must be associated with the same customer_id,
//     project_id, metric_id, encoding_config_id and metric_part_name.
//
// (3) Invoke Analyze() to perform the string RAPPOR analysis and obtain the
//     results.
//
// (4) Optionally examine the underlying BloomBitCounter via the bit_counter()
//     accessor.
class RapporAnalyzer {
 public:
  // Constructs a RapporAnalyzer for the given config and candidates. All of the
  // observations added via AddObservation() must have been encoded using this
  // config. If the config is not valid then all calls to AddObservation()
  // will return false.
  // Does not take ownership of |candidates|.
  //
  // If |candidates| is NULL or empty then AddObservation() may still succeed
  // but Analyze() will return INVALID_ARGUMENT.
  //
  // TODO(rudominer) Enhance this API to also accept DP release parameters.
  explicit RapporAnalyzer(const RapporConfig& config, const RapporCandidateList* candidates);

  // Adds an additional observation to be analyzed. The observation must have
  // been encoded using the RapporConfig passed to the constructor.
  //
  // Returns true to indicate the observation was added without error.
  bool AddObservation(const RapporObservation& obs);

  // Performs the string RAPPOR analysis and writes the results to
  // |results_out|. Return OK for success or an error status.
  //
  // |results_out| will initially be cleared and, upon success of the alogrithm,
  // will contain a vector of size candidates->size() where |candidates| is the
  // argument to the constructor. |results_out| will be in the same order
  // as |candidates|. More precisely, the CandidateResult in
  // (*results_out)[i] will be the result for the candidate in (*candidates)[i].
  grpc::Status Analyze(std::vector<CandidateResult>* results_out);

  // Gives access to the underlying BloomBitCounter.
  const BloomBitCounter& bit_counter() { return bit_counter_; }

 private:
  friend class RapporAnalyzerTest;

  // Builds the RAPPOR CandidateMap and the associated sparse matrix based on
  // the data passed to the constructor.
  grpc::Status BuildCandidateMap();

  // An instance of Hashes is implicitly associated with a given
  // (candidate, cohort) pair and gives the list of hash values for that pair
  // under each of several hash functions. Each of the hash values is a
  // bit index in a Bloom filter.
  struct Hashes {
    // This vector has size h = num_hashes from the RapporConfig passed
    // to the RapporAnalyzer constructor. bit_indices[i] contains the value of
    // the ith hash function applied to the implicitly associated
    // (candidate, cohort) pair. bit_indices[i] is a bit index in the range
    // [0, k) where k = num_bloom_bits from the RapporConfig passed to the
    // RapporAnalyzer constructor.
    //
    // IMPORTANT: We index bits "from the right." This means that bit number
    // zero is the least significant bit of the last byte of the Bloom filter.
    std::vector<uint16_t> bit_indices;
  };

  // An instance of CohortMap is implicitly associated with a given
  // candidate string S and gives the Hashes for the pairs (S, cohort)
  // for each cohort in the range [0, num_cohorts).
  struct CohortMap {
    // This vector has size m = num_cohorts from the RapporConfig passed to
    // the RapporAnalyzer constructor. cohort_hashes[i] contains the Hashes
    // for cohort i.
    std::vector<Hashes> cohort_hashes;
  };

  // CandidateMap stores the list of all candidates and a parallel list of
  // CohortMaps for each candidate.
  struct CandidateMap {
    // Contains the list of all candidates. (pointer not owned)
    const RapporCandidateList* candidate_list;

    // This vector has size equal to the number of candidates in
    // |candidate_list|. candidate_cohort_maps[i] contains the CohortMap for
    // the ith candidate.
    std::vector<CohortMap> candidate_cohort_maps;
  };

  // Computes the column vector est_bit_count_ratios as well as a vector
  // est_std_errors of the corresponding standard errors. This method should be
  // invoked after all Observations have been added via AddObservation().
  //
  // est_bit_count_ratios is a column vector of length m * k where
  // m = # of cohorts
  // k = # of Bloom filter bits per cohort.
  //
  // For i < m, j < k, est_bit_count_ratios[i*k +j] = est_count_i_j / n_i
  // where
  // est_count_i_j = the estimate of the true number of times that bit j was
  //                 set in cohort i.
  // n_i           = the number of observations from cohort i
  //
  // Likewise, est_std_errors[i*k + j] = std_error_i_j / n_i
  // where
  // std_error_i_j = the unbiased sample estimate of the standard deviation of
  // est_count_i_j, and n_i is the same as above.
  //
  // These values are extracted from the BloomBitCounter.
  //
  // See the note at the bottom of rappor_anlayzer.cc for a justification of
  // the formulas.
  grpc::Status ExtractEstimatedBitCountRatiosAndStdErrors(Eigen::VectorXd* est_bit_count_ratios,
                                                          std::vector<double>* est_std_errors);

  BloomBitCounter bit_counter_;

  std::shared_ptr<RapporConfigValidator> config_;

  CandidateMap candidate_map_;

  // candidate_matrix_ is a representation of candidate_map_ as a sparse matrix.
  // It is an (m * k) X s sparse binary matrix, where
  // m = # of cohorts
  // k = # of Bloom filter bits per cohort
  // s = # of candidates
  // and for i < m, j < k, r < s candidate_matrix_[i*k + j, r] = 1 iff
  // candidate_map_.candidate_cohort_maps[r].cohort_hashes[i].bit_indices[g] =
  //     k - j
  // for at least one g < h where h = # of hashes.
  //
  // In other words, if one of the hash functions for cohort i hashes candidate
  // r to bit j (indexed from the left) then we put a 1 in column r, row
  // i*k + j.
  //
  // The expression (k - j) above is due to the fact that
  // candidate_map_ indexes bits from the right instead of from the left.
  Eigen::SparseMatrix<double, Eigen::RowMajor> candidate_matrix_;
};

}  // namespace rappor
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_RAPPOR_RAPPOR_ANALYZER_H_

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

#include "algorithms/rappor/rappor_analyzer.h"

#include <algorithm>
#include <cmath>
#include <random>

#include <glog/logging.h>

#include "algorithms/rappor/rappor_encoder.h"
#include "util/crypto_util/hash.h"
#include "util/log_based_metrics.h"
#include "util/lossmin/eigen-types.h"
#include "util/lossmin/minimizers/gradient-evaluator.h"
#include "util/lossmin/minimizers/parallel-boosting-with-momentum.h"

using cobalt_lossmin::InstanceSet;
using cobalt_lossmin::LabelSet;
using cobalt_lossmin::Weights;

namespace cobalt {
namespace rappor {

// Stackdriver metric contants
namespace {
constexpr char kAnalyzeFailure[] = "rappor-analyzer-analyze-failure";
}  // namespace

using crypto::byte;

RapporAnalyzer::RapporAnalyzer(const RapporConfig& config, const RapporCandidateList* candidates)
    : bit_counter_(config), config_(bit_counter_.config()) {
  candidate_map_.candidate_list = candidates;
  // candidate_map_.candidate_cohort_maps remains empty for now. It
  // will be populated by BuildCandidateMap.
}

bool RapporAnalyzer::AddObservation(const RapporObservation& obs) {
  VLOG(5) << "RapporAnalyzer::AddObservation() cohort=" << obs.cohort();
  return bit_counter_.AddObservation(obs);
}

grpc::Status RapporAnalyzer::Analyze(std::vector<CandidateResult>* results_out) {
  CHECK(results_out);

  // TODO(rudominer) Consider inserting here an analysis of the distribution
  // of the number of Observations over the set of cohorts. The mathematics
  // of our algorithm below assumes that this distribution is uniform. If
  // it is not uniform in practice this may indicate a problem with client-
  // side code and we may wish to take some corrective action.

  auto status = BuildCandidateMap();
  if (!status.ok()) {
    return status;
  }

  // est_bit_count_ratios is the right-hand side vector b from the equation Ax =
  // b that we are estimating, and est_std_errors are the corresponding standard
  // errors of entries of b. See comments on the declaration of
  // ExtractEstimatedBitCountRatiosAndStdErrors() for a description of these
  // vectors.
  Eigen::VectorXd est_bit_count_ratios;
  std::vector<double> est_std_errors;
  ExtractEstimatedBitCountRatiosAndStdErrors(&est_bit_count_ratios, &est_std_errors);

  // Note(rudominer) The cobalt_lossmin's GradientEvaluator constructor takes a
  // const LabelSet& parameter but est_bit_count_ratios is a
  // VectorXf. These types are different:
  // LabelSet = Matrix<float, Dynamic, Dynamic, RowMajor>
  // VectorXf = Matrix<float, Dynamic, 1>
  // These are different in two ways: VectorXf has a static number of columns
  // and VectorXf uses ColumnMajor order.
  // Here we copy est_bit_count_ratios to a label set before passing it to
  // the GradientEvaluator. This works fine. Some notes about this:
  // (1) Eigen defines copy constructors that do the right thing.
  // (2) There is no compilation error if we pass est_bit_count_ratios directly
  //     to the GradientEvaluator constructor. Somehow this works--I'm not sure
  //     why. But doing this leads to some unknown memory corruption which
  //     causes very bad non-deterministic runtime behavior. Be careful not
  //     to do this.
  // (3) We could avoid doing a copy here by just declaring est_bit_count_ratios
  //     as a LabelSet to begin with. But I don't like doing this because it
  //     makes the code less understandable to define a known column vector
  //     as a matrix with a dynamic number of columns in RowMajor order.
  LabelSet as_label_set = est_bit_count_ratios;
  LassoRunner lasso_runner(&candidate_matrix_);

  // In the first step, we compute the lasso path. That is,
  // we compute the solutions to a sequence of lasso subproblems
  // with decreasing values of l1 penalty. See the description of
  // RunFirstRapporStep for details.
  //
  // Set the parameters for RunFirstRapporStep.
  // Note:
  // 1. The minimizer-specific constants are defined in the LassoRuner class and
  // in the implementations of RunFirstRapporStep and GetExactValuesAndStdErrs.
  // It is a good idea to be familiar with them as some of them may need to be
  // adjusted.
  // 2. You can change kMaxNonzeroCoefficients and kColumns2RowsRatioSecondStep
  // depending on how many largest coefficients you care about; smaller numbers
  // mean fewer iterations (shorter lasso path in RunFirstRapporStep). See the
  // descriptions below. However, note that the algorithm may behave differently
  // when these constants are changed, i.e. they affect the entire algorithm
  // in RunFirstRapporStep, not only what is returned.
  // 3. Depending on the application, the value of kMaxSolution1Norm much lower
  // than 1.0 may be better.
  //
  // kMaxNonzeroCoefficients and kColumns2RowsRatioSecondStep will determine the
  // maximum expected number of nonzero coefficients in the solution. The final
  // subproblem of the lasso path will be entered when the number of nonzero
  // coefficients (i.e. coefficients larger than lasso_runner.zero_threshold())
  // identified, is at least min(kMaxNonzeroCoefficients,
  // kColumns2RowsRatioSecondStep * config_->num_bits() *
  // config_->num_cohorts()); otherwise the algorithm will perform all
  // kNumLassoSteps (unless it reaches the maximum number of epochs). Read below
  // for the explanation of the expression involving
  // kColumns2RowsRatioSecondStep.
  // Note: the actual number of identified nonzero
  // coefficients can be slightly larger or smaller.
  //
  // kColumns2RowsRatioSecondStep is the maximum ratio of the number of columns
  // to the number of rows of the matrix in the second step of RAPPOR. We will
  // stop the lasso computations if the number of identified nonzero
  // candidates reaches kColumns2RowsRatioSecondStep * M, where M ==
  // config_->num_bits() * config_->num_cohorts(); the number of columns of
  // the matrix in the second step is then at most kColumns2RowsRatioSecondStep
  // "times" the number of rows. Since we would like this matrix to have
  // linearly independent columns (in other words, such matrix has full column
  // rank), we must have kColumns2RowsRatioSecondStep < 1.0. Simulations on
  // random matrices suggest that it can be as high as 0.9 but the smaller the
  // value the higher the probability that the matrix will have full column
  // rank.
  // Note(bazyli): (mironov) says that this should not be too high because
  // of possible false positives but testing suggests that higher values may
  // actually give better results, and there are some arguments for that.
  //
  // Note(bazyli): Notice that if you want to detect
  // candidates that account for p proportion of the observations, you will look
  // for at most 1/p candidates. For example, if you only care about candidates
  // that account for at least 1% of observations, you may want to set
  // kMaxNonzeroCoefficients = 100. However, it does affect the whole algorithm
  // so be careful.
  static const int kMaxNonzeroCoefficients = 500;
  static const double kColumns2RowsRatioSecondStep = 0.7;
  // We expect the real underlying solution to have 1-norm equal to 1.0, and
  // even more so, the solution of the penalized problem should have norm
  // smaller than 1.0. This is specified by kMaxSolution1Norm.
  // Thus, we also want to stop the lasso computations before
  // the 1-norm of the current solution vector (est_candidate_weights)
  // approaches 1.0. This is closely related to the standard representation of
  // the lasso problem. If we stop when the 1-norm equals 1.0, the lasso
  // solution also solves the quadratic program: min || A x - y ||_2 subject to
  // || x ||_2 <= 1.0. However, note that if there is an exact solution to Ax =
  // y with || x ||_2 = 1.0, it will not be found in the lasso step because of
  // penalty. It may be found in the second RAPPOR step where the penalty is
  // insignificant.
  static const double kMaxSolution1Norm = 0.9;
  // kL1FirstToSecondStep is the ratio of the l1 penalty used in the last lasso
  // subproblem to the l1 penalty used in GetExactValuesAndStdErrs. This second
  // step of RAPPOR is conceptually a least squares problem. However, we
  // introduce a tiny bit of penalty for stability
  // (see below). This number should probably not be larger than 1e-3.
  static const double kL1FirstToSecondStep = 1e-3;

  // Perform initializations based on the chosen parameters.
  const int num_candidates = candidate_matrix_.cols();
  const uint32_t num_bits = config_->num_bits();
  const uint32_t num_cohorts = config_->num_cohorts();
  const uint32_t num_hashes = config_->num_hashes();
  // In the first step identify at most kColumns2RowsRatioSecondStep * M
  // coefficients, where M is the number of rows of the candidate matrix (but no
  // more than kMaxNonzeroCoefficients). This is a heuristic to ensure that the
  // matrix in the second step is full column rank.
  // TODO(bazyli): Add "bogus candidates" stopping criterion.
  const int max_nonzero_coeffs =
      std::min(num_candidates,
               std::min(static_cast<int>(kColumns2RowsRatioSecondStep * num_cohorts * num_bits),
                        kMaxNonzeroCoefficients));

  // We will need to construct the matrix for the second step. This matrix
  // is composed of columns corresponding to identified nonzero candidates
  // stored in second_step_cols.
  std::vector<int> second_step_cols;
  // Initialize the solution vector to zero vector for the lasso path.
  Weights est_candidate_weights = Weights::Zero(num_candidates);
  // Run the first step of RAPPOR to get potential nonzero candidates.
  lasso_runner.RunFirstRapporStep(max_nonzero_coeffs, kMaxSolution1Norm, as_label_set,
                                  &est_candidate_weights, &second_step_cols);

  // Build the matrix for the second step of RAPPOR.
  const uint32_t second_step_num_candidates = second_step_cols.size();
  InstanceSet candidate_submatrix_second_step(candidate_matrix_.rows(), second_step_num_candidates);
  PrepareSecondRapporStepMatrix(&candidate_submatrix_second_step, second_step_cols,
                                candidate_matrix_, num_cohorts, num_hashes);

  // We can now run the second step of RAPPOR.
  // ************************************************************************
  // Note(bazyli) We will use parallel boosting with
  // momentum again, with very small l1 and l2 penalties. We could use a
  // standard least squares solver here (e.g. based on QR decomposition).
  // However, we still cannot technically guarantee that the columns are
  // linearly independent, and so a tiny penalty will prevent the
  // coefficients from behaving ''wildly'' (this is a better-safe-than-sorry
  // choice); also, we have a very good initial point from the lasso
  // computations so we expect to converge fast (and the problem is smaller
  // than before). Conceptually, we are just solving the least squares
  // problem.
  //
  // The values from lasso computations are a bit biased (likely lower)
  // because of penalty, so the solution after this step should be closer to
  // the underlying distribution. Also, we want to compute standard errors.
  //
  // The RAPPOR paper performs least squares computation at this point; it
  // has closed-form expression for standard errors of coefficients (more
  // precisely, the covariance matrix), which we could use. However, this
  // expression involves the matrix (A_s^T A_s)^(-1), where A_s ==
  // candidate_submatrix_second_step; a priori the inverse might not exist
  // (or A_s^T A_s can be numerically singular or very ill-conditioned).
  // Although unlikely, we do not want to check that. Even if it is not the
  // case, computing the inverse might be computationally (too) heavy.
  // Therefore, we will simulate the behavior of the coefficients to
  // approximate the standard errors.
  //
  // The standard deviations in the linear regression problem min || A * x -
  // b||_2 are computed with the assumption that y_i = < a_i,x_i > + err_i,
  // where err_i are i.i.d. Gaussian errors (so technically speaking, these
  // they are not entirely correct in our case). Although we do not
  // compute the theoretical standard errors, we nevertheless make the same
  // assumption that the noise of detecting y_i (in our case, this is the
  // normalized count of bit i), is Gaussian with standard deviation equal
  // to the standard error of y_i (stored in the est_std_errors). We assume
  // these errors to be independent. We thus simulate the standard errors of
  // the coefficients by introducing this noise directly in y (as_label_set)
  // num_runs time, each time re-running the minimizer with the same input
  // (see the description of GetExactValuesAndStdErrs for details).
  //
  // In any event, the standard errors are conditioned on the choice made by
  // the lasso step (as are standard errors from least squares used in
  // RAPPOR paper). They do not account for anything random that happens in
  // the first RAPPOR step. Other possible choices to get the p-values
  // include: 1. bootstrap on the pairs (a_i,y_i) -- but this could give
  // zero columns. 2. p-values for the particular choice of final penalty in
  // lasso -- described in "Statistical Learning with Sparsity: The Lasso
  // and Generalizations" pp. 150 -- but this probably would account only
  // for the first step.
  // ************************************************************************

  // Prepare initial guess for the second step of RAPPOR.
  Weights est_candidate_weights_second_step = Weights(second_step_num_candidates);
  for (uint32_t i = 0; i < second_step_num_candidates; i++) {
    int first_step_col_num = second_step_cols[i];
    est_candidate_weights_second_step[i] = est_candidate_weights[first_step_col_num];
  }

  // Initialize the input. We will use kL1FirstToSecondStep fraction of the
  // penalty used in the last subproblem of lasso path.
  const double l1_second_step = kL1FirstToSecondStep * lasso_runner.minimizer_data().l1;
  Weights exact_candidate_weights_second_step(num_candidates);
  Weights est_candidate_errors_second_step(num_candidates);
  // Run the second step of RAPPOR to obtain better estimates of candidate
  // values, and estimates of standard errors.
  lasso_runner.GetExactValuesAndStdErrs(l1_second_step, est_candidate_weights_second_step,
                                        est_std_errors, candidate_submatrix_second_step,
                                        as_label_set, &exact_candidate_weights_second_step,
                                        &est_candidate_errors_second_step);

  // Prepare the final solution vector.
  results_out->resize(num_candidates);
  for (int i = 0; i < num_candidates; i++) {
    results_out->at(i).count_estimate = 0;
    results_out->at(i).std_error = 0;
  }

  // Write the results from the second step of RAPPOR.
  const uint32_t num_observations = bit_counter_.num_observations();
  for (uint32_t i = 0; i < second_step_num_candidates; i++) {
    int first_step_col_num = second_step_cols[i];
    results_out->at(first_step_col_num).count_estimate =
        exact_candidate_weights_second_step[i] * num_observations;
    results_out->at(first_step_col_num).std_error =
        est_candidate_errors_second_step[i] * num_observations;
  }

  // Note: the errors below probably indicate that the problem was too difficult
  // for the given parameters and the limit on the number of epochs.
  if (!lasso_runner.minimizer_data().converged) {
    std::string message = "The last lasso subproblem did not converge.";
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAnalyzeFailure) << message;
    return grpc::Status(grpc::DEADLINE_EXCEEDED, message);
  }

  if (!lasso_runner.minimizer_data().reached_last_lasso_subproblem) {
    std::string message = "The lasso path did not reach the last subproblem.";
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAnalyzeFailure) << message;
    return grpc::Status(grpc::DEADLINE_EXCEEDED, message);
  }

  return grpc::Status::OK;
}

grpc::Status RapporAnalyzer::ExtractEstimatedBitCountRatiosAndStdErrors(
    Eigen::VectorXd* est_bit_count_ratios, std::vector<double>* est_std_errors) {
  VLOG(5) << "RapporAnalyzer::ExtractEstimatedBitCountRatiosAndStdErrors()";
  CHECK(est_bit_count_ratios);

  if (!config_->valid()) {
    return grpc::Status(grpc::INVALID_ARGUMENT, "Invalid RapporConfig passed to constructor.");
  }

  if (candidate_map_.candidate_list == nullptr ||
      candidate_map_.candidate_list->candidates_size() == 0) {
    return grpc::Status(grpc::INVALID_ARGUMENT,
                        "Cannot perform RAPPOR analysis because no candidate "
                        "list was specified.");
  }

  const uint32_t num_bits = config_->num_bits();
  const uint32_t num_cohorts = config_->num_cohorts();

  est_bit_count_ratios->resize(num_cohorts * num_bits);
  est_std_errors->resize(num_cohorts * num_bits);

  const std::vector<CohortCounts>& estimated_counts = bit_counter_.EstimateCounts();
  CHECK(estimated_counts.size() == num_cohorts);

  int cohort_block_base = 0;
  for (auto& cohort_data : estimated_counts) {
    CHECK(cohort_data.count_estimates.size() == num_bits);
    for (size_t bit_index = 0; bit_index < num_bits; bit_index++) {
      // |bit_index| is an index "from the right".
      size_t bloom_index = num_bits - 1 - bit_index;
      (*est_bit_count_ratios)(cohort_block_base + bloom_index) =
          cohort_data.count_estimates[bit_index] /
          static_cast<double>(cohort_data.num_observations);
      (*est_std_errors)[cohort_block_base + bloom_index] =
          cohort_data.std_errors[bit_index] / static_cast<double>(cohort_data.num_observations);
    }
    cohort_block_base += num_bits;
  }
  return grpc::Status::OK;
}

grpc::Status RapporAnalyzer::BuildCandidateMap() {
  VLOG(5) << "RapporAnalyzer::BuildCandidateMap()";
  if (!config_->valid()) {
    return grpc::Status(grpc::FAILED_PRECONDITION, "Invalid RapporConfig passed to constructor.");
  }

  if (candidate_map_.candidate_list == nullptr ||
      candidate_map_.candidate_list->candidates_size() == 0) {
    return grpc::Status(grpc::INVALID_ARGUMENT,
                        "Cannot perform RAPPOR analysis because no candidate "
                        "list was specified.");
  }

  // TODO(rudominer) We should cache candidate_matrix_ rather than recomputing
  // candidate_map_ and candidate_matrix_ each time.

  const uint32_t num_bits = config_->num_bits();
  const uint32_t num_cohorts = config_->num_cohorts();
  const uint32_t num_hashes = config_->num_hashes();
  const uint32_t num_candidates = candidate_map_.candidate_list->candidates_size();

  if (VLOG_IS_ON(4)) {
    VLOG(4) << "RapporAnalyzer: Start list of " << num_candidates << " candidates:";
    for (const std::string& candidate : candidate_map_.candidate_list->candidates()) {
      VLOG(4) << "RapporAnalyzer: candidate: " << candidate;
    }
    VLOG(4) << "RapporAnalyzer: End list of " << num_candidates << " candidates.";
  }

  candidate_matrix_.resize(num_cohorts * num_bits, num_candidates);
  std::vector<Eigen::Triplet<double>> sparse_matrix_triplets;
  sparse_matrix_triplets.reserve(num_candidates * num_cohorts * num_hashes);

  int column = 0;
  for (const std::string& candidate : candidate_map_.candidate_list->candidates()) {
    // In rappor_encoder.cc it is not std::strings that are encoded but rather
    // |ValuePart|s. So here we want to take the candidate as a string and
    // convert it into a serialized |ValuePart|.
    ValuePart candidate_as_value_part;
    candidate_as_value_part.set_string_value(candidate);
    std::string serialized_candidate;
    candidate_as_value_part.SerializeToString(&serialized_candidate);

    // Append a CohortMap for this candidate.
    candidate_map_.candidate_cohort_maps.emplace_back();
    CohortMap& cohort_map = candidate_map_.candidate_cohort_maps.back();

    // Iterate through the cohorts.
    int row_block_base = 0;
    for (size_t cohort = 0; cohort < num_cohorts; cohort++) {
      // Append an instance of |Hashes| for this cohort.
      cohort_map.cohort_hashes.emplace_back();
      Hashes& hashes = cohort_map.cohort_hashes.back();

      // Form one big hashed value of the serialized_candidate. This will be
      // used to obtain multiple bit indices.
      byte hashed_value[crypto::hash::DIGEST_SIZE];
      if (!RapporEncoder::HashValueAndCohort(serialized_candidate, cohort, num_hashes,
                                             hashed_value)) {
        return grpc::Status(grpc::INTERNAL, "Hash operation failed unexpectedly.");
      }

      // bloom_filter is indexed "from the left". That is bloom_filter[0]
      // corresponds to the most significant bit of the first byte of the
      // Bloom filter.
      std::vector<bool> bloom_filter(num_bits, false);

      // Extract one bit index for each of the hashes in the Bloom filter.
      for (size_t hash_index = 0; hash_index < num_hashes; hash_index++) {
        uint32_t bit_index = RapporEncoder::ExtractBitIndex(hashed_value, hash_index, num_bits);
        hashes.bit_indices.push_back(bit_index);
        // |bit_index| is an index "from the right".
        bloom_filter[num_bits - 1 - bit_index] = true;
      }

      // Add triplets to the sparse matrix representation. For the current
      // column and the current block of rows we add a 1 into the row
      // corresponding to the index of each set bit in the Bloom filter.
      for (size_t bloom_index = 0; bloom_index < num_bits; bloom_index++) {
        if (bloom_filter[bloom_index]) {
          int row = row_block_base + static_cast<int>(bloom_index);
          sparse_matrix_triplets.emplace_back(row, column, 1.0);
        }
      }

      // In our sparse matrix representation each cohort corresponds to a block
      // of |num_bits| rows.
      row_block_base += num_bits;
    }
    // In our sparse matrix representation a column corresponds to a candidate.
    column++;
    row_block_base = 0;
  }

  candidate_matrix_.setFromTriplets(sparse_matrix_triplets.begin(), sparse_matrix_triplets.end());

  return grpc::Status::OK;
}

}  // namespace rappor
}  // namespace cobalt

/*

Justification for the formula used in ExtractEstimatedBitCountRatiosAndStdErrors
-------------------------------------------------------------------
See the comments at the declaration of the method
ExtractEstimatedBitCountRatiosAndStdErrors() in rappor_analyzer.h for the
context and the definitions of the symbols used here.

Here we justify the use of the formula

     est_bit_count_ratios[i*k +j] = est_count_i_j / n_i.

Let A be the binary sparse matrix produced by the method BuildCandidateMap()
and stored in candidate_matrix_. Let b be the column vector produced by
the method ExtractEstimatedBitCountRatiosAndStdErrors() and stored in the
variable est_bit_count_ratios.  In RapporAnalyzer::Analyze() we compute an
estimate of a solution to the equation Ax = b. The question we want to address
here is how do we know we are using the correct value of b? In particular, why
is it appropriate to divide each entry by n_i, the number of observations from
cohort i?

The assumption that underlies the justifcation is that the probability of
a given candidate string occurring is the same in each cohort. That is, there
is a probability distribution vector x_0 of length s = # of candidates such
that for each cohort i < m, and each candidate index r < s,
x_0[r] =
   (number of true observations of candidate r in cohort i) /
        (number of observations from cohort i)

Assume such an x_0 exists. Now let n_i = (number of observations from cohort i).
Then consider the vector b_i = A (n_i) x_0. We are only concerned with the
entries in b_i corresponding to cohort i, that is the entries
i*k + j for 0 <= j < k. Fix such a j and note that
b_i[i*k + j] = "the true count of 1's for bit j in cohort i". That is, the
count of 1's for bit j in cohort i prior to flipping bits for randomized
response. In other words, the count of 1's if we use p = 0, q = 1.

Dividing both sides of the equation A (n_i) x_0 = b_i by n_i and focusing
only on cohort i we get
     A x_0 [i*k + j] = "the true count of 1's for bit j in cohort i" / n_i

Let b* = A x_0. Then we have:

(i) x_0 is a solution to the equation Ax = b*
(ii) b*[i*k + j] = "the true count of 1's for bit j in cohort i" / n_i

This justifies our use of the vector b. We have
 b[i*k + j] = "the estimated count of 1's for bit j in cohort i" / n_i

 and we seek an estimate to an x such that Ax = b. Such an x may therefore
 naturally be considered to be an estimate of x_0.

*/

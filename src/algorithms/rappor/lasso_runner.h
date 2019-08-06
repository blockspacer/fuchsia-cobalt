// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ALGORITHMS_RAPPOR_LASSO_RUNNER_H_
#define COBALT_ALGORITHMS_RAPPOR_LASSO_RUNNER_H_

#include <math.h>

#include <vector>

#include "util/lossmin/eigen-types.h"
#include "util/lossmin/minimizers/gradient-evaluator.h"
#include "util/lossmin/minimizers/parallel-boosting-with-momentum.h"

#include "third_party/eigen/Eigen/SparseCore"

namespace cobalt {
namespace rappor {

// MinimizerData is a struct for storing the info about cobalt_lossmin
// minimizer.
struct MinimizerData {
  bool converged;

  bool reached_solution;

  bool reached_last_lasso_subproblem;

  int num_epochs_run;

  double l1;

  double l2;

  double zero_threshold;

  double convergence_threshold;
};

// LassoRunner is a class for running the optimizations in both steps of RAPPOR
// algorithm. These optimizations involve repeatedly solving problems of the
// general form:
//
// min_x || A * x - b ||_2^2 + l1 || x ||_1 + l2 * || x ||_2^2,
//
// with variable x, where A is an m x n matrix, and l1, l2 >= 0.
//
// (1) RunFirstRapporStep computes the lasso path to identify nonzero
// coefficients of x in the equation A * x = b where b is given approximately
// and A can be such that m < n (see exact description below).
//
// (2) GetExactValuesAndStdErrs solves a single lasso problem a number of times,
// each time introducing noise to the right hand side vector b in order to
// estimate standard errors of the coefficients (see below).
//
// Both functions use the cobalt_lossmin::ParallelBoostingWithMomentum solver to
// perform the optimizations.
class LassoRunner {
 public:
  explicit LassoRunner(const cobalt_lossmin::InstanceSet* matrix);

  // Runs the first step of RAPPOR. That is, runs the lasso path, i.e.
  // computes the solutions to a sequence of lasso subproblems:
  // min 1/(2N) || A * x - y ||_2^2 + l1_i * || x ||_1 + 1/2 * l2 * || x ||_2^2,
  // with variable x, and decreasing values of l1 penalty:
  // l1_1 > l1_2 > l2_3 > ... > l1_n. The l2 penalty is meant to be
  // insignificant and is introduced for stability reasons (so l2 << l1_n).
  // A == candidate_matrix_ and y == |as_label_set|,  N is
  // equal to the number of rows of A.
  //
  // The solution to each problem gives a "warm start" for the next one.
  // Initially, x == 0, and l1_1 is the smallest value such that x == 0 is the
  // solution to the first subproblem. The value of n and l1_n / l1_1 ratio are
  // defined inside the function,
  //
  // The lasso path can either be linear or logarithmic. This is specified
  // in the implementation. Linear path means that the difference between the
  // lasso penalties of each two consecutive subproblems is constant (so l1_i -
  // l1_(i+1) is the same for all i). The logarithmic path is a possible
  // alternative to the linear path (it is used by the R glmnet library); in it,
  // the l1_i form a geometric rather than arithmetic sequence, with
  // l1_(i+1)/l1_1 being constant and smaller than 1.0.
  //
  // If i is encountered such that the solution x of the i-th problem satisfies:
  // || x ||_1 >= |max_solution_1_norm| or || x ||_0 >= |max_nonzero_coeffs|, or
  // if i == n, then the lasso problem with l1_i is solved with a different
  // (possibly better) accuracy (set inside the function), and
  // the solution itself is written at |est_candidate_weights|, which should
  // be initialized to zero. The indices corresponding to positive entries of
  // the solution are stored in |second_step_cols|.
  //
  // Computing a lasso path (though not necessarily
  // linear) is the standard way to perform lasso computations and has a number
  // of advantages cited in literature. It is more numerically stable and more
  // efficient than computing just the last problem; it gives the entire path of
  // solutions and therefore can be used to choose the most meaningful value of
  // penalty (in our case we do not really know it a priori). The number of
  // nonzero coefficients of x (|| x ||_0) will be increasing as i increases.
  //
  // Note(bazyli): the logarithmic path is a possible alternative to the linear
  // path (it is used by the R glmnet library); in it, the l1_i form a geometric
  // rather than arithmetic sequence, with l1_(i+1)/l1_1 being constant and
  // smaller than 1.0. I found, however, that even though it typically results
  // in less problems solved, it often introduces many nonzero
  // coefficients in the initial subproblems of the lasso path. Since we are
  // interested in heavy hitters, we may not need to run the entire path and
  // want to be more conservative as to how quickly the new nonzero coefficients
  // are identified.
  void RunFirstRapporStep(int max_nonzero_coeffs, double max_solution_1_norm,
                          const cobalt_lossmin::LabelSet& as_label_set,
                          cobalt_lossmin::Weights* est_candidate_weights,
                          std::vector<int>* second_step_cols);
  // Runs the second step of RAPPOR. Solves the problems
  // min 1/(2N) || A * x - y_i ||_2^2 + |l1| * || x ||_1 + 1/2 * |l2| * || x
  // ||_2^2 with variable x, where A == |instances|, y_i == |as_label_set| +
  // err_i, for i = 1,2,..., |num_runs|. Here, (err_i)_j is a Gaussian error
  // with standard deviation equal to |est_stand_errs|[j]. All (err_i)_j, are
  // independent. N is equal to the number of rows of A.
  //
  // It then computes the standard errors ex_i from all runs for each of the
  // solutions x_i. The mean x from all num_runs is written at
  // |exact_est_candidate_weights|. (If none of the problems converged, then
  // unchanged |est_candidate_weights| will be written). The standard errors are
  // written at |est_candidate_errors|. (However, if less than 5 problems
  // converged, then standard errors are set to zero.)
  //
  // The problem is repeatedly solved using the parallel boosting with momentum
  // algorithm with |est_candidate_weights| as the initial guess.
  // The variables exact_est_candidate_weights and est_candidate_errors
  void GetExactValuesAndStdErrs(double l1, const cobalt_lossmin::Weights& est_candidate_weights,
                                const std::vector<double>& est_standard_errs,
                                const cobalt_lossmin::InstanceSet& instances,
                                const cobalt_lossmin::LabelSet& as_label_set,
                                cobalt_lossmin::Weights* exact_est_candidate_weights,
                                cobalt_lossmin::Weights* est_candidate_errors);

  // Returns reference to minimizer_data_.
  [[nodiscard]] const MinimizerData& minimizer_data() const { return minimizer_data_; }

 private:
  friend class LassoRunnerTest;

  // Pointer to the matrix A.
  const Eigen::SparseMatrix<double, Eigen::RowMajor>* matrix_;

  // Random device (used for generating Gaussian noise in
  // GetSignificantNonZeros).
  std::random_device random_dev_;

  // Stores info about lossmin minimizer after RunFirstRapporStep.
  MinimizerData minimizer_data_;

  // Constants used inside RunFirstRapporStep and GetExactValuesAndStdErrs.
  // We also use them in automated testing. They are reset in the constructor.
  // See the implementation in .cc file.
  //
  // zero_threshold_ is the number below which we assume a
  // coefficient of the solution to be effectively zero;
  // it is used in minimizers. It should be probably be not smaller than 1e-14.
  double zero_threshold_;
  // l2_to_l1_ratio_ is the ratio between l1 and l2 penalties used in all
  // minimizers.
  double l2_to_l1_ratio_;
  // alpha_ is a constant from parallel boosting with momentum algorithm;
  // it must be a number satisfying 0 < alpha_ < 1.
  double alpha_;
  // min_convergence_threshold_ is the minimum convergence threshold used in all
  // minimizers. Its purpose is to prevent the convergence thresholds in the
  // from becoming numerically too small. It should probably be not smaller than
  // 1e-14.
  double min_convergence_threshold_;
  // num_lasso_steps_ is the number of subproblems solved in the lasso path in
  // RunFirstRapporStep. It must be a positive integer.
  int num_lasso_steps_;
  // l1_max_to_l1_min_ratio_ is the ratio between the largest (initial) and
  // smallest (final) penalty in the lasso path. It must be smaller than 1.0.
  double l1_max_to_l1_min_ratio_;
  // use_linear_path_ specifies if linear path should be used (instead of
  // logarithmic),
  bool use_linear_path_;
};

}  // namespace rappor
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_RAPPOR_LASSO_RUNNER_H_

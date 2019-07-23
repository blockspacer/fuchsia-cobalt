// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "algorithms/rappor/lasso_runner.h"

#include <algorithm>

#include <glog/logging.h>

using cobalt_lossmin::GradientEvaluator;
using cobalt_lossmin::InstanceSet;
using cobalt_lossmin::LabelSet;
using cobalt_lossmin::ParallelBoostingWithMomentum;
using cobalt_lossmin::Weights;

namespace cobalt::rappor {

namespace {
// ***************************************************************************
// Constants used by the cobalt_lossmin minimizers in both steps of RAPPOR
// (lasso_runner.RunFirstRapporStep and lasso_runner.GetExactValuesAndStdErrs).
// They are meant to be generic so modify them with caution.
//
// kZeroThreshold is the (relative) proportion below which we assume a
// candidate count to be effectively zero; that is, we assume a candidate to
// be zero if its estimated count is below kZeroThreshold of the
// bit_counter_.num_observations(). It cannot be exactly zero for
// performance reasons (also, exact zero makes little if any sense
// numerically). Also, smaller values might be more difficult for individual
// runs of the minimizer.
// You can change kZeroThreshold to the value you think is best (smaller
// than what you would deem negligible); however, do not get close to double
// precision accuracy (1e-16). A reasonable value could be between 1e-4 and
// 1e-8.
constexpr double kZeroThreshold = 1e-6;
// kL2toL1Ratio is the ratio between l1 and l2 penalty.
// Although pure lasso (or linear regression) does not include any l2 penalty,
// a tiny bit can improve stability. A value of kL2toL1Ratio less or equal to
// 1e-2 should not affect the interpretation of the solution.
constexpr double kL2toL1Ratio = 1e-3;
// kLossEpochs denotes how often the current objective value is recorded in
// the solver (it will be recorded every kLossEpochs epochs).
// kConvergenceMeasures denotes how often the minimizer checks convergence:
// the algorithm will check convergence
// every kConvergenceEpochs epochs and terminate if at least
// one of the following is true.
// 1. The minimizer reached the solution up to the accuracy defined by
// minimizer.covergence_threshold(). In this case,
// minimizer.reached_solution() == true and minimizer.converged() == true.
// 2. The algorithm stopped improving as measured by the
// simple convergence check of the minimizer.
// In this case, minimizer.converged() == true.
// kLossEpochs and kConvergenceEpochs must be positive (and
// probably both not larger than 10). Also, kLossEpochs <=
// kConvergenceEpochs makes more sense.
constexpr int kLossEpochs = 5;
constexpr int kConvergenceMeasures = 5;
// kAlpha is a constant from parallel boosting with momentum paper,
// must be 0 < kAlpha < 1; cobalt_lossmin library default initial choice is 0.5,
// but we need to be able to reset this value in minimizer if needed.
constexpr double kAlpha = 0.5;
// kMinConvergenceThreshold is an absolute lower bound for the convergence
// thresholds in the algorithm; It should be something within a couple of
// orders of magnitude of the double precision (reasonable values may be
// between 1e-12 and 1e-14). This is introduced because convergence thresholds
// are computed relatively to the initial gradient norm and so they might be
// too low if the initial guess is very good.
constexpr double kMinConvergenceThreshold = 1e-12;
//
// ***************************************************************************
// Constants of the lasso_runner that will be used in RunFirstLassoStep.
//
// Note(bazyli) about modifying constants:
// 1. To improve the accuracy of the solution you may want to
// set the convergence thresholds: kRelativeConvergenceThreshold,
// kRelativeInLassoPathConvergenceThreshold, and kSimpleConvergenceThreshold
// to smaller values, at the expense of more computations (epochs) performed.
// See their descriptions below.
// 2. You may want to modify kMaxEpochs if it is too strict (which may be the
// case if you set very small convergence thresholds), but kNumLassoSteps and
// kL1maxToL1minRatio should be quite generic so modify them with caution.
// 3. You may test whether linear path (kUseLinearPath == true) or
// (kUseLinearPath == false) is better for your case. The run times and result
// quality may slightly differ (see below).
//
// Define minimizer and lasso-path-specific parameters.
// kRelativeConvergenceThreshold is the improvement that we expect to achieve
// at convergence, relative to initial gradient. That is, if ||g|| is the
// initial 2-norm norm of the gradient, we expect the final (i.e. after the
// last lasso problem) total measure of KKT violation to be equal to
// kRelativeConvergenceThreshold * ||g||. Value smaller than 1e-8 gives a full
// (single precision) convergence and is probably overshooting because the
// purpose of the first step is mostly to identify potential nonzero
// coefficients. Something between 1e-5 and 1e-7 should be enough.
// Even larger value can be chosen for performance reasons. This accuracy will
// be used only in the final lasso subproblem.
// kRelativeInLassoPathConvergenceThreshold has the same interpretation but
// will be used inside the lasso path (before the last subproblem). We
// probably want to set this to be slightly (e.g. ten times) less restrictive
// (larger) than kRelativeConvergenceThreshold for efficiency because the
// solutions inside the lasso path serve as a "warm-up" before the last
// subproblem; this subproblem, on the other hand, can be more accurate and
// should benefit more from "momentum" computations in the algorithm.
// kSimpleConvergenceThreshold == d means that the algorithm will stop if the
// best relative improvement between two consecutive measures of the objective
// in the last kConvergenceMeasures measures, is below d. In other words,
// relative improvements less than d will cause the minimizer to return.
// See the description of kConvergenceEpochs and
// kLossEpochs above. A rule-of-thumb value for kSimpleConvergenceThreshold
// can be some small fraction of the inverse of kMaxEpochs below.
//
// Note: The actual absolute values of convergence
// thresholds used in the algorithm are capped by
// loss_minimizer.min_convergence_threshold_ in case ||g|| is almost zero in the
// first place (see below). Note: If you are bumping into "The lasso path did
// not reach the last subproblem" or "The lasso path did not reach the last
// subproblem." in Analyze, it might mean that the convergence thresholds are
// too strict for the specified kMaxEpochs limit, given the difficulty of the
// problem.
constexpr double kRelativeConvergenceThreshold = 1e-5;
constexpr double kRelativeInLassoPathConvergenceThreshold = 1e-4;
constexpr double kSimpleConvergenceThreshold = 1e-5;
// kMaxEpochs denotes the limit on the total number of epochs (iterations)
// run; the actual number can be up to two times larger
// because every individual lasso subproblem (run of
// minimizer) has the same bound and the total epochs count is updated after
// the subproblem is run.
// Note: If you are bumping into "The lasso path did not reach the
// last subproblem." error in Analyze, it is possible that this number is too
// strict (low) for the convergence thresholds above.
constexpr int kMaxEpochs = 20000;
// kNumLassoSteps is the number of subproblems solved in the lasso path;
// it is not true that more steps will take more time: there should be a "sweet
// spot", and definitely this number should not be too small (probably something
// between 50 and 500).
constexpr int kNumLassoSteps = 100;
// kL1maxToL1minRatio is the ratio between the first and the last l1 penalty
// value in the lasso path; should probably be something between 1e-6 and
// 1e-3 (the R library glmnet uses 1e-3 by default).
constexpr double kL1maxToL1minRatio = 1e-3;
// kUseLinearPath specifies whether the lasso path should be linear (true) or
// logarithmic. Linear path means that the l1 penalties form an arithmetic
// sequence from largest to smallest; logarithmic path means that the penalties
// form a geometric series. Note(bazyli): linear path is more conservative in
// decreasing the penalties in the initial part of the lasso path. Since we are
// interested in the heavy hitters, we may want to prefer to add them slowly in
// this phase. Logarithmic path, on the other hand, decreases the penalty fast
// in the initial phase but more slowly towards the end of the lasso path. This
// is numerically more stable and may be faster but may introduce a lot of
// nonzero coefficients in the initial phase.
constexpr bool kUseLinearPath = true;
//
// ***************************************************************************
// Constants related to GetExactValuesAndStdErrors
//
// The convergence thresholds and
// kMaxEpochsSingleRun have the same definitions as the corresponding
// constants in RunFirstRapporStep. However, we may want the convergence
// thresholds to be more strict, because here we are interested in more exact
// values, and the problem to solve should be easier.
//
// Initialize the minimizer constants.
// Relative convergence thresholds have the same interpretation as the ones
// used in RunFirstRapporStep. Consult their description if you plan to modify
// the values.
constexpr double kRelativeConvergenceThreshold2Step = 1e-6;
constexpr double kSimpleConvergenceThreshold2Step = 1e-6;
// kNumRuns is the number of runs to estimate standard deviations of
// coefficients. The problem will be solved kNumRuns times, each time
// with a slightly different right hand side.
// This number probably should not be smaller than 5 but not too large:
// large value should give a better approximation of standard errors but
// kNumRuns == n means that the whole problem will be solved n times so it
// affects the runtime.
constexpr int kNumRuns = 20;
// kMaxEpochsSingleRun is the maximum number of epochs in a single
// run of the algorithm. So total number of epochs run will be bounded by
// kNumRuns * kMaxEpochsSingleRun.
constexpr int kMaxEpochsSingleRun = 5000;
// Note(bazyli): If convergence thresholds are too small relative to
// kMaxEpochsSingleRun then some of the runs of the algorithm may not
// converge. If less than 5 converge then the standard errors are set to 0. If
// none converges, then the exact_est_candidate_weights is set to be the same
// as est_candidate_weights. This is a safeguard solution and if it happens,
// we may want to adjust the constants so that it doesn't happen again (e.g.
// increase kMaxEpochsSingleRun).
}  // namespace

LassoRunner::LassoRunner(const InstanceSet* matrix) : matrix_(matrix) {
  zero_threshold_ = kZeroThreshold;
  l2_to_l1_ratio_ = kL2toL1Ratio;
  alpha_ = kAlpha;
  min_convergence_threshold_ = kMinConvergenceThreshold;
  num_lasso_steps_ = kNumLassoSteps;
  l1_max_to_l1_min_ratio_ = kL1maxToL1minRatio;
  use_linear_path_ = kUseLinearPath;
}

void LassoRunner::RunFirstRapporStep(const int max_nonzero_coeffs, const double max_solution_1_norm,
                                     const LabelSet& as_label_set, Weights* est_candidate_weights,
                                     std::vector<int>* second_step_cols) {
  // Construct the lossmin minimizer objects.
  // TODO(bazyli): remove this copy
  InstanceSet candidate_matrix = *matrix_;
  GradientEvaluator grad_eval(candidate_matrix, as_label_set);
  ParallelBoostingWithMomentum minimizer(0.0, 0.0, grad_eval);  // penalties will be set later.

  // Initialize the solution vector to zero vector for the lasso path and
  // compute the initial gradient (this will be used to get initial l1 penalty
  // and convergence thresholds).
  const int num_candidates = candidate_matrix.cols();
  Weights initial_gradient = Weights::Zero(num_candidates);
  grad_eval.SparseGradient(*est_candidate_weights, &initial_gradient);

  // Set the minimizer absolute convergence constants.
  const double initial_mean_gradient_norm = initial_gradient.norm() / num_candidates;
  const double kConvergenceThreshold = std::max(
      min_convergence_threshold_, kRelativeConvergenceThreshold * initial_mean_gradient_norm);
  const double kInLassoPathConvergenceThreshold =
      std::max(min_convergence_threshold_,
               kRelativeInLassoPathConvergenceThreshold * initial_mean_gradient_norm);

  // Set the constants for lasso path computations.
  const double l1max = initial_gradient.array().abs().maxCoeff();
  const double l1min = l1_max_to_l1_min_ratio_ * l1max;
  const double l2 = l2_to_l1_ratio_ * l1min;
  const double l1delta = (l1max - l1min) / num_lasso_steps_;
  const double l1mult = std::exp(std::log(l1_max_to_l1_min_ratio_) / num_lasso_steps_);

  // Set up the minimizer.
  minimizer.set_zero_threshold(zero_threshold_);
  minimizer.set_convergence_threshold(kInLassoPathConvergenceThreshold);
  minimizer.set_simple_convergence_threshold(kSimpleConvergenceThreshold);
  minimizer.set_l2(l2);
  minimizer.compute_and_set_learning_rates();  // learning rates must be
                                               // re-computed when l2 penalty
                                               // changes.
  VLOG(4) << "Lasso in-path convergence threshold ==" << kInLassoPathConvergenceThreshold;
  VLOG(4) << "Lasso final convergence threshold ==" << kConvergenceThreshold;

  // Initialize variables to track the lasso path computations.
  std::vector<double> loss_history;
  double solution_1_norm = 0;
  int total_epochs_run = 0;
  int how_many_nonzero_coeffs = 0;

  // Perform lasso path computations.
  int i = 0;
  double l1_this_step = use_linear_path_ ? l1max - l1delta : l1max * l1mult;

  for (; i < num_lasso_steps_ && total_epochs_run < kMaxEpochs; i++) {
    VLOG(4) << "Minimizing " << i << "-th Lasso subproblem";

    if (how_many_nonzero_coeffs >= max_nonzero_coeffs || i == num_lasso_steps_ - 1 ||
        solution_1_norm >= max_solution_1_norm) {
      // Enter the final lasso subproblem.
      minimizer.set_convergence_threshold(kConvergenceThreshold);
      if (i < num_lasso_steps_ - 1) {
        // If stopping criteria are met before reaching maximum number of steps,
        // use l1 from previous run.
        l1_this_step = use_linear_path_ ? l1_this_step + l1delta : l1_this_step / l1mult;
        i = num_lasso_steps_ - 1;
      }
      VLOG(4) << "Entered last Run";
    }

    minimizer.set_l1(std::max(l1min, l1_this_step));
    minimizer.set_reached_solution(false);
    minimizer.set_converged(false);

    // Set minimizer input as in the parallel boosting with momentum paper.
    minimizer.set_phi_center(*est_candidate_weights);
    minimizer.set_alpha(alpha_);
    minimizer.set_beta(1 - alpha_);

    VLOG(4) << "The l1 penalty used == " << l1_this_step;

    minimizer.Run(kMaxEpochs, kLossEpochs, kConvergenceMeasures, est_candidate_weights,
                  &loss_history);

    // Compute the 1-norm of the current solution and the number of nonzero
    // coefficients.
    solution_1_norm = est_candidate_weights->lpNorm<1>();
    how_many_nonzero_coeffs = (est_candidate_weights->array() > zero_threshold_).count();

    VLOG(4) << "Ran " << minimizer.num_epochs_run() << " epochs in this step.";
    VLOG(4) << "Num of nonzero coefficients found: " << how_many_nonzero_coeffs;
    VLOG(4) << "Solution 1-norm == " << solution_1_norm;
    total_epochs_run += minimizer.num_epochs_run();

    l1_this_step = use_linear_path_ ? l1_this_step - l1delta : l1_this_step * l1mult;
  }

  VLOG(4) << "Ran " << total_epochs_run << " epochs in total.";

  // Record minimizer info.
  minimizer_data_.reached_last_lasso_subproblem = (i == num_lasso_steps_);
  minimizer_data_.num_epochs_run = total_epochs_run;
  minimizer_data_.converged = minimizer.converged();
  minimizer_data_.reached_solution = minimizer.reached_solution();
  minimizer_data_.l1 = minimizer.l1();
  minimizer_data_.l2 = minimizer.l2();
  minimizer_data_.convergence_threshold = kConvergenceThreshold;
  minimizer_data_.zero_threshold = kZeroThreshold;

  // Prepare the vector of columns for the second step of RAPPOR.
  second_step_cols->clear();
  second_step_cols->reserve(how_many_nonzero_coeffs);
  for (int i = 0; i < est_candidate_weights->size(); i++) {
    if (est_candidate_weights->coeff(i) > zero_threshold_) {
      second_step_cols->push_back(i);
    }
  }
}

void LassoRunner::GetExactValuesAndStdErrs(const double l1, const Weights& est_candidate_weights,
                                           const std::vector<double>& est_standard_errs,
                                           const InstanceSet& instances,
                                           const LabelSet& as_label_set,
                                           Weights* exact_est_candidate_weights,
                                           Weights* est_candidate_errors) {
  // Note(bazyli): If convergence thresholds are too small relative to
  // kMaxEpochsSingleRun then some of the runs of the algorithm may not
  // converge. If less than 5 converge then the standard errors are set to 0. If
  // none converges, then the exact_est_candidate_weights is set to be the same
  // as est_candidate_weights. This is a safeguard solution and if it happens,
  // we may want to adjust the constants so that it doesn't happen again (e.g.
  // increase kMaxEpochsSingleRun).
  const double l2 = l2_to_l1_ratio_ * l1;
  const int num_candidates = est_candidate_weights.size();
  const int num_labels = as_label_set.size();
  const int min_num_converged_for_standard_errors = 5;
  int num_converged = 0;  // We record the number of runs in which the minimizer
                          // actually converged (although if everything is fine,
                          // i.e. all constants have appropriate values for the given case, this
                          // should be equal to num_runs at completion).

  // We will need the solutions from all runs to compute the mean solution and
  // standard errors.
  std::vector<Weights> est_weights_runs;  // vector for storing solutions from different runs
  Weights mean_est_weights = Weights::Zero(num_candidates);

  // In each run create a new_label_set by adding Gaussian noise to the original
  // as_label_set.
  for (int i = 0; i < kNumRuns; i++) {
    LabelSet new_label_set = as_label_set;

    for (int j = 0; j < num_labels; j++) {
      std::normal_distribution<double> nrm_distr(0, static_cast<double>(est_standard_errs[j]));
      double noise = nrm_distr(random_dev_);
      new_label_set(j) += noise;
    }

    // We will run the minimizer for the right hand side with noise
    // (new_label_set). We always use the est_candidate_weights as the initial
    // guess.

    // Construct the minimizer and compute initial gradient.
    Weights new_candidate_weights = est_candidate_weights;
    std::vector<double> loss_history_not_used;
    GradientEvaluator grad_eval(instances, new_label_set);
    ParallelBoostingWithMomentum minimizer(l1, l2, grad_eval);
    Weights initial_gradient = Weights(num_candidates);
    grad_eval.SparseGradient(new_candidate_weights, &initial_gradient);
    double initial_mean_gradient_norm = initial_gradient.norm() / num_candidates;
    double convergence_threshold =
        std::max(min_convergence_threshold_,
                 kRelativeConvergenceThreshold2Step * initial_mean_gradient_norm);

    // Set up and run the minimizer.
    minimizer.set_converged(false);
    minimizer.set_reached_solution(false);
    minimizer.set_phi_center(new_candidate_weights);
    minimizer.set_convergence_threshold(convergence_threshold);
    minimizer.set_zero_threshold(zero_threshold_);
    minimizer.set_simple_convergence_threshold(kSimpleConvergenceThreshold2Step);
    minimizer.set_alpha(alpha_);
    minimizer.set_beta(1 - alpha_);
    minimizer.Run(kMaxEpochsSingleRun, kLossEpochs, kConvergenceMeasures, &new_candidate_weights,
                  &loss_history_not_used);

    if (minimizer.converged()) {
      // Update the mean and store the current solution.
      mean_est_weights += new_candidate_weights;
      est_weights_runs.push_back(new_candidate_weights);
      num_converged++;
    }
  }

  if (num_converged > 0) {
    mean_est_weights /= num_converged;
  } else {
    mean_est_weights = est_candidate_weights;
  }

  // Compute the sample means and standard deviations (standard errors).
  Weights sample_stds = Weights::Zero(num_candidates);
  if (num_converged >= min_num_converged_for_standard_errors) {
    for (auto& est_weight : est_weights_runs) {
      sample_stds += (est_weight - mean_est_weights).array().pow(2).matrix();
    }
    sample_stds = sample_stds / (num_converged - 1);
    sample_stds = sample_stds.array().sqrt();
  }
  *est_candidate_errors = sample_stds;
  *exact_est_candidate_weights = mean_est_weights;
}

}  // namespace cobalt::rappor

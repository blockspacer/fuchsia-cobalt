// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/algorithms/rappor/lasso_runner_test.h"

namespace cobalt::rappor {

using cobalt_lossmin::InstanceSet;
using cobalt_lossmin::LabelSet;
using cobalt_lossmin::Weights;

// Tests of the correctness of the first RAPPOR step.
// Note: some of the tests are heuristic.

// Test if zero right hand side gives a zero solution.
TEST_F(LassoRunnerTest, ZeroSolution) {
  // Any matrix with zero right hand side and zero initial guess should give a
  // zero solution.
  // We will create a random m x n matrix with num_nonzero_entries.
  static const int m = 100;
  static const int n = 200;
  static const int num_nonzero_entries =
      1000;  // the actual number of non-zeros might be slightly different
  std::vector<Eigen::Triplet<double>> triplets(num_nonzero_entries);

  // Create a random m x n sparse matrix and zero right hand side.
  InstanceSet matrix = RandomMatrix(m, n, num_nonzero_entries);
  Weights zero_right_hand_side = Weights::Zero(m);
  LabelSet right_hand_side = zero_right_hand_side;
  Weights results = Weights::Zero(n);

  // The result should not depend on the values below.
  const int max_nonzero_coeffs = 1.0;
  const double max_solution_1_norm = 1.0;
  std::vector<int> second_step_cols;

  // Reset the runner and run first lasso step.
  lasso_runner_ = std::make_unique<LassoRunner>(&matrix);
  lasso_runner_->RunFirstRapporStep(max_nonzero_coeffs, max_solution_1_norm, right_hand_side,
                                    &results, &second_step_cols);
  // Check that the Lasso parameters make numerical sense.
  CheckLassoRunnerParameters();
  // Check that the result is zero.
  EXPECT_LE(results.norm(), 1e-12);
  EXPECT_EQ(second_step_cols.empty(), true);
}

// Checks that the result on a non-singular square matrix with full lasso path
// is correct; it should be close to the actual solution.
TEST_F(LassoRunnerTest, ExactSolution) {
  // We will create a random triangular n x n sparse matrix with ones on the
  // diagonal and additional num_nonzero_entries. Such matrix is nonsingular.
  static const int n = 50;
  static const int num_nonzero_entries =
      50;  // the actual number of non-zeros might be slightly different
  std::vector<Eigen::Triplet<double>> triplets(n + num_nonzero_entries);

  // If the absolute values of the additional random entries are at most 1.0,
  // the matrix will be well-conditioned.
  std::uniform_int_distribution<int> n_distribution(0, n - 1);
  std::uniform_real_distribution<double> real_distribution(0, 1.0);

  // Create the random matrix and a random solution.
  for (int k = 0; k < n; k++) {
    triplets.emplace_back(Eigen::Triplet<double>(k, k, 1.0));
  }
  int nonzero_entries_count = 0;
  while (nonzero_entries_count < num_nonzero_entries) {
    uint32_t i = n_distribution(random_dev_);
    uint32_t j = n_distribution(random_dev_);
    if (j > i) {
      double entry = real_distribution(random_dev_);
      triplets.emplace_back(Eigen::Triplet<double>(i, j, entry));
      nonzero_entries_count++;
    }
  }
  InstanceSet matrix(n, n);
  matrix.setFromTriplets(triplets.begin(), triplets.end());

  Weights random_solution(n);
  for (int k = 0; k < n; k++) {
    random_solution(k) = real_distribution(random_dev_);
  }
  Weights random_right_hand_side = matrix * random_solution;
  LabelSet right_hand_side = random_right_hand_side;
  Weights results = Weights::Zero(n);

  // Set the limits on the solution to make sure full lasso path is performed.
  const int max_nonzero_coeffs = n + 1;
  const double max_solution_1_norm = 10 * random_solution.lpNorm<1>();
  std::vector<int> second_step_cols;

  // Reset the lasso runner and perform the first step of RAPPOR.
  lasso_runner_ = std::make_unique<LassoRunner>(&matrix);
  MakeLastLassoStepExact();
  lasso_runner_->RunFirstRapporStep(max_nonzero_coeffs, max_solution_1_norm, right_hand_side,
                                    &results, &second_step_cols);
  // Check solution correctness.
  CheckLassoRunnerParameters();
  CheckFirstRapporStepCorrectness(right_hand_side, results);
  CheckNonzeroCandidates(second_step_cols, results);
  EXPECT_LE((results - random_solution).norm() / random_solution.norm(), 1e-3);
}

// Checks that the limit on the number of nonzero elements in the solution is
// implemented correctly. Runs a bunch of examples and checks if the limit on
// the number of nonzero coordinates is satisfied.
TEST_F(LassoRunnerTest, MaxNonzeros) {
  // We will create m x n random sparse matrix and
  // repeat the test num_tests times. The matrix will have approximately
  // num_nonzero_entries.
  static const int num_tests = 10;
  static const int m = 100;
  static const int n = 50;
  static const int num_nonzero_entries =
      500;  // the actual number of nonzero entries might be slightly different
  static const int max_norm_factor = 100;

  std::uniform_real_distribution<double> real_distribution(-1.0, 1.0);
  std::uniform_int_distribution<int> n_distribution(1, n);

  for (int i = 0; i < num_tests; i++) {
    // Create a random matrix and right hand side.
    // Take random limit on the number of nonzero coordinates.
    // Solve and check if the limit is satisfied.
    // The other stopping criterion is purposely lax.
    InstanceSet matrix = RandomMatrix(m, n, num_nonzero_entries);
    Weights random_solution(n);
    for (int k = 0; k < n; k++) {
      random_solution(k) = real_distribution(random_dev_);
    }
    Weights random_right_hand_side = matrix * random_solution;
    LabelSet right_hand_side = random_right_hand_side;

    int max_nonzero_coeffs = n_distribution(random_dev_);
    double max_solution_1_norm = max_norm_factor * random_solution.lpNorm<1>();

    // Reset the lasso runner and run the first RAPPOR step.
    Weights results = Weights::Zero(n);
    std::vector<int> second_step_cols;
    lasso_runner_ = std::make_unique<LassoRunner>(&matrix);
    lasso_runner_->RunFirstRapporStep(max_nonzero_coeffs, max_solution_1_norm, right_hand_side,
                                      &results, &second_step_cols);
    // Check that parameters make numerical sense and that the solution is
    // correct.
    CheckLassoRunnerParameters();
    CheckNonzeroCandidates(second_step_cols, results);
    CheckFirstRapporStepCorrectness(right_hand_side, results);

    // Since the last step of lasso is computed with a different accuracy, we
    // relax the check. This is heuristic.
    EXPECT_LE(second_step_cols.size(), static_cast<uint32_t>(1.2 * max_nonzero_coeffs + 5));
  }
}

// Checks that the limit on the 1-norm of the solution is well-implemented.
// This test is similar to MaxNonzeros. Run a bunch of examples and see if the
// limit on the solution 1-norm is satisfied
TEST_F(LassoRunnerTest, MaxNorm) {
  // We will create m x n random sparse matrix and repeat the test
  // num_tests times. The matrix will have approximately num_nonzero_entries.
  static const int num_tests = 10;
  static const int m = 100;
  static const int n = 50;
  static const int num_nonzero_entries =
      500;  // the actual number of nonzero entries might be slightly different

  std::uniform_real_distribution<double> real_distribution(0, 1.0);
  std::uniform_int_distribution<int> n_distribution(1, n);

  for (int i = 0; i < num_tests; i++) {
    // Create a random matrix and right hand side.
    // Take random limit on the 1-norm of the solution.
    // Solve and check if the limit is satisfied.
    // The other stopping criterion is purposely lax.
    InstanceSet matrix = RandomMatrix(m, n, num_nonzero_entries);
    Weights random_solution(n);
    for (int k = 0; k < n; k++) {
      random_solution(k) = real_distribution(random_dev_);
    }
    Weights random_right_hand_side = matrix * random_solution;
    LabelSet right_hand_side = random_right_hand_side;

    int max_nonzero_coeffs = n + 1;
    double max_solution_1_norm = real_distribution(random_dev_) * random_solution.lpNorm<1>() + 1.0;

    Weights results = Weights::Zero(n);
    std::vector<int> second_step_cols;
    lasso_runner_ = std::make_unique<LassoRunner>(&matrix);
    lasso_runner_->RunFirstRapporStep(max_nonzero_coeffs, max_solution_1_norm, right_hand_side,
                                      &results, &second_step_cols);
    // Check that parameters make numerical sense and that the solution is
    // correct.
    CheckLassoRunnerParameters();
    CheckNonzeroCandidates(second_step_cols, results);
    CheckFirstRapporStepCorrectness(right_hand_side, results);

    // Since the last step of lasso is computed with a different accuracy we
    // relax the check. This is heuristic.
    EXPECT_LE(results.lpNorm<1>(), static_cast<double>(1.5 * max_solution_1_norm));
  }
}

// Tests of the correctness of the second RAPPOR step.
// Note: Some tests are inherently heuristic.

// This test is similar to ExactSolution but checks exactness of the second
// RAPPOR step. Also checks that zero standard errors of the label set give
// zero standard errors of the result.
TEST_F(LassoRunnerTest, SecondStepExactness) {
  // Create a random triangular m x n sparse matrix with ones on the diagonal
  // and additional num_nonzero_entries.
  static const int m = 50;
  static const int n = 50;
  static const int num_nonzero_entries =
      100;  // the actual number of non-zeros might be slightly different
  static const double l1_penalty = 1e-8;
  std::vector<Eigen::Triplet<double>> triplets(n + num_nonzero_entries);

  std::uniform_int_distribution<int> n_distribution(0, n - 1);
  std::uniform_int_distribution<int> m_distribution(0, m - 1);
  std::uniform_real_distribution<double> real_distribution(0.0, 1.0);

  // Create the random matrix and a random solution.
  Weights random_solution(n);
  for (int k = 0; k < n; k++) {
    triplets.emplace_back(Eigen::Triplet<double>(k, k, 1.0));
    random_solution(k) = real_distribution(random_dev_);
  }
  int nonzero_entries_count = 0;
  while (nonzero_entries_count < num_nonzero_entries) {
    uint32_t i = m_distribution(random_dev_);
    uint32_t j = n_distribution(random_dev_);
    if (j < i) {
      double entry = real_distribution(random_dev_);
      triplets.emplace_back(Eigen::Triplet<double>(i, j, entry));
      nonzero_entries_count++;
    }
  }
  InstanceSet matrix(m, n);
  matrix.setFromTriplets(triplets.begin(), triplets.end());
  Weights random_right_hand_side = matrix * random_solution;
  LabelSet right_hand_side = random_right_hand_side;

  // Set standard errors to zero and prepare the minimizer input.
  std::vector<double> standard_errs(m, 0.0);
  Weights initial_guess = Weights::Constant(n, 0.75);  // NOLINT
  Weights results = Weights::Zero(n);
  Weights estimated_errs = Weights::Zero(n);

  // Reset the lasso runner and run the second RAPPOR step.
  lasso_runner_ = std::make_unique<LassoRunner>(&matrix);
  lasso_runner_->GetExactValuesAndStdErrs(l1_penalty, initial_guess, standard_errs, matrix,
                                          right_hand_side, &results, &estimated_errs);

  // Check the correctness of the solution and that the estimated errors are
  // zero.
  EXPECT_LE((results - random_solution).norm() / random_solution.norm(), 1e-3);
  EXPECT_LE(estimated_errs.norm(), 1e-12);
}

// Check the exactness of the standard error estimates.
// We run the Second RAPPOR step on an identity matrix and check that
// the estimated solution and errors are reasonable.
// Note: The tests are inherently heuristic because of randomness in the
// algorithm.
TEST_F(LassoRunnerTest, SecondStepErrors) {
  static const int n = 50;
  static const double l1_penalty = 1e-8;
  std::vector<Eigen::Triplet<double>> triplets;
  std::uniform_real_distribution<double> real_distribution(0.5, 1.0);  // NOLINT

  // Create the identity matrix and a random solution.
  Weights random_solution(n);
  for (int k = 0; k < n; k++) {
    random_solution(k) = real_distribution(random_dev_);
    triplets.push_back(Eigen::Triplet<double>(k, k, 1.0));  // NOLINT
  }

  InstanceSet matrix(n, n);
  matrix.setFromTriplets(triplets.begin(), triplets.end());
  LabelSet right_hand_side = random_solution;

  // Set standard errors to 10% of the entry.
  std::vector<double> standard_errs(n);
  Weights errors_to_compare(n);
  for (int k = 0; k < n; k++) {
    standard_errs[k] = 0.1 * random_solution(k);  // NOLINT
    errors_to_compare(k) = standard_errs[k];
  }

  // Run the second RAPPOR step.
  Weights initial_guess = Weights::Constant(n, 0.75);  // NOLINT
  Weights results = Weights::Zero(n);
  Weights estimated_errs = Weights::Zero(n);
  lasso_runner_ = std::make_unique<LassoRunner>(&matrix);
  lasso_runner_->GetExactValuesAndStdErrs(l1_penalty, initial_guess, standard_errs, matrix,
                                          right_hand_side, &results, &estimated_errs);

  // The solution should be close but we should not count on exactness.
  EXPECT_LE((results - random_solution).norm() / random_solution.norm(), 1e-1);
  // Check that the error estimates are reasonable.
  EXPECT_LE((estimated_errs - errors_to_compare).norm() / errors_to_compare.norm(), 0.5);
}

}  // namespace cobalt::rappor

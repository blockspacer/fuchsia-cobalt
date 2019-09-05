// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/algorithms/rappor/lasso_runner_test.h"

namespace cobalt::rappor {

using cobalt_lossmin::InstanceSet;
using cobalt_lossmin::LabelSet;
using cobalt_lossmin::Weights;

void LassoRunnerTest::SetLassoRunner(const InstanceSet* matrix) {
  lasso_runner_ = std::make_unique<LassoRunner>(matrix);
}

void LassoRunnerTest::CheckFirstRapporStepCorrectness(const LabelSet& right_hand_side,
                                                      const Weights& results) {
  // Get the penalty paramaters.
  const double l1 = lasso_runner_->minimizer_data_.l1;
  const double l2 = lasso_runner_->minimizer_data_.l2;
  const double zero_threshold = lasso_runner_->minimizer_data_.zero_threshold;
  // Reference to the problem matrix.
  const Eigen::SparseMatrix<double, Eigen::RowMajor>* A_matrix(lasso_runner_->matrix_);

  // Check that the minimizer converged and that minimizer info makes sense.
  EXPECT_EQ(lasso_runner_->minimizer_data_.converged, true);
  EXPECT_GE(l1, 0);
  EXPECT_GE(l2, 0);
  EXPECT_GT(lasso_runner_->minimizer_data_.convergence_threshold, 1e-16);
  EXPECT_GE(lasso_runner_->minimizer_data_.convergence_threshold,
            lasso_runner_->min_convergence_threshold_);
  EXPECT_GT(lasso_runner_->minimizer_data_.num_epochs_run,
            0);  // this check assumes that the initial guess was not exact
  if (l1 > 0) {
    EXPECT_GT(l1, l2);
  }

  // Check dimensions correctness and compute the
  // gradient = A^T * (A * x - y) / N + l2 * x.
  EXPECT_EQ(right_hand_side.size(), A_matrix->rows());
  EXPECT_EQ(results.size(), A_matrix->cols());
  Eigen::VectorXd residual = *A_matrix * results;
  residual -= right_hand_side;
  Eigen::VectorXd gradient = A_matrix->transpose() * residual;
  // Scale regression part of the gradient
  gradient /= A_matrix->rows();
  gradient += l2 * results;

  // Compute the KKT condition violation.
  Eigen::VectorXd estimate_error;
  estimate_error = ((results.array() > zero_threshold).select(gradient.array() + l1, 0)).matrix();
  estimate_error += ((results.array() < -zero_threshold).select(gradient.array() - l1, 0)).matrix();
  estimate_error +=
      ((abs(results.array()) <= zero_threshold).select(abs(gradient.array()) - l1, 0).max(0))
          .matrix();
  // The correctness check is relatively lax because the algorithm could have
  // converged before reaching the solution. On the other hand, the convergence
  // thresholds should be such that this holds.
  EXPECT_LE(estimate_error.norm() / results.size(), 1e-3);
}

void LassoRunnerTest::CheckNonzeroCandidates(const std::vector<int>& nonzero_cols,
                                             const Weights& results) {
  for (int i = 0; i < results.size(); i++) {
    if (std::find(nonzero_cols.begin(), nonzero_cols.end(), i) != nonzero_cols.end()) {
      EXPECT_GE(results[i], lasso_runner_->zero_threshold_);
    } else {
      EXPECT_LE(results[i], lasso_runner_->zero_threshold_);
    }
  }
}

void LassoRunnerTest::CheckLassoRunnerParameters() {
  EXPECT_GT(lasso_runner_->zero_threshold_, 1e-15);
  EXPECT_LT(lasso_runner_->zero_threshold_, 1e-2);
  EXPECT_GT(lasso_runner_->alpha_, 0.0);
  EXPECT_LT(lasso_runner_->alpha_, 1.0);
  EXPECT_GT(lasso_runner_->min_convergence_threshold_, 1e-15);
  EXPECT_GE(lasso_runner_->l1_max_to_l1_min_ratio_, 0);
  EXPECT_LT(lasso_runner_->l1_max_to_l1_min_ratio_, 1e-2);
  EXPECT_GT(lasso_runner_->num_lasso_steps_, 10);
}

// Creates a random sparse m x n matrix with positive entries.
InstanceSet LassoRunnerTest::RandomMatrix(const int m, const int n, const int num_nonzero_entries) {
  std::vector<Eigen::Triplet<double>> triplets(num_nonzero_entries);
  std::uniform_int_distribution<int> m_distribution(0, m - 1);
  std::uniform_int_distribution<int> n_distribution(0, n - 1);
  static const double min_entry = 1.0;
  static const double max_entry = 1.0;
  std::uniform_real_distribution<double> real_distribution(min_entry, max_entry);
  for (int k = 0; k < num_nonzero_entries; k++) {
    uint32_t i = m_distribution(random_dev_);
    uint32_t j = n_distribution(random_dev_);
    double entry = real_distribution(random_dev_);
    triplets.emplace_back(Eigen::Triplet<double>(i, j, entry));
  }
  InstanceSet matrix(m, n);
  matrix.setFromTriplets(triplets.begin(), triplets.end());

  return matrix;
}

}  // namespace cobalt::rappor

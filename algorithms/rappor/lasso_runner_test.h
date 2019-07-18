// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ALGORITHMS_RAPPOR_LASSO_RUNNER_TEST_H_
#define COBALT_ALGORITHMS_RAPPOR_LASSO_RUNNER_TEST_H_

#include <glog/logging.h>

#include <memory>
#include <vector>

#include "algorithms/rappor/lasso_runner.h"
#include "third_party/eigen/Eigen/SparseCore"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/lossmin/eigen-types.h"

namespace cobalt {
namespace rappor {

static const float kVerySmallPenalty = 1e-6;

class LassoRunnerTest : public ::testing::Test {
 protected:
  // Set the matrix pointed to by lasso_runner_.
  void SetLassoRunner(const cobalt_lossmin::InstanceSet* matrix);

  // Checks correctness of the solution to a single lasso problem stored in
  // |results|. lasso_runner_ must store the minimizer data.
  // Also checks that values stored in minimizer_data_ are reasonable.
  //
  // The convergence is checked by verifying the KKT conditions directly.
  // First, we compute the gradient of the objective without l1 penalty:
  // grad = (1/N) * A^T (A * x - b) + l2  * x,
  // where A == lasso_runner_->matrix_
  // x == results, l2 = lasso_runner_->minimizer_data.l2
  // b = right_hand_side, N = A.rows().
  //
  // The KKT condition is necessary
  // and sufficient for |results| to be a minimizer:
  // If results[i] < 0 then grad[i] == l1
  // If results[i] > 0 then grad[i] == -l1
  // If results[i] == 0 then -l1 <= grad[i] <= l1,
  //
  // where l1 = lasso_runner_->minimizer_data_.l1.
  // The function checks whether the norm of the violations of the
  // KKT condition is within a certain bound.
  void CheckFirstRapporStepCorrectness(
      const cobalt_lossmin::LabelSet& right_hand_side,
      const cobalt_lossmin::Weights& results);

  // Ensures that the last penalty in the lasso path is very small.
  // Logarithmic path is more appropriate in this case.
  void MakeLastLassoStepExact() {
    lasso_runner_->use_linear_path_ = false;
    lasso_runner_->l1_max_to_l1_min_ratio_ = kVerySmallPenalty;
  }

  // Checks that the |nonzero_cols| contains exactly column ids corresponding to
  // nonzero entries.
  void CheckNonzeroCandidates(const std::vector<int>& nonzero_cols,
                              const cobalt_lossmin::Weights& results);

  // Checks that the constants critical for the lasso path have reasonable
  // values.
  void CheckLassoRunnerParameters();

  // Creates a random sparse |m| x |n| matrix with positive entries.
  // The number of nonzero entries will approximately equal
  // |num_nonzero_entries|.
  cobalt_lossmin::InstanceSet RandomMatrix(int m, int n,
                                           int num_nonzero_entries);

  std::unique_ptr<LassoRunner> lasso_runner_;  // NOLINT

  // Random device
  std::random_device random_dev_;  // NOLINT
};

}  // namespace rappor
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_RAPPOR_LASSO_RUNNER_TEST_H_

// Copyright 2014 Google Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Author: nevena@google.com (Nevena Lazic)

#include "util/lossmin/minimizers/parallel-boosting-with-momentum.h"

#include <math.h>

#include <algorithm>
#include <functional>

#include "util/lossmin/minimizers/gradient-evaluator.h"

#include "third_party/eigen/Eigen/Core"

namespace cobalt_lossmin {

void ParallelBoostingWithMomentum::Setup() {
  compute_and_set_learning_rates();
  set_converged(false);
  set_reached_solution(false);
  alpha_ = 0.5;  // NOLINT readability-magic-numbers
  beta_ = 1.0 - alpha_;
  phi_center_ = Weights::Zero(gradient_evaluator().NumWeights());
}

void ParallelBoostingWithMomentum::compute_and_set_learning_rates() {
  // Per-coordinate learning rates are learning_rates[j] = 1 / (sparsity * Lj),
  // where sparsity is the maximum l0 norm of the rows of instance matrix, and
  // Lj is upper bound on the loss curvature along coordinate j.
  double sparsity = gradient_evaluator().Sparsity();
  gradient_evaluator().PerCoordinateCurvature(&learning_rates_);
  learning_rates_ =
      (learning_rates_.array() + l2()).inverse().matrix() / sparsity;
}

double ParallelBoostingWithMomentum::Loss(const Weights &weights) const {
  double loss = gradient_evaluator().SparseLoss(weights);
  if (l2() > 0.0) {
    // NOLINTNEXTLINE readability-magic-numbers
    loss += 0.5 * l2() * weights.squaredNorm();
  }
  if (l1() > 0.0) {
    loss += l1() * weights.cwiseAbs().sum();
  }
  return loss;
}

void ParallelBoostingWithMomentum::ConvergenceCheck(const Weights &weights,
                                                    const Weights &gradient) {
  double error_squared = 0.0;
  for (int i = 0; i < gradient.size(); i++) {
    if (weights(i) > zero_threshold()) {
      // for weights > 0 the gradient should be == -l1
      error_squared += (gradient(i) + l1()) * (gradient(i) + l1());
    } else if (weights(i) < -zero_threshold()) {
      // for weights < 0 the gradient should be == l1
      error_squared += (gradient(i) - l1()) * (gradient(i) - l1());
    } else {
      // for weights == 0 the gradient should be between -l1 and l1
      double err = std::max(std::abs(gradient(i)) - l1(), 0.0);
      error_squared += err * err;
    }
  }

  if (std::sqrt(error_squared) / weights.size() < convergence_threshold()) {
    set_reached_solution(true);
    set_converged(true);
  }
}

void ParallelBoostingWithMomentum::EpochUpdate(Weights *weights, int /*epoch*/,
                                               bool check_convergence) {
  // Compute the intermediate weight vector y.
  Weights y = (1.0 - alpha_) * *weights + alpha_ * phi_center_;

  // Compute the gradient at y (except l1 penalty).
  Weights gradient_wrt_y = Weights::Zero(y.size());
  gradient_evaluator().SparseGradient(y, &gradient_wrt_y);
  if (l2() > 0.0) {
    gradient_wrt_y += l2() * y;
  }

  // Take the gradient descent step.
  *weights -= gradient_wrt_y.cwiseProduct(learning_rates_);

  // Apply the l1 shrinkage.
  if (l1() > 0.0) {
    L1Prox(l1() * learning_rates_, weights);
  }

  // Update the approximation function.
  phi_center_ -= (1.0 - alpha_) / alpha_ * (y - *weights);
  // NOLINTNEXTLINE readability-magic-numbers
  alpha_ = -beta_ / 2.0 + pow(beta_ + beta_ * beta_ / 4.0, 0.5);
  beta_ *= (1.0 - alpha_);

  if (check_convergence) {
    // Compute the gradient of the objective except the l1 part and check
    // convergence.
    Weights gradient_wrt_weights = Weights::Zero(weights->size());
    gradient_evaluator().SparseGradient(*weights, &gradient_wrt_weights);
    if (l2() > 0.0) {
      gradient_wrt_weights += l2() * *weights;
    }
    ConvergenceCheck(*weights, gradient_wrt_weights);
  }
}

}  // namespace cobalt_lossmin

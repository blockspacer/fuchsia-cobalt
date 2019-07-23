// Copyright 2014 Google Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Author: nevena@google.com (Nevena Lazic)
//
// An implementation of:
// I. Mukherjee, K. Canini, R. Frongillo, and Y. Singer, "Parallel Boosting with
// Momentum", ECML PKDD 2013.
// Variable names follow the notation in the paper.

#ifndef COBALT_UTIL_LOSSMIN_MINIMIZERS_PARALLEL_BOOSTING_WITH_MOMENTUM_H_
#define COBALT_UTIL_LOSSMIN_MINIMIZERS_PARALLEL_BOOSTING_WITH_MOMENTUM_H_

#include "util/lossmin/eigen-types.h"
#include "util/lossmin/minimizers/gradient-evaluator.h"
#include "util/lossmin/minimizers/loss-minimizer.h"

namespace cobalt_lossmin {

class GradientEvaluator;

class ParallelBoostingWithMomentum : public LossMinimizer {
 public:
  ParallelBoostingWithMomentum(double l1, double l2, const GradientEvaluator &gradient_evaluator)
      : LossMinimizer(l1, l2, gradient_evaluator) {
    Setup();
  }

  // Sets learning rates, initializes alpha_, beta_ and phi_center_.
  void Setup() override;

  // Checks convergence by verifying the KKT conditions directly.
  // |gradient| is the gradient of the objective at |weights|, not including
  // the contribution of the l1 penalty part of the objective.
  //
  // The function checks whether the mean norm of violations of the KKT
  // condition is below convergence threshold. The KKT condition is necessary
  // and sufficient for |weights| to be a minimizer:
  // If weights_i < 0 then gradient_i == l1()
  // If weights_i > 0 then gradient_i == -l1()
  // If weights_i == 0 then -l1() <= gradient_i <= l1().
  //
  // The squared violations at each coordinate are summed and the square
  // root is divided by weights.size() is compared to convergence_thresold().
  // If convergence is determined, sets the appropriate flags so that
  // converged() == true and reached_solution() == true.
  void ConvergenceCheck(const Weights &weights, const Weights &gradient) override;

  // Returns the total loss for given parameters |weights|, including l1 and l2
  // regularization. Uses sparse matrix multiply from Eigen.
  // This is more efficient in terms of performed operations than calling
  // gradient_evaluator().Loss(weights).
  [[nodiscard]] double Loss(const Weights &weights) const override;

  // Set phi_center_ (this is the same as v_0 in the paper).
  // Note: Following the paper exactly, phi_center_ should be equal to the
  // initial guess for weights when Run() is executed (however, this requirement
  // does not seem to be necessary for convergence in practice).
  void set_phi_center(const VectorXd &phi) { phi_center_ = phi; }

  // Computes the learning rates. This is introduced to enable recomputing
  // learning rates in case l2 penalty changes.
  void compute_and_set_learning_rates();

  // Set alpha_; we may need to reset alpha before Run().
  void set_alpha(const double alpha) { alpha_ = alpha; }

  // Set beta_; we may need to reset beta before Run().
  void set_beta(const double beta) { beta_ = beta; }

 private:
  // Run one epoch (iteration) of the algorithm.
  // Updates 'weights' and the quadratic approximation function phi(w), such
  // that at iteration k, loss(weights_k) <= min_w phi_k(w).
  //     y = (1 - alpha) * weights + alpha * phi_center
  //     grad_y = loss_grad(y) + l2 * y
  //     weights[j] = weights[j] - grad_y[j] / learning_rates[j]
  //     weights[j] =
  //         sign(weights[j]) * max(0, weights[j], l1 / learning_rates[j])
  void EpochUpdate(Weights *weights, int epoch, bool check_convergence) override;

  // Per-coordinate learning rates.
  VectorXd learning_rates_;

  // Center of the approximating quadratic function phi.
  VectorXd phi_center_;

  // Parameter for updating the approximation function phi. At each iteration,
  // 'alpha_' is updated to the solution of the quadratic equation
  //     alpha_^2 = beta_ * (1.0 - alpha_)
  double alpha_;

  // Parameter used to update alpha, defined as
  //     beta_{epoch} = \prod_{i=1}^{epoch} (1 - alpha_i).
  double beta_;
};

}  // namespace cobalt_lossmin

#endif  // COBALT_UTIL_LOSSMIN_MINIMIZERS_PARALLEL_BOOSTING_WITH_MOMENTUM_H_

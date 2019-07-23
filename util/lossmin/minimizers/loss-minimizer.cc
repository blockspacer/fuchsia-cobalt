// Copyright 2014 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Author: nevena@google.com (Nevena Lazic)

#include "util/lossmin/minimizers/loss-minimizer.h"

#include <algorithm>
#include <random>

namespace cobalt_lossmin {

constexpr double kLossThreshold = 1e-12;

bool LossMinimizer::Run(int max_epochs, int loss_epochs, int convergence_epochs,
                        Weights *weights, std::vector<double> *loss) {
  // If the initial guess is exact skip the update.
  if (Loss(*weights) < kLossThreshold) {
    set_converged(true);
    set_reached_solution(true);
    return converged_;
  }

  // Run for up to 'max_epochs' epochs.
  int epoch;
  for (epoch = 0; epoch < max_epochs; ++epoch) {
    // Record the loss before this epoch update.
    if (epoch % loss_epochs == 0) {
      loss->push_back(Loss(*weights));
    }

    // Set the 'check_convergence' flag (do we check convergence at this
    // iteration?).
    bool check_convergence = (epoch > 0) && (epoch % convergence_epochs == 0);

    // Run for one epoch to update the parameters.
    EpochUpdate(weights, epoch, check_convergence);

    // Note: We should periodically check if the algorithm has not stopped by
    // calling SimpleConvergenceCheck; numerical problems can be encountered if
    // the convergence is checked only by CheckConvergence() inside
    // EpochUpdate(), namely if the algorithm "stops" for any reason before
    // solving the problem up to the given accuracy. The result should still be
    // meaningful (provided it is not due some unrelated bug).
    if (check_convergence) {
      SimpleConvergenceCheck(*loss);
    }

    // Check the convergence flag.
    if (converged_) {
      break;
    }
  }
  loss->push_back(Loss(*weights));

  num_epochs_run_ = std::min(epoch + 1, max_epochs);
  return converged_;
}

// By default only check if gradient is == 0.
void LossMinimizer::ConvergenceCheck(const Weights &weights,
                                     const Weights &gradient) {
  if (gradient.squaredNorm() / weights.size() < convergence_threshold_) {
    set_reached_solution(true);
    set_converged(true);
  }
}

void LossMinimizer::SimpleConvergenceCheck(const std::vector<double> &loss) {
  // Check convergence by verifying that the max relative loss decrease
  // (loss[t-1] - loss[t]) / loss[t-1] is below 'simple_convergence_threshold_'.
  if (loss.size() > num_convergence_epochs_) {
    double loss_difference = 0.0;
    for (size_t i = loss.size() - num_convergence_epochs_; i < loss.size();
         ++i) {
      if (loss[i - 1] > kLossThreshold) {
        loss_difference = std::max(loss_difference, 1 - loss[i] / loss[i - 1]);
      } else {
        set_reached_solution(true);
        set_converged(true);
      }
    }
    if (loss_difference < simple_convergence_threshold_) set_converged(true);
  }
}

}  // namespace cobalt_lossmin

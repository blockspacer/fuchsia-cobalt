// Copyright 2014 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Author: nevena@google.com (Nevena Lazic)
//
// Interface for gradient descent algorithms. Example usage:
//
// 1. Create a GradientEvaluator object that computes the value and gradient of
//    a loss function on a given dataset.
//
//    InstanceSet instances = ...
//    LabelSet labels = ...
//    GradientEvaluator gradient_evaluator(instances, labels);
//
// 2. Create a LossMinimizer object from 'gradient_evaluator' and l1 and l2
//    regularization (penalty) parameters.
//
//    double l1 = ...
//    double l2 = ...
//    ParallelBoostingWithMomentum minimizer(l1, l2, gradient_evaluator);
//
// 3. Run optimization for up to 'max_epochs' epochs. 'loss' is filled with
//    loss values across epochs, and 'weights' contains the best parameters.
//
//    Weights weights = Weights::Zero(num_features);
//    vector<double> loss;
//    int max_epochs = 100;
//    bool converged = loss_minimizer.Run(max_epochs, &weights, &loss);
//
// Note: algorithm-specific parameters such as learning rates are set based on
// the data. If the dataset or the label set owned by 'gradient_evaluator'
// changes, the user must call loss_minimizer.Setup() method before
// loss_minimizer.Run().

#ifndef COBALT_UTIL_LOSSMIN_MINIMIZERS_LOSS_MINIMIZER_H_
#define COBALT_UTIL_LOSSMIN_MINIMIZERS_LOSS_MINIMIZER_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "util/lossmin/eigen-types.h"
#include "util/lossmin/minimizers/gradient-evaluator.h"

#include "third_party/eigen/Eigen/Core"

namespace cobalt_lossmin {

// Interface for gradient descent algorithms that minimize a loss function over
// a labeled dataset. A GradientEvaluator object provides the loss value and
// gradient for given parameters at each epoch. Derived classes must provide the
// methods EpochUpdate() and Setup().
class LossMinimizer {
 public:
  // Constructor sets the l1 and l2 regularization parameters and
  // 'gradient_evalutor_'.
  LossMinimizer(double l1, double l2,
                const GradientEvaluator &gradient_evaluator)
      : l1_(l1), l2_(l2), gradient_evaluator_(gradient_evaluator) {}

  virtual ~LossMinimizer() = default;

  // Initializes algorithm-specific parameters such as learning rates, based
  // on the loss function and the dataset. Called in the constructor of the
  // derived classes. If the dataset that 'gradient_evaluator' points to
  // changes, Setup() must be called again by the user.
  virtual void Setup() = 0;

  // Runs minimization until convergence, or up to 'max_epochs' epochs. Returns
  // true if the algorithm has converged. Parameters 'weights' contain the
  // initial guess and should be initialized by the user; at completion they
  // will contain the parameters produced at the last epoch. 'loss' is filled
  // with loss values produced every 'loss_epochs' epochs. Convergence is
  // checked every 'convergence_epochs'.
  bool Run(int max_epochs, int loss_epochs, int convergence_epochs,
           Weights *weights, std::vector<double> *loss);

  // Convenience Run method that evaluates the loss and checks for convergence
  // at every iteration.
  bool Run(int max_epochs, Weights *weights, std::vector<double> *loss) {
    return Run(max_epochs, 1, 1, weights, loss);
  }

  // Abstract method that updates the weights in the current iteration, i.e.
  // it implements a single iteration in Run.
  virtual void EpochUpdate(Weights *weights, int epoch,
                           bool check_convergence) = 0;

  // Returns the total loss for given parameters 'weights', including l1 and l2
  // regularization.
  [[nodiscard]] virtual double Loss(const Weights &weights) const {
    double loss = gradient_evaluator_.Loss(weights);
    if (l2_ > 0.0) {
      loss += 0.5 * l2_ *  // NOLINT readability-magic-numbers
              weights.squaredNorm();
    }
    if (l1_ > 0.0)
      loss += l1_ * weights.cwiseAbs().sum();
    return loss;
  }

  // Checks convergence by checking the sufficient and necessary conditions for
  // minimizer directly. If converged, the flags 'converged_' and
  // 'reached_solution_' are set to true.
  virtual void ConvergenceCheck(const Weights &weights,
                                const Weights &gradient);

  // Checks convergence based on the decrease in loss over the last
  // 'num_convergence_epochs_' epochs. If converged, the flag 'converged_' is
  // set to true.
  // The convergence is determined in the following way. The function
  // looks at the last num_convergence_epochs_ loss values that have been
  // recorded, and if the relative difference between every two consecutive
  // recorded values (so loss[t-1] - loss[t]) / loss[t-1]) is below
  // simple_convergence_threshold_, it declares convergence.
  void SimpleConvergenceCheck(const std::vector<double> &loss);

  // Setters and getters for convergence criteria parameters.
  [[nodiscard]] bool converged() const { return converged_; }
  void set_converged(bool converged) { converged_ = converged; }
  [[nodiscard]] bool reached_solution() const { return reached_solution_; }
  void set_reached_solution(bool reached_solution) {
    reached_solution_ = reached_solution;
  }
  [[nodiscard]] double convergence_threshold() const {
    return convergence_threshold_;
  }
  void set_convergence_threshold(double convergence_threshold) {
    convergence_threshold_ = convergence_threshold;
  }
  [[nodiscard]] double simple_convergence_threshold() const {
    return simple_convergence_threshold_;
  }
  void set_simple_convergence_threshold(double simple_convergence_threshold) {
    simple_convergence_threshold_ = simple_convergence_threshold;
  }
  void set_num_convergence_epochs(int num_convergence_epochs) {
    num_convergence_epochs_ = num_convergence_epochs;
  }
  [[nodiscard]] double zero_threshold() const { return zero_threshold_; }
  void set_zero_threshold(double zero_threshold) {
    zero_threshold_ = zero_threshold;
  }

  // Returns a reference to 'gradient_evaluator_'.
  [[nodiscard]] const GradientEvaluator &gradient_evaluator() const {
    return gradient_evaluator_;
  }

  // Getter/setter of the l1 regularization parameter.
  [[nodiscard]] double l1() const { return l1_; }
  void set_l1(double l1) { l1_ = l1; }

  // Getter/setter of the l2 regularization parameter.
  [[nodiscard]] double l2() const { return l2_; }
  void set_l2(double l2) { l2_ = l2; }

  // Returns the number of iterations the last time Run() was executed.
  [[nodiscard]] int num_epochs_run() const { return num_epochs_run_; }

  // Applies L1Prox coefficientwise to 'weights' and 'threshold'.
  static void L1Prox(double threshold, Weights *weights) {
    for (int i = 0; i < weights->size(); ++i) {
      weights->coeffRef(i) = L1Prox(weights->coeff(i), threshold);
    }
  }

  // Applies L1Prox coefficientwise to 'weights' and 'threshold', where
  // 'threshold' is a vector of per-coordinate thresholds.
  static void L1Prox(const VectorXd &threshold, Weights *weights) {
    for (int i = 0; i < weights->size(); ++i) {
      weights->coeffRef(i) = L1Prox(weights->coeff(i), threshold.coeff(i));
    }
  }

  // Returns sign('x') * max(0.0, abs('x') - 'threshold')
  // (this is often called "soft thresholding").
  static inline double L1Prox(double x, double threshold) {
    return Sign(x) * std::max(std::abs(x) - threshold, 0.0);
  }

  // Returns sign('x').
  static inline double Sign(double x) {
    if (x > 0.0)
      return 1.0;
    if (x < 0.0)
      return -1.0;
    return 0.0;
  }

 private:
  // Regularization parameters.
  double l1_;
  double l2_;

  // GradientEvaluator used to compute the (unpenalized) loss and gradient.
  const GradientEvaluator &gradient_evaluator_;

  // Convergence parameters.
  // These can also be updated after construction of the minimizer.

  // Note: reached_solution_ and converged_ are different. converged_ == true
  // means any (or both) of the two: 1. that the algorithm found the solution up
  // to the desired convergence_threshold_ (and then reached_solution_ == true
  // as well), 2. that it stopped improving as determined by
  // simple_convergence_threshold_. So if converged_ == true and
  // reached_solution_ == false, it doesn't mean that the algorithm did not
  // reach the solution but that it did not reach the solution up to the desired
  // accuracy before it slowed down. Most likely, the final point is very close
  // to the solution in that case anyway.
  bool converged_ = false;  // convergence flag set by convergence checks
  bool reached_solution_ =
      false;  // flag indicating whether the algorithm
              // actually reached the solution as determined by ConvergenceCheck
  double convergence_threshold_ =
      // NOLINTNEXTLINE
      1e-5;  // threshold for assessing convergence by ConvergenceCheck
  double simple_convergence_threshold_ =
      // NOLINTNEXTLINE
      1e-5;  // threshold for assessing convergence by SimpleConvergenceCheck
  // NOLINTNEXTLINE
  size_t num_convergence_epochs_ = 5;  // used in SimpleConvergenceCheck

  // zero_threshold_ is the threshold below which we treat the coordinate value
  // as zero (in absolute terms). This is used in ConvergenceCheck.
  double zero_threshold_ = 1e-6;  // NOLINT

  // The number of epochs (iterations) when Run() was executed.
  // In other words, each epoch is a step towards minimum during minimization.
  // This variable gets updated when Run() is called.
  int num_epochs_run_ = 0;
};

}  // namespace cobalt_lossmin

#endif  // COBALT_UTIL_LOSSMIN_MINIMIZERS_LOSS_MINIMIZER_H_

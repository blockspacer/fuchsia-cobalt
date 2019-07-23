// Copyright 2014 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Author: nevena@google.com (Nevena Lazic)
//
// GradientEvaluator is a class for computing the value and gradient of a
// loss function on a labeled dataset {(instance_i, label_i)}, given parameters
// 'weights'. Its methods are called by gradient descent algorithms implementing
// the LossMinimizer interface.
//
// By default the loss function (whose value and gradient are computed), is the
// linear regression objective function:
//
// f(x) == (0.5 / N) * || A * x - b ||_2^2,
//
// where x == 'weights', A = instances_, b == labels_, N = instances_.rows().

#ifndef COBALT_UTIL_LOSSMIN_MINIMIZERS_GRADIENT_EVALUATOR_H_
#define COBALT_UTIL_LOSSMIN_MINIMIZERS_GRADIENT_EVALUATOR_H_

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "util/lossmin/eigen-types.h"

namespace cobalt_lossmin {

class GradientEvaluator {
 public:
  // Constructor sets up the dataset.
  GradientEvaluator(const InstanceSet &instances, const LabelSet &labels)
      : instances_(instances), instances_transposed_(instances.transpose()), labels_(labels) {}

  virtual ~GradientEvaluator() = default;

  // Returns the residual between the predicted labels at 'weights', and
  // labels_. The default implementation returns A * x - b, where A ==
  // instances_, x = 'weights', b == labels_. The implementation exploits
  // sparsity using iterators over rows of instances_ to implement matrix-vector
  // multiplication straight from definition.
  // Note:  It is not as efficient as calling
  // Eigen's matrix-vector multiply directly but can potentially be easily
  // parallelized (when parallel implementation is desired but open mp is not
  // available).
  [[nodiscard]] virtual Weights Residual(const Weights &weights) const;

  // Returns the loss for given parameters 'weights'.
  // The default implementation computes the normalized norm of vector returned
  // by Residual(weights): 0.5 / N * || A * x - y ||_2^2, where A == instances_
  // y == labels_, x == 'weights', N = instances_.rows().
  [[nodiscard]] virtual double Loss(const Weights &weights) const;

  // This is the same as Loss except (by default) instead of calling Residual,
  // it uses Eigen's efficient matrix-vector multiplication directly.
  // Note: It can be parallelized using open mp, if it is supported.
  [[nodiscard]] virtual double SparseLoss(const Weights &weights) const;

  // Computes the gradient wrt the given parameters 'weights'. 'gradient' is
  // owned by the caller and should be initialized to zero.
  // The default implementation computes (1 / N) * A^T (A * x - b) where A ==
  // instances_ y == labels_, x == 'weights', N = instances_.rows(). The default
  // implementation exploits sparsity using iterators over rows of
  // instances_transposed_ to implement matrix-vector multiplication straight
  // from definition, in the same way as the default implementation of
  // Residual().
  // Note:  It is not as efficient as calling Eigen's matrix-vector
  // multiply directly but can potentially be easily parallelized (when parallel
  // implementation is desired but open mp is not supported).
  virtual void Gradient(const Weights &weights, Weights *gradient) const;

  // This is the same as Gradient except it uses (by default)
  // Eigen's efficient matrix-vector multiplication directly.
  // Note: It can be parallelized using open mp, if it is supported.
  virtual void SparseGradient(const Weights &weights, Weights *gradient) const;

  // Returns the number of examples in the dataset.
  [[nodiscard]] int NumExamples() const { return instances_.rows(); }

  // Returns the number of features.
  [[nodiscard]] int NumFeatures() const { return instances_.cols(); }

  // Alias for the number of features (these are often used interchangeably).
  [[nodiscard]] int NumWeights() const { return NumFeatures(); }

  // Returns the per-coordinate curvature of the data. Used to set the learning
  // rates of ParallelBoostingWithMomentum.
  void PerCoordinateCurvature(VectorXd *per_coordinate_curvature) const;

  // Returns sparsity, defined as the maximum instance l0 norm. Used to help
  // set learning rates in ParallelBoostingWithMomentum.
  // TODO(bazyli): exploit sparsity in the implementation.
  [[nodiscard]] double Sparsity() const;

  // Returns the instances.
  [[nodiscard]] const InstanceSet &instances() const { return instances_; }

  // Returns the transpose pf instances.
  [[nodiscard]] const InstanceSet &instances_transposed() const { return instances_transposed_; }

  // Returns the labels.
  [[nodiscard]] const LabelSet &labels() const { return labels_; }

 private:
  // Training instances.
  const InstanceSet &instances_;

  // The transpose of instances. This is needed for fast gradient computations
  // and should be computed once so it is initialized at construction (not each
  // time gradient is computed).
  const InstanceSet instances_transposed_;

  // Instance labels.
  const LabelSet &labels_;
};

}  // namespace cobalt_lossmin

#endif  // COBALT_UTIL_LOSSMIN_MINIMIZERS_GRADIENT_EVALUATOR_H_

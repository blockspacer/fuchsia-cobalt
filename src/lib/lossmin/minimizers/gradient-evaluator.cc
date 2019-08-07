// Copyright 2014 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Author: nevena@google.com (Nevena Lazic)

#include "src/lib/lossmin/minimizers/gradient-evaluator.h"

#include <functional>

namespace cobalt_lossmin {

void GradientEvaluator::PerCoordinateCurvature(VectorXd *per_coordinate_curvature) const {
  // TODO(bazyli): cwiseProduct can be avoided if the matrix is just binary.
  *per_coordinate_curvature = VectorXd::Ones(NumExamples()).transpose() *
                              instances_.cwiseProduct(instances_) / NumExamples();
}

double GradientEvaluator::Sparsity() const {
  typename Instance::Index sparsity = 0;
  for (int i = 0; i < instances_.rows(); ++i) {
    sparsity = std::max(sparsity, instances_.innerVector(i).nonZeros());
  }
  return static_cast<double>(sparsity);
}

Weights GradientEvaluator::Residual(const Weights &weights) const {
  // TODO(azani): Implement multi-threaded version.
  // Note(bazyli): The outer loop can be executed in parallel (assuming thread
  // safety).
  // TODO(bazyli): if the matrix is binary, multiplication is not necessary.
  Weights residual = Weights::Zero(NumExamples());
  for (int i = 0; i < NumExamples(); i++) {
    for (InstanceSet::InnerIterator it(instances(), i); it; ++it) {
      residual[i] += it.value() * weights[it.col()];
    }
  }
  residual -= labels();
  return residual;
}

double GradientEvaluator::Loss(const Weights &weights) const {
  Weights residual = Residual(weights);
  // NOLINTNEXTLINE readability-magic-numbers
  return 0.5 * residual.squaredNorm() / NumExamples();
}

void GradientEvaluator::Gradient(const Weights &weights, Weights *gradient) const {
  Weights residual = Residual(weights);
  // TODO(azani): Implement multi-threaded version.
  // Note(bazyli): The outer loop can be executed in parallel (assuming thread
  // safety).
  // TODO(bazyli): if the matrix is binary, the multiplication is not necessary
  for (int i = 0; i < NumWeights(); i++) {
    for (InstanceSet::InnerIterator it(instances_transposed(), i); it; ++it) {
      gradient->coeffRef(i) += it.value() * residual[it.col()];
    }
  }

  *gradient /= NumExamples();
}

void GradientEvaluator::SparseGradient(const Weights &weights, Weights *gradient) const {
  // Note(bazyli): Eigen library recommends computations step-by-step for best
  // performance.
  // Note(bazyli): Eigen supports parallelization of row-major-sparse * dense
  // matrix-vector products using open mp; open mp must be enabled in the
  // compiler;
  Weights residual = instances() * weights;
  residual -= labels();
  *gradient = instances_transposed() * residual;
  *gradient /= NumExamples();
}

double GradientEvaluator::SparseLoss(const Weights &weights) const {
  Weights residual = instances() * weights;
  residual -= labels();
  // NOLINTNEXTLINE readability-magic-numbers
  return 0.5 * residual.squaredNorm() / NumExamples();
}

}  // namespace cobalt_lossmin

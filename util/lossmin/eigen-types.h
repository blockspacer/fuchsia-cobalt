// Copyright 2014 Google Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_UTIL_LOSSMIN_EIGEN_TYPES_H_
#define COBALT_UTIL_LOSSMIN_EIGEN_TYPES_H_

#include "third_party/eigen/Eigen/Core"
#include "third_party/eigen/Eigen/SparseCore"

namespace cobalt_lossmin {

// Arrays.
using ArrayXb = Eigen::Array<bool, Eigen::Dynamic, 1>;
using ArrayXf = Eigen::ArrayXf;
using ArrayXd = Eigen::ArrayXd;
using ArrayXi = Eigen::ArrayXi;

// Vectors.
using VectorXf = Eigen::VectorXf;
using VectorXd = Eigen::VectorXd;
using VectorXi = Eigen::VectorXi;

// Sparse Vectors.
using SparseVectorXf = Eigen::SparseVector<float>;
using SparseVectorXd = Eigen::SparseVector<double>;

// Matrix.
using MatrixXf = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor>;
using RowMatrixXf = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
using MatrixXd = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor>;
using RowMatrixXd = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

// Instances and parameters.
using Weights = VectorXd;

using Label = VectorXd;
using LabelSet = RowMatrixXd;

using Instance = Eigen::SparseVector<double>;
using InstanceSet = Eigen::SparseMatrix<double, Eigen::RowMajor>;
using SparseMatrixXd = Eigen::SparseMatrix<double, Eigen::ColMajor>;

}  // namespace cobalt_lossmin

#endif  // COBALT_UTIL_LOSSMIN_EIGEN_TYPES_H_

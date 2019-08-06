// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ALGORITHMS_RAPPOR_RAPPOR_ANALYZER_UTILS_H_
#define COBALT_ALGORITHMS_RAPPOR_RAPPOR_ANALYZER_UTILS_H_

#include <vector>

#include "util/lossmin/eigen-types.h"

#include "third_party/eigen/Eigen/SparseCore"

namespace cobalt {
namespace rappor {

// Constructs a submatrix of |full_matrix| composed of columns corresponding to
// indices in |second_step_cols|. |num_cohorts| and |num_hashes| are needed to
// determine the number of nonzero entries of the matrix.
void PrepareSecondRapporStepMatrix(cobalt_lossmin::InstanceSet* second_step_matrix,
                                   const std::vector<int>& second_step_cols,
                                   const cobalt_lossmin::InstanceSet& full_matrix, int num_cohorts,
                                   int num_hashes);

}  // namespace rappor
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_RAPPOR_RAPPOR_ANALYZER_UTILS_H_

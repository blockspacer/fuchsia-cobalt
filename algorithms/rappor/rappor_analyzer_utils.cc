// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "algorithms/rappor/rappor_analyzer_utils.h"

#include <vector>

using cobalt_lossmin::InstanceSet;

namespace cobalt::rappor {

void PrepareSecondRapporStepMatrix(InstanceSet* second_step_matrix,
                                   const std::vector<int>& second_step_cols,
                                   const InstanceSet& full_matrix, int num_cohorts,
                                   int num_hashes) {
  // We will construct the matrix from triplets which is simple and efficient.
  const uint32_t second_step_num_candidates = second_step_cols.size();
  const int nonzero_matrix_entries =
      num_cohorts * num_hashes * static_cast<int>(second_step_num_candidates);
  std::vector<Eigen::Triplet<double>> second_step_matrix_triplets;
  second_step_matrix_triplets.reserve(nonzero_matrix_entries);

  // Construct a ColMajor copy of candidate_matrix_ format to iterate over
  // columns efficiently, and form the triplets.
  Eigen::SparseMatrix<double, Eigen::ColMajor> candidate_matrix_col_major = full_matrix;
  for (uint32_t col_i = 0; col_i < second_step_num_candidates; col_i++) {
    // Iterate over column corresponding to this candidate and update
    // triplets.
    for (Eigen::SparseMatrix<double, Eigen::ColMajor>::InnerIterator it(candidate_matrix_col_major,
                                                                        second_step_cols[col_i]);
         it; ++it) {
      second_step_matrix_triplets.emplace_back(Eigen::Triplet<double>(it.row(), col_i, it.value()));
    }
  }

  // Build the second step matrix.
  second_step_matrix->setFromTriplets(second_step_matrix_triplets.begin(),
                                      second_step_matrix_triplets.end());
}

}  // namespace cobalt::rappor

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_EXPERIMENTAL_ARCHIVED_KRR_INTEGER_ENCODER_H_
#define COBALT_SRC_ALGORITHMS_EXPERIMENTAL_ARCHIVED_KRR_INTEGER_ENCODER_H_

#include "src/algorithms/experimental/random.h"
#include "src/algorithms/experimental/archived/response_randomizer.h"

namespace cobalt {

// Encodes integer values using a fixed-precision encoding followed by randomized response.
//
// This scheme is based on Algorithm 2 of "The privacy blanket of the shuffle model" (Balle,
// Bell, Gascon, Nissim; 2019), adapted slightly to accept integers in any specified range.
class ArchivedKrrIntegerEncoder {
 public:
  // The parameter |p| is the probability that Encode() returns a data-independent result, and
  // should be in the range [0.0, 1.0] inclusive. The range of integers that may be encoded is
  // specified by |min_int| and |max_int|. The parameter |partitions| should be a divisor of
  // (|max_int| - |min_int|). The segment [|min_int|, |max_int|] will be discretized into
  // |partitions| subsegments.
  ArchivedKrrIntegerEncoder(BitGeneratorInterface<uint32_t>* gen, int64_t min_int, int64_t max_int,
                 uint32_t partitions, double p);

  // Given an integer |val|, the Encode() method returns an index in the range [0, |partitions|] in
  // two steps:
  //
  // (Step 1) Randomized fixed-point encoding. The integer |val| is snapped to one of the
  // (|partitions| + 1) boundary points of the |partitions| subsegments of the range [|min_int|,
  // |max_int|]. If |val| is in the range (|min_int|, |max_int|) and is not equal to a boundary
  // point, then |val| is snapped to one of the two nearest boundary points, with probability
  // proportional to its distance from each of those points. Out-of-range values are snapped to
  // |min_int| (if underflow) or to |max_int| (if overflow).
  //
  // (Step 2) Randomized response. The index of the boundary point selected in (Step 1) is encoded
  // using randomized response on the set [0, |partitions|].
  uint32_t Encode(int64_t val);

 private:
  // Returns the index of the largest element of |boundaries_| which is less than or equal to |val|,
  // or 0 if |val| is less than the smallest element of |boundaries_|.
  uint32_t GetLeftIndex(int64_t val);

  // Given an integer |val| and the index |left_index| of the largest element of |boundaries_| which
  // is less than or equal to |val|, returns either |left_index| or (|left_index| + 1) with
  // probability proportional to the distance of |val| from those two respective elements of
  // |boundaries_|.
  uint32_t RandomRound(int64_t val, uint32_t left_index);

  BitGeneratorInterface<uint32_t>* gen_;
  // The boundaries of the |partitions| equal subsegments of [|min_int|, |max_int|], sorted in
  // increasing order.
  std::vector<int64_t> boundaries_;
  std::unique_ptr<ArchivedResponseRandomizer> randomizer_;
};

// Estimates the true sum of a vector of values, each of which was encoded with an ArchivedKrrIntegerEncoder.
class ArchivedKrrIntegerSumEstimator {
 public:
  // These parameters should be the same as the ones used in the ArchivedKrrIntegerEncoder which encoded the
  // values. See the ArchivedKrrIntegerEncoder constructor for their meaning.
  ArchivedKrrIntegerSumEstimator(int64_t min_int, int64_t max_int, uint32_t partitions, double p);

  // Computes an unbiased estimate of the sum of a vector of encoded integers
  //
  // The elements of |encoded_vals| are indices into the range [0, |partitions|] specified in the
  // constructor. First, an unbiased estimator of the frequency of each index is computed . Next,
  // the frequency estimate for each index i is multiplied by the numeric value corresponding to
  // that index, namely:
  //
  // |min_int| + ( i * (|max_int| - |min_int|) / |partitions| ).
  //
  // The method returns the sum of these scaled frequencies over all indices in range [0,
  // |partitions|].
  double ComputeSum(const std::vector<uint32_t>& encoded_vals);

 private:
  // The boundaries of the |partitions| equal subsegments of [|min_int|, |max_int|], sorted in
  // increasing order.
  std::vector<int64_t> boundaries_;
  // The sum of |boundaries_|.
  int64_t boundary_sum_;
  std::unique_ptr<ArchivedFrequencyEstimator> frequency_estimator_;
};

}  // namespace cobalt

#endif  // COBALT_SRC_ALGORITHMS_EXPERIMENTAL_ARCHIVED_KRR_INTEGER_ENCODER_H_

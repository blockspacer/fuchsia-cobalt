// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_EXPERIMENTAL_ARCHIVED_TWO_DIM_RAPPOR_HISTOGRAM_ENCODER_H_
#define COBALT_SRC_ALGORITHMS_EXPERIMENTAL_ARCHIVED_TWO_DIM_RAPPOR_HISTOGRAM_ENCODER_H_

#include "src/algorithms/experimental/random.h"

namespace cobalt {

// An encoder that operates on histograms. Each pair (bucket_index, bucket_count) in the input
// histogram is represented as a 1-hot bitvector of length
// L = |num_buckets| * (|max_count| + 1)
// using a fixed mapping of the set {0,...,|num_buckets| - 1} x {0,...,|max_count|} to {1,...,L}.
// The |num_buckets| 1-hot vectors are each encoded using Basic RAPPOR; i.e., each bit is flipped
// with probability |p|. The encoder then returns the multiset of pairs (bucket_index, bucket_count)
// corresponding to the "1" bits among the |num_buckets| encoded vectors. Pairs of the form
// (bucket_index, 0) are not included in the result.
class ArchivedTwoDimRapporHistogramEncoder {
 public:
  // |num_buckets| is the number of buckets in each input histogram, |max_count| is the maximum
  // count that should be reported for any bucket, and |p| is the probability of a bit flip during
  // Basic RAPPOR encoding.
  ArchivedTwoDimRapporHistogramEncoder(BitGeneratorInterface<uint32_t>* gen, uint32_t num_buckets,
                                       uint64_t max_count, double p);

  // |histogram| is a vector of length |num_buckets|, where the n-th element is the count for that
  // bucket. Counts should be in the range [0, |max_count|] inclusive; larger bucket counts are
  // clipped to |max_count| before encoding.
  std::vector<std::pair<uint32_t, uint64_t>> Encode(const std::vector<uint64_t>& histogram);

 private:
  BitGeneratorInterface<uint32_t>* gen_;
  uint32_t num_buckets_;
  uint64_t max_count_;
  double p_;
};

// Estimates the true sum of a collection of histograms, each of which was encoded with an
// ArchivedTwoDimRapporHistogramEncoder.
class ArchivedTwoDimRapporHistogramSumEstimator {
 public:
  // |num_buckets| is the number of histogram buckets, |max_count| is the maximum contribution of an
  // individual user to each bucket count, and |p| is a noise parameter. These should be equal to
  // the arguments that were used to construct the ArchivedTwoDimRapporHistogramEncoder which
  // produced the encoded histograms.
  ArchivedTwoDimRapporHistogramSumEstimator(uint32_t num_buckets, uint64_t max_count, double p);

  // |encoded_histograms| is a vector of encoded histograms produced by an
  // ArchivedTwoDimRapporHistogramEncoder, and |num_participants| is the number of users
  // contributing to the report. For each index n in the range [0, |num_buckets| - 1], ComputeSum()
  // returns an unbiased estimate of the sum of the counts in the n-th buckets of the encoded
  // histograms.
  std::vector<double> ComputeSum(
      const std::vector<std::vector<std::pair<uint32_t, uint64_t>>>& encoded_histograms,
      uint64_t num_participants);

 private:
  uint32_t num_buckets_;
  uint64_t max_count_;
  double p_;
};

}  // namespace cobalt

#endif  // COBALT_SRC_ALGORITHMS_EXPERIMENTAL_ARCHIVED_TWO_DIM_RAPPOR_HISTOGRAM_ENCODER_H_

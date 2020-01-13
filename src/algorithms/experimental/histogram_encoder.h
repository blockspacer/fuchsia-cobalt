// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_EXPERIMENTAL_HISTOGRAM_ENCODER_H_
#define COBALT_SRC_ALGORITHMS_EXPERIMENTAL_HISTOGRAM_ENCODER_H_

#include <tuple>

#include "src/algorithms/experimental/integer_encoder.h"

namespace cobalt {

class HistogramEncoder {
 public:
  // |num_buckets| is the number of histogram buckets, and |integer_encoder| is the IntegerEncoder
  // that should be used to encode each bucket count.
  HistogramEncoder(uint32_t num_buckets, IntegerEncoder* integer_encoder);

  // |histogram| is a vector of length |num_buckets|, where the n-th element is the count for that
  // bucket. The encoding of that count is the n-th element of the return value.
  std::vector<uint32_t> Encode(const std::vector<int64_t>& histogram);

 private:
  uint32_t num_buckets_;
  IntegerEncoder* integer_encoder_;
};

class HistogramSumEstimator {
 public:
  // |num_buckets| is the number of histogram buckets, and |integer_encoder| is the IntegerEncoder
  // that should be used to compute the approximate sum of the counts for each bucket.
  HistogramSumEstimator(uint32_t num_buckets, IntegerSumEstimator* integer_sum_estimator);

  // |encoded_histograms| is a vector of encoded histograms, where each encoded histogram is a
  // vector of indices into the bucketing scheme of the IntegerSumEstimator used to construct this
  // HistogramSumEstimator. For each index n in the range [0, |num_buckets| - 1], ComputeSum()
  // returns the estimated sum of the counts in the n-th buckets of the encoded histograms, together
  // with the estimated underflow and overflow counts for that bucket.
  std::vector<std::tuple<double, uint64_t, uint64_t>> ComputeSum(
      const std::vector<std::vector<uint32_t>>& encoded_histograms);

 private:
  uint32_t num_buckets_;
  IntegerSumEstimator* integer_sum_estimator_;
};

}  // namespace cobalt

#endif  // COBALT_SRC_ALGORITHMS_EXPERIMENTAL_HISTOGRAM_ENCODER_H_

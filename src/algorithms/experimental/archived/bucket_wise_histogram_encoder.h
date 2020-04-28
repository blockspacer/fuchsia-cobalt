// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_EXPERIMENTAL_ARCHIVED_BUCKET_WISE_HISTOGRAM_ENCODER_H_
#define COBALT_SRC_ALGORITHMS_EXPERIMENTAL_ARCHIVED_BUCKET_WISE_HISTOGRAM_ENCODER_H_

#include "src/algorithms/experimental/archived/krr_integer_encoder.h"

namespace cobalt {

// An encoder that operates on histograms. Each bucket count is independently encoded using a
// provided ArchivedIntegerEncoder.
class ArchivedBucketWiseHistogramEncoder {
 public:
  // |num_buckets| is the number of histogram buckets, and |integer_encoder| is the IntegerEncoder
  // that should be used to encode each bucket count.
  ArchivedBucketWiseHistogramEncoder(uint32_t num_buckets,
                                     ArchivedKrrIntegerEncoder* integer_encoder);

  // |histogram| is a vector of length |num_buckets|, where the n-th element is the value for that
  // bucket. Values should be within the bounds that |integer_encoder| was constructed with. The
  // integer encoder's encoding of the n-th bucket count is the n-th element of the return value.
  std::vector<uint32_t> Encode(const std::vector<int64_t>& histogram);

 private:
  uint32_t num_buckets_;
  ArchivedKrrIntegerEncoder* integer_encoder_;
};

// Estimates the true sum of a collection of histograms, each of which was encoded with a
// ArchivedBucketWiseHistogramSumEstimator.
class ArchivedBucketWiseHistogramSumEstimator {
 public:
  // |num_buckets| is the number of histogram buckets, and |integer_sum_estimator| is the estimator
  // that should be used to compute the approximate sum of the counts for each bucket.
  ArchivedBucketWiseHistogramSumEstimator(uint32_t num_buckets,
                                          ArchivedKrrIntegerSumEstimator* integer_sum_estimator);

  // |encoded_histograms| is a vector of encoded histograms, where each encoded histogram is a
  // vector of indices into the bucketing scheme of the ArchivedKrrIntegerSumEstimator used to
  // construct this ArchivedBucketWiseHistogramSumEstimator. For each index n in the range [0,
  // |num_buckets| - 1], ComputeSum() returns an unbiased estimate of the sum of the counts in the
  // n-th buckets of the encoded histograms.
  std::vector<double> ComputeSum(const std::vector<std::vector<uint32_t>>& encoded_histograms);

 private:
  uint32_t num_buckets_;
  ArchivedKrrIntegerSumEstimator* integer_sum_estimator_;
};

}  // namespace cobalt

#endif  // COBALT_SRC_ALGORITHMS_EXPERIMENTAL_ARCHIVED_BUCKET_WISE_HISTOGRAM_ENCODER_H_

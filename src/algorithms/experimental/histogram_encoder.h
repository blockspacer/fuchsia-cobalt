// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_EXPERIMENTAL_HISTOGRAM_ENCODER_H_
#define COBALT_SRC_ALGORITHMS_EXPERIMENTAL_HISTOGRAM_ENCODER_H_

#include "src/algorithms/experimental/integer_encoder.h"
#include "src/algorithms/experimental/random.h"
#include "src/algorithms/experimental/randomized_response.h"

namespace cobalt {

// An encoder that operates on histograms. Each bucket count is independently encoded using a
// provided IntegerEncoder.
class BucketWiseHistogramEncoder {
 public:
  // |num_buckets| is the number of histogram buckets, and |integer_encoder| is the IntegerEncoder
  // that should be used to encode each bucket count.
  BucketWiseHistogramEncoder(uint32_t num_buckets, IntegerEncoder* integer_encoder);

  // |histogram| is a vector of length |num_buckets|, where the n-th element is the value for that
  // bucket. Values should be within the bounds that the IntegerEncoder was constructed with. The
  // IntegerEncoder's encoding of the n-th bucket count is the n-th element of the return value.
  std::vector<uint32_t> Encode(const std::vector<int64_t>& histogram);

 private:
  uint32_t num_buckets_;
  IntegerEncoder* integer_encoder_;
};

// Estimates the true sum of a collection of histograms, each of which was encoded with a
// BucketWiseHistogramSumEstimator.
class BucketWiseHistogramSumEstimator {
 public:
  // |num_buckets| is the number of histogram buckets, and |integer_encoder| is the IntegerEncoder
  // that should be used to compute the approximate sum of the counts for each bucket.
  BucketWiseHistogramSumEstimator(uint32_t num_buckets, IntegerSumEstimator* integer_sum_estimator);

  // |encoded_histograms| is a vector of encoded histograms, where each encoded histogram is a
  // vector of indices into the bucketing scheme of the IntegerSumEstimator used to construct this
  // HistogramSumEstimator. For each index n in the range [0, |num_buckets| - 1], ComputeSum()
  // returns an unbiased estimate of the sum of the counts in the n-th buckets of the encoded
  // histograms.
  std::vector<double> ComputeSum(const std::vector<std::vector<uint32_t>>& encoded_histograms);

 private:
  uint32_t num_buckets_;
  IntegerSumEstimator* integer_sum_estimator_;
};

// An encoder that operates on histograms. Each occurrence of a bucket index is encoded using
// randomized response on the set of buckets, and the resulting 1-hot histograms are summed to form
// the encoded histogram.
class OccurrenceWiseHistogramEncoder {
 public:
  // |num_buckets| is the number of buckets in each input histogram, |max_count| is the maximum
  // count that should be reported for any bucket, and |p| is the noise parameter for each instance
  // of randomized response.
  OccurrenceWiseHistogramEncoder(BitGeneratorInterface<uint32_t>* gen, uint32_t num_buckets,
                                 uint64_t max_count, double p);

  // |histogram| is a vector of length |num_buckets|, where the n-th element is the count for that
  // bucket. Counts should be in the range [0, |max_count|] inclusive; larger bucket counts are
  // clipped to |max_count| before encoding.
  std::vector<uint64_t> Encode(const std::vector<uint64_t>& histogram);

 private:
  uint32_t num_buckets_;
  uint64_t max_count_;
  std::unique_ptr<ResponseRandomizer> randomizer_;
};

// Estimates the true sum of a collection of histograms, each of which was encoded with an
// OccurrenceWiseHistogramSumEstimator.
class OccurrenceWiseHistogramSumEstimator {
 public:
  // |num_buckets| is the number of histogram buckets, and |p| is a noise parameter. These should be
  // equal to the arguments that were used to construct the OccurrenceWiseHistogramEncoder which
  // produced the encoded histograms.
  explicit OccurrenceWiseHistogramSumEstimator(uint32_t num_buckets, double p);

  // |encoded_histograms| is a vector of encoded histograms produced by an
  // OccurrenceWiseHistogramEncoder. For each index n in the range [0, |num_buckets| - 1],
  // ComputeSum() returns an unbiased estimate of the sum of the counts in the n-th buckets of the
  // encoded histograms.
  std::vector<double> ComputeSum(const std::vector<std::vector<uint64_t>>& encoded_histograms);

 private:
  std::unique_ptr<FrequencyEstimator> frequency_estimator_;
};

// An encoder that operates on histograms. Each pair (bucket_index, bucket_count) in the input
// histogram is represented as a 1-hot bitvector of length
// L = |num_buckets| * (|max_count| + 1)
// using a fixed mapping of the set {0,...,|num_buckets| - 1} x {0,...,|max_count|} to {1,...,L}.
// The |num_buckets| 1-hot vectors are each encoded using Basic RAPPOR; i.e., each bit is flipped
// with probability |p|. The encoder then returns the multiset of pairs (bucket_index, bucket_count)
// corresponding to the "1" bits among the |num_buckets| encoded vectors. Pairs of the form
// (bucket_index, 0) are not included in the result.
class TwoDimRapporHistogramEncoder {
 public:
  // |num_buckets| is the number of buckets in each input histogram, |max_count| is the maximum
  // count that should be reported for any bucket, and |p| is the probability of a bit flip during
  // Basic RAPPOR encoding.
  TwoDimRapporHistogramEncoder(BitGeneratorInterface<uint32_t>* gen, uint32_t num_buckets,
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

// Estimates the true sum of a collection of histograms, each of which was encoded with a
// TwoDimRapporHistogramEncoder.
class TwoDimRapporHistogramSumEstimator {
 public:
  // |num_buckets| is the number of histogram buckets, |max_count| is the maximum contribution of an
  // individual user to each bucket count, and |p| is a noise parameter. These should be equal to
  // the arguments that were used to construct the OccurrenceWiseHistogramEncoder which produced the
  // encoded histograms.
  TwoDimRapporHistogramSumEstimator(uint32_t num_buckets, uint64_t max_count, double p);

  // |encoded_histograms| is a vector of encoded histograms produced by an
  // OccurrenceWiseHistogramEncoder, and |num_participants| is the number of users contributing to
  // the report. For each index n in the range [0, |num_buckets| - 1], ComputeSum()
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

#endif  // COBALT_SRC_ALGORITHMS_EXPERIMENTAL_HISTOGRAM_ENCODER_H_

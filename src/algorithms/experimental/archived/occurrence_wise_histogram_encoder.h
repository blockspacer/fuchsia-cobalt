// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_EXPERIMENTAL_ARCHIVED_OCCURRENCE_WISE_HISTOGRAM_ENCODER_H_
#define COBALT_SRC_ALGORITHMS_EXPERIMENTAL_ARCHIVED_OCCURRENCE_WISE_HISTOGRAM_ENCODER_H_

#include "src/algorithms/experimental/archived/response_randomizer.h"
#include "src/algorithms/experimental/random.h"

namespace cobalt {

// An encoder that operates on histograms. Each occurrence of a bucket index is encoded using
// randomized response on the set of buckets, and the resulting 1-hot histograms are summed to form
// the encoded histogram.
class ArchivedOccurrenceWiseHistogramEncoder {
 public:
  // |num_buckets| is the number of buckets in each input histogram, |max_count| is the maximum
  // count that should be reported for any bucket, and |p| is the noise parameter for each instance
  // of randomized response.
  ArchivedOccurrenceWiseHistogramEncoder(BitGeneratorInterface<uint32_t>* gen, uint32_t num_buckets,
                                         uint64_t max_count, double p);

  // |histogram| is a vector of length |num_buckets|, where the n-th element is the count for that
  // bucket. Counts should be in the range [0, |max_count|] inclusive; larger bucket counts are
  // clipped to |max_count| before encoding.
  std::vector<uint64_t> Encode(const std::vector<uint64_t>& histogram);

 private:
  uint32_t num_buckets_;
  uint64_t max_count_;
  std::unique_ptr<ArchivedResponseRandomizer> randomizer_;
};

// Estimates the true sum of a collection of histograms, each of which was encoded with an
// OccurrenceWiseHistogramSumEstimator.
class ArchivedOccurrenceWiseHistogramSumEstimator {
 public:
  // |num_buckets| is the number of histogram buckets, and |p| is a noise parameter. These should be
  // equal to the arguments that were used to construct the ArchivedOccurrenceWiseHistogramEncoder
  // which produced the encoded histograms.
  explicit ArchivedOccurrenceWiseHistogramSumEstimator(uint32_t num_buckets, double p);

  // |encoded_histograms| is a vector of encoded histograms produced by an
  // ArchivedOccurrenceWiseHistogramEncoder. For each index n in the range [0, |num_buckets| - 1],
  // ComputeSum() returns an unbiased estimate of the sum of the counts in the n-th buckets of the
  // encoded histograms.
  std::vector<double> ComputeSum(const std::vector<std::vector<uint64_t>>& encoded_histograms);

 private:
  std::unique_ptr<ArchivedFrequencyEstimator> frequency_estimator_;
};

}  // namespace cobalt

#endif  // COBALT_SRC_ALGORITHMS_EXPERIMENTAL_ARCHIVED_OCCURRENCE_WISE_HISTOGRAM_ENCODER_H_

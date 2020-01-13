// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_EXPERIMENTAL_INTEGER_ENCODER_H_
#define COBALT_SRC_ALGORITHMS_EXPERIMENTAL_INTEGER_ENCODER_H_

#include "src/algorithms/experimental/random.h"
#include "src/algorithms/experimental/randomized_response.h"
#include "src/registry/buckets_config.h"
#include "src/registry/report_definition.pb.h"

namespace cobalt {

// Encodes integer values using a fixed-precision encoding followed by randomized response.
//
// This scheme is based on Algorithm 2 of "The privacy blanket of the shuffle model" (Balle, Bell,
// Gascon, Nissim; 2019), adapted slightly to accept integers in any specified range and to handle
// out-of-bounds values.
class IntegerEncoder {
 public:
  // A strategy for computing the bucket index corresponding to an integer. If the RoundingStrategy
  // is |kFloor|, the chosen index will be the largest one such that the floor of that bucket is
  // less than or equal to the integer. If |kRandom|, the chosen index will either be the same as
  // for |kFloor|, or the following index, with probability proportional to the distance from the
  // integer to the floors of those buckets.
  enum RoundingStrategy { kFloor, kRandom };

  // Constructors:
  // The parameter |p| is the probability that Encode() returns a data-independent result, and
  // should be in the range [0.0, 1.0] inclusive. If |random_round| is true, then a value
  // falling between the floor of bucket i and the floor of bucket i+1 (inclusive) is assigned to
  // one of the two buckets with probability proportional to its position in the interval. The bit
  // generator |gen| provides entropy for randomized response and (if used) randomized rounding.
  //
  // Constructor using an IntegerBuckets proto |buckets| to specify the bucket ranges. This is
  // assumed to be of type LinearIntegerBuckets.
  IntegerEncoder(BitGeneratorInterface<uint32_t>* gen, const IntegerBuckets& buckets, double p,
                 const RoundingStrategy& rounding_strategy);
  // Constructor using parameters to specify the bucket ranges. The difference (|max_int| -
  // |min_int|) must be a multiple of |partitions|. The resulting number of buckets is equal to
  // |partitions|, equally dividing the interval [|min_int|, |max_int|]. Additionally, underflow and
  // overflow buckets are allocated.
  IntegerEncoder(BitGeneratorInterface<uint32_t>* gen, int64_t min_int, int64_t max_int,
                 uint32_t partitions, double p, const RoundingStrategy& rounding_strategy);

  // Places an integer |val| into a bucket according to the specified bucketing scheme, then encodes
  // the bucket index using randomized response.
  uint32_t Encode(int64_t val);

 private:
  // Given an integer |val| which is greater than or equal to the floor of bucket |bucket_index| and
  // less than the floor of bucket (|bucket_index| + 1), returns either |bucket_index| or
  // (|bucket_index| + 1). The probability of returning the larger bucket index is equal to
  // (|val| - b_i) / (b_{i+1} - b_i),
  // where b_i denotes the floor of the bucket containing |val|.
  //
  // If |bucket_index| is the index of the underflow or overflow bucket, then the method
  // deterministically returns |bucket_index|.
  //
  // TODO(pesk): Currently, values in the largest numeric bucket can be rounded into the overflow
  // bucket. Is this desirable? If so, which parameters to the IntegerSumEstimator should be used
  // for values encoded with random rounding?
  uint32_t RandomRound(int64_t val, uint32_t bucket_index);

  BitGeneratorInterface<uint32_t>* gen_;
  std::unique_ptr<config::IntegerBucketConfig> bucket_config_;
  std::unique_ptr<ResponseRandomizer> randomizer_;
  RoundingStrategy rounding_strategy_;
};

// Estimates the true sum of a vector of values, each of which was encoded with an IntegerEncoder.
class IntegerSumEstimator {
 public:
  // A strategy for handling out-of-bounds values. If an encoded value is outside the specified
  // range of bucket indices and the strategy is |kClamp|, that index will be converted to a numeric
  // value equal to the nearest endpoint of a numeric bucket. If the strategy is |kDiscard|, the
  // encoded value will not be counted toward the sum.
  enum OutOfBoundsStrategy { kDiscard, kClamp };

  // Constructors:
  // The parameter |p| is the probability that each encoded value was data-independent.
  // This should be the same parameter |p| used to construct the IntegerEncoder which produced the
  // encoded values.
  //
  // Constructor using an IntegerBuckets proto |buckets| to specify the bucket ranges. This is
  // assumed to be of type LinearIntegerBuckets.
  IntegerSumEstimator(const IntegerBuckets& buckets, double p,
                      const OutOfBoundsStrategy& underflow_strategy,
                      const OutOfBoundsStrategy& overflow_strategy);
  // Constructor using parameters |min_int|, |max_int|, and |partitions| to specify the bucket
  // ranges. The resulting number of buckets is equal to |partitions|, equally dividing the interval
  // [|min_int|, |max_int|]. Additionally, underflow and overflow buckets are allocated.
  IntegerSumEstimator(int64_t min_int, int64_t max_int, uint32_t partitions, double p,
                      const OutOfBoundsStrategy& underflow_strategy,
                      const OutOfBoundsStrategy& overflow_strategy);

  // Computes an unbiased estimate of the sum of a vector of encoded integers, as well as the
  // standard error of the estimate.
  //
  // The elements of |encoded_vals| are indices into the bucketing scheme specified in the
  // constructor. ComputeSum() converts each index to a numeric value, which is currently the floor
  // of the bucket with that index, and sums the resulting values. The raw sum is debiased and
  // returned as the first element of the result.
  //
  // Encoded values equal to the index of the underflow or overflow bucket do not contribute to the
  // estimated sum. However, these values do contribute to the total size of the input, which is
  // used to compute the bias and standard error. The counts for the underflow and overflow buckets
  // are returned as the second and third elements of the result, respectively.
  std::tuple<double, uint64_t, uint64_t> ComputeSum(const std::vector<uint32_t>& encoded_vals);

 private:
  // Populates a vector with a multiplier for each bucket index. ComputeSum() replaces each
  // occurrence of an index with its multiplier.
  //
  // At return time, the first and last entries of |multipliers| are the multipliers for the
  // underflow and overflow buckets, respectively. These multipliers are set according to
  // |underflow_strategy| and |overflow_strategy|.  Each remaining entry is equal to the floor of
  // the corresponding bucket.
  void GetBucketMultipliers(std::vector<double>* multipliers,
                            const OutOfBoundsStrategy& underflow_strategy,
                            const OutOfBoundsStrategy& overflow_strategy);

  // Given the |raw_sum| of the multipliers corresponding to a set of |input_size| encoded values,
  // compute an unbiased estimate of sum of the multipliers corresponding to the true values.
  //
  // Writing M for the sum of |multipliers_|, and taking |num_buckets| to be the total number of
  // buckets including under/overflow, the bias due to response randomization is:
  //
  // bias = ((|input_size| * p_) / |num_buckets|) + M
  //
  // This method returns |raw_sum| - |bias|.
  double DebiasSum(int64_t raw_sum, uint64_t input_size);

  std::unique_ptr<config::IntegerBucketConfig> bucket_config_;
  std::unique_ptr<FrequencyEstimator> frequency_estimator_;
  std::vector<double> multipliers_;
  double multiplier_sum_;
  double p_;
};

}  // namespace cobalt

#endif  // COBALT_SRC_ALGORITHMS_EXPERIMENTAL_INTEGER_ENCODER_H_

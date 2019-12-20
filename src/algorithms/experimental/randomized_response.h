// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_EXPERIMENTAL_RANDOMIZED_RESPONSE_H_
#define COBALT_SRC_ALGORITHMS_EXPERIMENTAL_RANDOMIZED_RESPONSE_H_

#include "src/algorithms/experimental/distributions.h"
#include "src/algorithms/experimental/random.h"

namespace cobalt {

// Encodes 1-hot histograms using k-randomized response.
class ResponseRandomizer {
 public:
  // |gen| : An implementation of BitGeneratorInterface, used to provide entropy to the randomizer.
  // |max_index| : The largest bucket index of the histogram. It is assumed that the minimum bucket
  // index is 0.
  // |p| : The probability that the output of the Encode() method is independent of its input.
  ResponseRandomizer(BitGeneratorInterface<uint32_t>* gen, uint32_t max_index, double p);
  // Encode a 1-hot histogram, represented as a bucket index |index|.
  // A coin with weight |p| is flipped. If it comes up heads (with probability |p|), the method
  // returns a sample from the uniform distribution on the integer range [0, |max_index|]. Otherwise
  // (with probability (1 - |p|), the method returns the input |index|.
  //
  // Inputs larger than |max_index| are replaced with |max_index| before encoding.
  // TODO(pesk): Consider other ways of handling bad input.
  uint32_t Encode(uint32_t index);

 private:
  uint32_t max_index_;
  BernoulliDistribution bernoulli_dist_;
  DiscreteUniformDistribution uniform_dist_;
};

// Aggregates messages which each consist of a single encoded bucket index, producing a histogram of
// the number of occurrences of each bucket index.
class FrequencyEstimator {
 public:
  // |max_index|: The largest bucket index that may be contained in an input message. It is assumed
  // that the minimum bucket index is 0.
  explicit FrequencyEstimator(uint32_t max_index);
  // |indices|: A vector of bucket indices, where indices may be repeated and need not be in any
  // particular order.
  //
  // Creates and returns a vector of size (|max_index| + 1), where the k-th element is the number of
  // occurrences of index k in |indices|. If an element of |indices| is greater than |max_index|, it
  // is disregarded.
  std::vector<uint64_t> GetFrequencies(const std::vector<uint32_t>& indices);

 private:
  uint32_t max_index_;
};

}  // namespace cobalt
#endif  // COBALT_SRC_ALGORITHMS_EXPERIMENTAL_RANDOMIZED_RESPONSE_H_

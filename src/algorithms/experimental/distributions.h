// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_EXPERIMENTAL_DISTRIBUTIONS_H_
#define COBALT_SRC_ALGORITHMS_EXPERIMENTAL_DISTRIBUTIONS_H_

#include <random>

#include "src/algorithms/experimental/random.h"

namespace cobalt {

// Abstract class that provides samples from a discrete distribution.
template <typename sample_type>
class DiscreteDistribution {
 public:
  virtual sample_type Sample() = 0;
  virtual ~DiscreteDistribution() = default;
};

// Provides samples from the poisson distribution with mean |mean|.
// Entropy is obtained from a uniform random bit generator |gen|.
class PoissonDistribution : public DiscreteDistribution<int> {
 public:
  PoissonDistribution(BitGeneratorInterface<uint32_t>* gen, int mean);
  int Sample() override;

 private:
  BitGeneratorInterface<uint32_t>* gen_;
  std::poisson_distribution<int> dist_;
};

// Provides samples from the uniform distribution over the integers from |min| to |max|, inclusive.
// Entropy is obtained from a uniform random bit generator |gen|.
class DiscreteUniformDistribution : public DiscreteDistribution<uint32_t> {
 public:
  DiscreteUniformDistribution(BitGeneratorInterface<uint32_t>* gen, uint32_t min, uint32_t max);
  DiscreteUniformDistribution() = default;
  uint32_t Sample() override;

 private:
  BitGeneratorInterface<uint32_t>* gen_;
  std::uniform_int_distribution<uint32_t> dist_;
};

// Provides samples from the Bernoulli distribution with parameter |p|. Entropy is obtained from a
// uniform random bit generator |gen|.
class BernoulliDistribution : public DiscreteDistribution<bool> {
 public:
  BernoulliDistribution(BitGeneratorInterface<uint32_t>* gen, double p);
  BernoulliDistribution() = default;
  bool Sample() override;

 private:
  BitGeneratorInterface<uint32_t>* gen_;
  std::bernoulli_distribution dist_;
};

}  // namespace cobalt

#endif  // COBALT_SRC_ALGORITHMS_EXPERIMENTAL_DISTRIBUTIONS_H_

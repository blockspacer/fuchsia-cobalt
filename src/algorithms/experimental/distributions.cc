#include "src/algorithms/experimental/distributions.h"

#include <random>

#include "src/algorithms/experimental/random.h"

namespace cobalt {

DiscreteUniformDistribution::DiscreteUniformDistribution(BitGeneratorInterface<uint32_t>* gen,
                                                         uint32_t min, uint32_t max)
    : gen_(gen) {
  dist_ = std::uniform_int_distribution<uint32_t>(min, max);
}

uint32_t DiscreteUniformDistribution::Sample() { return dist_(*gen_); }

BernoulliDistribution::BernoulliDistribution(BitGeneratorInterface<uint32_t>* gen, double p)
    : gen_(gen) {
  dist_ = std::bernoulli_distribution(p);
}

bool BernoulliDistribution::Sample() { return dist_(*gen_); }

}  // namespace cobalt

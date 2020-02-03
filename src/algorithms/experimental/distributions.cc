#include "src/algorithms/experimental/distributions.h"

#include <random>

#include "src/algorithms/experimental/random.h"

namespace cobalt {

PoissonDistribution::PoissonDistribution(BitGeneratorInterface<uint32_t>* gen, int mean)
    : gen_(gen) {
  dist_ = std::poisson_distribution<int>(mean);
}

int PoissonDistribution::Sample() { return dist_(*gen_); }

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

#include "src/algorithms/experimental/randomized_response.h"

#include <algorithm>

#include "src/algorithms/experimental/distributions.h"
#include "src/algorithms/experimental/random.h"

namespace cobalt {

ResponseRandomizer::ResponseRandomizer(BitGeneratorInterface<uint32_t>* gen, uint32_t max_index,
                                       double p)
    : max_index_(max_index) {
  bernoulli_dist_ = BernoulliDistribution(gen, p);
  uniform_dist_ = DiscreteUniformDistribution(gen, 0u, max_index);
}

uint32_t ResponseRandomizer::Encode(uint32_t index) {
  uint32_t response = std::min(max_index_, index);
  uint32_t random_response = uniform_dist_.Sample();
  if (bernoulli_dist_.Sample()) {
    response = random_response;
  }
  return response;
}

FrequencyEstimator::FrequencyEstimator(uint32_t max_index) : max_index_(max_index) {}

std::vector<uint64_t> FrequencyEstimator::GetFrequencies(const std::vector<uint32_t>& indices) {
  std::vector<uint64_t> frequencies(max_index_ + 1, 0);
  for (uint32_t index : indices) {
    if (index <= max_index_) {
      ++frequencies[index];
    }
  }
  return frequencies;
}

}  // namespace cobalt

#include "src/algorithms/experimental/archived/response_randomizer.h"

#include <algorithm>

#include "src/algorithms/experimental/distributions.h"
#include "src/algorithms/experimental/random.h"

namespace cobalt {

ArchivedResponseRandomizer::ArchivedResponseRandomizer(BitGeneratorInterface<uint32_t>* gen,
                                                       uint32_t max_index, double p)
    : max_index_(max_index) {
  bernoulli_dist_ = BernoulliDistribution(gen, p);
  uniform_dist_ = DiscreteUniformDistribution(gen, 0u, max_index);
}

uint32_t ArchivedResponseRandomizer::Encode(uint32_t index) {
  uint32_t response = std::min(max_index_, index);
  uint32_t random_response = uniform_dist_.Sample();
  if (bernoulli_dist_.Sample()) {
    response = random_response;
  }
  return response;
}

ArchivedFrequencyEstimator::ArchivedFrequencyEstimator(uint32_t max_index, double p)
    : max_index_(max_index), p_(p) {}

std::vector<double> ArchivedFrequencyEstimator::GetFrequenciesFromIndices(
    const std::vector<uint32_t>& indices) {
  std::vector<uint64_t> raw_frequencies(max_index_ + 1, 0);
  for (uint32_t index : indices) {
    if (index <= max_index_) {
      ++raw_frequencies[index];
    }
  }
  return GetDebiasedFrequencies(raw_frequencies, indices.size());
}

std::vector<double> ArchivedFrequencyEstimator::GetFrequenciesFromHistograms(
    const std::vector<std::vector<uint64_t>>& histograms) {
  uint64_t input_size = 0;
  std::vector<uint64_t> raw_frequencies(max_index_ + 1, 0);
  for (const auto& histogram : histograms) {
    for (uint32_t i = 0; i <= max_index_; i++) {
      raw_frequencies[i] += histogram[i];
      input_size += histogram[i];
    }
  }
  return GetDebiasedFrequencies(raw_frequencies, input_size);
}

std::vector<double> ArchivedFrequencyEstimator::GetDebiasedFrequencies(
    const std::vector<uint64_t>& raw_frequencies, uint64_t input_size) {
  double shift = (static_cast<double>(input_size) * p_) / (static_cast<double>(max_index_ + 1));
  double scale = (1.0 - p_);
  std::vector<double> debiased_frequencies(max_index_ + 1);
  for (uint32_t i = 0; i <= max_index_; i++) {
    debiased_frequencies[i] = (static_cast<double>(raw_frequencies[i]) - shift) / scale;
  }
  return debiased_frequencies;
}

}  // namespace cobalt

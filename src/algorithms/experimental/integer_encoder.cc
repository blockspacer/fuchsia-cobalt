#include "src/algorithms/experimental/integer_encoder.h"

#include "src/algorithms/experimental/distributions.h"
#include "src/algorithms/experimental/random.h"
#include "src/algorithms/experimental/randomized_response.h"

namespace cobalt {

namespace {

void PopulateBoundaries(int64_t min_int, int64_t max_int, uint32_t partitions,
                        std::vector<int64_t>* boundaries) {
  boundaries->resize(partitions + 1);
  int64_t step_size = (max_int - min_int) / partitions;
  for (int64_t i = 0; i <= partitions; i++) {
    (*boundaries)[i] = min_int + i * step_size;
  }
}

int64_t GetBoundarySum(const std::vector<int64_t>& boundaries) {
  int64_t sum = 0;
  for (const int64_t b : boundaries) {
    sum += b;
  }
  return sum;
}

}  // namespace

IntegerEncoder::IntegerEncoder(BitGeneratorInterface<uint32_t>* gen, int64_t min_int,
                               int64_t max_int, uint32_t partitions, double p)
    : gen_(gen) {
  PopulateBoundaries(min_int, max_int, partitions, &boundaries_);
  randomizer_ = std::make_unique<ResponseRandomizer>(gen_, partitions + 1, p);
}

uint32_t IntegerEncoder::Encode(int64_t val) { return RandomRound(val, GetLeftIndex(val)); }

uint32_t IntegerEncoder::GetLeftIndex(int64_t val) {
  uint32_t left = 0;
  uint32_t right = boundaries_.size() - 1;
  if (val <= boundaries_[left]) {
    return left;
  }
  if (val >= boundaries_[right]) {
    return right;
  }
  while (left + 1 < right) {
    uint32_t mid = left + (right - left) / 2;
    if (val < boundaries_[mid]) {
      right = mid;
    } else {
      left = mid;
    }
  }
  return left;
}

uint32_t IntegerEncoder::RandomRound(int64_t val, uint32_t left_index) {
  if (left_index == boundaries_.size() - 1) {
    return left_index;
  }
  int64_t lower_bound = boundaries_[left_index];
  int64_t width = (boundaries_[left_index + 1] - lower_bound);
  double pos = static_cast<double>(val - lower_bound) / width;
  return left_index += BernoulliDistribution(gen_, pos).Sample();
}

IntegerSumEstimator::IntegerSumEstimator(int64_t min_int, int64_t max_int, uint32_t partitions,
                                         double p)
    : p_(p) {
  PopulateBoundaries(min_int, max_int, partitions, &boundaries_);
  boundary_sum_ = GetBoundarySum(boundaries_);
  frequency_estimator_ = std::make_unique<FrequencyEstimator>(partitions);
}

double IntegerSumEstimator::ComputeSum(const std::vector<uint32_t>& encoded_vals) {
  std::vector<uint64_t> frequencies = frequency_estimator_->GetFrequencies(encoded_vals);
  int64_t raw_sum = 0;
  for (uint32_t index = 0; index < frequencies.size(); index++) {
    raw_sum += frequencies[index] * boundaries_[index];
  }
  return GetDebiasedSum(raw_sum, encoded_vals.size());
}

double IntegerSumEstimator::GetDebiasedSum(int64_t raw_sum, uint64_t input_size) {
  double coeff = (static_cast<double>(input_size) * p_) / (boundaries_.size());
  return (static_cast<double>(raw_sum) - (coeff * boundary_sum_)) / (1.0 - p_);
}

}  // namespace cobalt

#include "src/algorithms/experimental/archived/krr_integer_encoder.h"

#include "src/algorithms/experimental/archived/response_randomizer.h"
#include "src/algorithms/experimental/distributions.h"
#include "src/algorithms/experimental/random.h"

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

ArchivedKrrIntegerEncoder::ArchivedKrrIntegerEncoder(BitGeneratorInterface<uint32_t>* gen,
                                                     int64_t min_int, int64_t max_int,
                                                     uint32_t partitions, double p)
    : gen_(gen) {
  PopulateBoundaries(min_int, max_int, partitions, &boundaries_);
  randomizer_ = std::make_unique<ArchivedResponseRandomizer>(gen_, partitions, p);
}

uint32_t ArchivedKrrIntegerEncoder::Encode(int64_t val) {
  return randomizer_->Encode(RandomRound(val, GetLeftIndex(val)));
}

uint32_t ArchivedKrrIntegerEncoder::GetLeftIndex(int64_t val) {
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

uint32_t ArchivedKrrIntegerEncoder::RandomRound(int64_t val, uint32_t left_index) {
  if (left_index == boundaries_.size() - 1) {
    return left_index;
  }
  int64_t lower_bound = boundaries_[left_index];
  int64_t width = (boundaries_[left_index + 1] - lower_bound);
  double pos = static_cast<double>(val - lower_bound) / width;
  return left_index += BernoulliDistribution(gen_, pos).Sample();
}

ArchivedKrrIntegerSumEstimator::ArchivedKrrIntegerSumEstimator(int64_t min_int, int64_t max_int,
                                                               uint32_t partitions, double p) {
  PopulateBoundaries(min_int, max_int, partitions, &boundaries_);
  boundary_sum_ = GetBoundarySum(boundaries_);
  frequency_estimator_ = std::make_unique<ArchivedFrequencyEstimator>(partitions, p);
}

double ArchivedKrrIntegerSumEstimator::ComputeSum(const std::vector<uint32_t>& encoded_vals) {
  std::vector<double> frequencies = frequency_estimator_->GetFrequenciesFromIndices(encoded_vals);
  double sum = 0.0;
  for (uint32_t index = 0; index < frequencies.size(); index++) {
    sum += frequencies[index] * boundaries_[index];
  }
  return sum;
}

}  // namespace cobalt

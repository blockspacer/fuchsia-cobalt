#include "src/algorithms/experimental/archived/occurrence_wise_histogram_encoder.h"

#include <algorithm>

#include "src/algorithms/experimental/archived/response_randomizer.h"
#include "src/algorithms/experimental/random.h"

namespace cobalt {

ArchivedOccurrenceWiseHistogramEncoder::ArchivedOccurrenceWiseHistogramEncoder(
    BitGeneratorInterface<uint32_t>* gen, uint32_t num_buckets, uint64_t max_count, double p)
    : num_buckets_(num_buckets), max_count_(max_count) {
  randomizer_ = std::make_unique<ArchivedResponseRandomizer>(gen, num_buckets - 1, p);
}

std::vector<uint64_t> ArchivedOccurrenceWiseHistogramEncoder::Encode(
    const std::vector<uint64_t>& histogram) {
  std::vector<uint64_t> encoded_histogram(num_buckets_);
  for (uint32_t bucket_index = 0u; bucket_index < histogram.size(); bucket_index++) {
    uint64_t bucket_count = std::min(histogram[bucket_index], max_count_);
    for (uint64_t i = 0u; i < bucket_count; i++) {
      encoded_histogram[randomizer_->Encode(bucket_index)]++;
    }
  }
  return encoded_histogram;
}

ArchivedOccurrenceWiseHistogramSumEstimator::ArchivedOccurrenceWiseHistogramSumEstimator(
    uint32_t num_buckets, double p) {
  frequency_estimator_ = std::make_unique<ArchivedFrequencyEstimator>(num_buckets - 1, p);
}

std::vector<double> ArchivedOccurrenceWiseHistogramSumEstimator::ComputeSum(
    const std::vector<std::vector<uint64_t>>& encoded_histograms) {
  return frequency_estimator_->GetFrequenciesFromHistograms(encoded_histograms);
}

}  // namespace cobalt

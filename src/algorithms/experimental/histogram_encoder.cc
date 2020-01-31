#include "src/algorithms/experimental/histogram_encoder.h"

#include <algorithm>
#include <tuple>

#include "src/algorithms/experimental/integer_encoder.h"
#include "src/algorithms/experimental/randomized_response.h"

namespace cobalt {

BucketWiseHistogramEncoder::BucketWiseHistogramEncoder(uint32_t num_buckets,
                                                       IntegerEncoder* integer_encoder)
    : num_buckets_(num_buckets), integer_encoder_(integer_encoder) {}

std::vector<uint32_t> BucketWiseHistogramEncoder::Encode(const std::vector<int64_t>& histogram) {
  std::vector<uint32_t> encoded(num_buckets_);
  for (uint32_t index = 0; index < num_buckets_; index++) {
    encoded[index] = integer_encoder_->Encode(histogram[index]);
  }
  return encoded;
}

BucketWiseHistogramSumEstimator::BucketWiseHistogramSumEstimator(
    uint32_t num_buckets, IntegerSumEstimator* integer_sum_estimator)
    : num_buckets_(num_buckets), integer_sum_estimator_(integer_sum_estimator) {}

std::vector<double> BucketWiseHistogramSumEstimator::ComputeSum(
    const std::vector<std::vector<uint32_t>>& encoded_histograms) {
  std::vector<double> decoded(num_buckets_);
  for (uint32_t index = 0; index < num_buckets_; index++) {
    std::vector<uint32_t> encoded_counts(encoded_histograms.size());
    for (size_t k = 0; k < encoded_histograms.size(); k++) {
      encoded_counts[k] = encoded_histograms[k][index];
    }
    decoded[index] = integer_sum_estimator_->ComputeSum(encoded_counts);
  }
  return decoded;
}

OccurrenceWiseHistogramEncoder::OccurrenceWiseHistogramEncoder(BitGeneratorInterface<uint32_t>* gen,
                                                               uint32_t num_buckets,
                                                               uint64_t max_count, double p)
    : num_buckets_(num_buckets), max_count_(max_count) {
  randomizer_ = std::make_unique<ResponseRandomizer>(gen, num_buckets - 1, p);
}

std::vector<uint64_t> OccurrenceWiseHistogramEncoder::Encode(
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

OccurrenceWiseHistogramSumEstimator::OccurrenceWiseHistogramSumEstimator(uint32_t num_buckets,
                                                                         double p) {
  frequency_estimator_ = std::make_unique<FrequencyEstimator>(num_buckets - 1, p);
}

std::vector<double> OccurrenceWiseHistogramSumEstimator::ComputeSum(
    const std::vector<std::vector<uint64_t>>& encoded_histograms) {
  return frequency_estimator_->GetFrequenciesFromHistograms(encoded_histograms);
}

}  // namespace cobalt

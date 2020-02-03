#include "src/algorithms/experimental/histogram_encoder.h"

#include <tuple>

#include "src/algorithms/experimental/integer_encoder.h"

namespace cobalt {

HistogramEncoder::HistogramEncoder(uint32_t num_buckets, IntegerEncoder* integer_encoder)
    : num_buckets_(num_buckets), integer_encoder_(integer_encoder) {}

std::vector<uint32_t> HistogramEncoder::Encode(const std::vector<int64_t>& histogram) {
  std::vector<uint32_t> encoded(num_buckets_);
  for (uint32_t index = 0; index < num_buckets_; index++) {
    encoded[index] = integer_encoder_->Encode(histogram[index]);
  }
  return encoded;
}

HistogramSumEstimator::HistogramSumEstimator(uint32_t num_buckets,
                                             IntegerSumEstimator* integer_sum_estimator)
    : num_buckets_(num_buckets), integer_sum_estimator_(integer_sum_estimator) {}

std::vector<double> HistogramSumEstimator::ComputeSum(
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

}  // namespace cobalt

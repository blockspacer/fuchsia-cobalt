#include "src/algorithms/experimental/archived/two_dim_rappor_histogram_encoder.h"

#include <algorithm>
#include <map>

#include "src/algorithms/experimental/distributions.h"
#include "src/algorithms/experimental/random.h"

namespace cobalt {

ArchivedTwoDimRapporHistogramEncoder::ArchivedTwoDimRapporHistogramEncoder(
    BitGeneratorInterface<uint32_t>* gen, uint32_t num_buckets, uint64_t max_count, double p)
    : gen_(gen), num_buckets_(num_buckets), max_count_(max_count), p_(p) {}

std::vector<std::pair<uint32_t, uint64_t>> ArchivedTwoDimRapporHistogramEncoder::Encode(
    const std::vector<uint64_t>& histogram) {
  std::vector<uint64_t> clipped_histogram(num_buckets_);
  for (uint32_t bucket_index = 0u; bucket_index < num_buckets_; bucket_index++) {
    clipped_histogram[bucket_index] = std::min(histogram[bucket_index], max_count_);
  }
  std::vector<std::pair<uint32_t, uint64_t>> encoded;
  auto dist_if_absent = BinomialDistribution(gen_, num_buckets_, p_);
  auto dist_0_to_1_if_present = BinomialDistribution(gen_, num_buckets_ - 1, p_);
  auto dist_1_to_1_if_present = BernoulliDistribution(gen_, 1.0 - p_);
  for (uint32_t bucket_index = 0u; bucket_index < num_buckets_; bucket_index++) {
    // We skip encoding and sending bucket counts of 0.
    for (uint64_t bucket_count = 1u; bucket_count <= max_count_; bucket_count++) {
      uint64_t encoded_count =
          (bucket_count == clipped_histogram[bucket_index]
               ? dist_0_to_1_if_present.Sample() + dist_1_to_1_if_present.Sample()
               : dist_if_absent.Sample());
      for (uint64_t num_occurrences = 0u; num_occurrences < encoded_count; num_occurrences++) {
        encoded.emplace_back(std::pair<uint32_t, uint64_t>(bucket_index, bucket_count));
      }
    }
  }
  return encoded;
}

ArchivedTwoDimRapporHistogramSumEstimator::ArchivedTwoDimRapporHistogramSumEstimator(
    uint32_t num_buckets, uint64_t max_count, double p)
    : num_buckets_(num_buckets), max_count_(max_count), p_(p) {}

std::vector<double> ArchivedTwoDimRapporHistogramSumEstimator::ComputeSum(
    const std::vector<std::vector<std::pair<uint32_t, uint64_t>>>& encoded_histograms,
    uint64_t num_participants) {
  // Compute the number of occurrences of each bit ID in |encoded_histograms|.
  std::map<std::pair<uint32_t, uint64_t>, uint64_t> raw_bit_counts;
  for (const auto& hist : encoded_histograms) {
    for (auto bit_id : hist) {
      raw_bit_counts[bit_id]++;
    }
  }

  // The total number of encoded 1-hot bitvectors which contributed to |encoded_histograms|.
  auto num_bitvectors = static_cast<double>(num_participants * num_buckets_);

  std::vector<double> sum(num_buckets_);
  for (uint32_t bucket_index = 0; bucket_index < num_buckets_; bucket_index++) {
    double total_count_for_bucket = 0.0;
    for (uint64_t bucket_count = 0; bucket_count <= max_count_; bucket_count++) {
      // Estimate the true number of occurrences of the bit with ID (|bucket_count|, |bucket_index|)
      // among the |num_bitvectors| encoded bitvectors. (See Erlingsson, Pihur, Korolova, "RAPPOR:
      // Randomized Aggregatable Privacy-Preserving Ordinal Response" section 4, taking f = 1.)
      double estimated_bit_count =
          (static_cast<double>(raw_bit_counts[{bucket_index, bucket_count}]) -
           p_ * num_bitvectors) /
          (1.0 - 2 * p_);
      // Clip the estimate into the range of possible true values.
      if (estimated_bit_count < 0.0) {
        estimated_bit_count = 0.0;
      }
      if (estimated_bit_count > num_bitvectors) {
        estimated_bit_count = num_bitvectors;
      }
      // Compute the contribution of the (estimated number of) bits with true ID (|bucket_index|,
      // |bucket_count|) to the total count for bucket |bucket_index|.
      total_count_for_bucket += static_cast<double>(bucket_count) * estimated_bit_count;
    }
    sum[bucket_index] = total_count_for_bucket;
  }
  return sum;
}

}  // namespace cobalt

#include "src/algorithms/experimental/histogram_encoder.h"

#include <tuple>

#include "src/algorithms/experimental/integer_encoder.h"
#include "src/algorithms/experimental/random.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

using testing::ElementsAre;

namespace cobalt {

class HistogramEncoderTest : public ::testing::Test {
 protected:
  void SetUp() override { gen_ = std::make_unique<RandomNumberGenerator>(); }

  RandomNumberGenerator* GetGenerator() { return gen_.get(); }

  std::unique_ptr<IntegerEncoder> MakeDeterministicIntegerEncoder(int64_t max_int,
                                                                  uint32_t partitions) {
    return std::make_unique<IntegerEncoder>(GetGenerator(), /*min_int=*/0, max_int, partitions,
                                            /*p=*/0.0,
                                            /*rounding_strategy=*/IntegerEncoder::kFloor);
  }

  std::unique_ptr<IntegerSumEstimator> MakeIntegerSumEstimatorDiscard(int64_t max_int,
                                                                      uint32_t partitions,
                                                                      double p) {
    return std::make_unique<IntegerSumEstimator>(
        /*min_int=*/0, max_int, partitions, p,
        /*underflow_strategy=*/IntegerSumEstimator::kDiscard,
        /*overflow_strategy=*/IntegerSumEstimator::kDiscard);
  }

  std::unique_ptr<IntegerSumEstimator> MakeIntegerSumEstimatorClamp(int64_t max_int,
                                                                    uint32_t partitions, double p) {
    return std::make_unique<IntegerSumEstimator>(/*min_int=*/0, max_int, partitions, p,
                                                 /*underflow_strategy=*/IntegerSumEstimator::kClamp,
                                                 /*overflow_strategy=*/IntegerSumEstimator::kClamp);
  }

 private:
  std::unique_ptr<RandomNumberGenerator> gen_;
};

TEST_F(HistogramEncoderTest, Encode) {
  // Parameters for encoding bucket counts
  int64_t max_count = 5;
  uint32_t partitions = 5;
  auto integer_encoder = MakeDeterministicIntegerEncoder(max_count, partitions);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_encoder = HistogramEncoder(num_buckets, integer_encoder.get());

  std::vector<int64_t> histogram = {-1, 0, 1, 2, 3, 4, 5, 6, 0, 1};
  std::vector<uint32_t> encoded = histogram_encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(0, 1, 2, 3, 4, 5, 6, 6, 1, 2));
}

TEST_F(HistogramEncoderTest, EncodeWithCoarseIntegerBuckets) {
  // Parameters for encoding bucket counts
  int64_t max_count = 10;
  uint32_t partitions = 5;
  auto integer_encoder = MakeDeterministicIntegerEncoder(max_count, partitions);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 15;
  auto histogram_encoder = HistogramEncoder(num_buckets, integer_encoder.get());

  std::vector<int64_t> histogram = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1};
  std::vector<uint32_t> encoded = histogram_encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 1, 1));
}

TEST_F(HistogramEncoderTest, EstimateHistogramSumDiscard) {
  // Parameters for estimating integer sums
  int64_t max_count = 10;
  uint32_t partitions = 5;
  auto integer_sum_estimator = MakeIntegerSumEstimatorDiscard(max_count, partitions, /*p=*/0.0);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_sum_estimator = HistogramSumEstimator(num_buckets, integer_sum_estimator.get());

  // Since |partitions| is 5, the elements of the encoded histograms have values between 0 and 6
  // inclusive.
  std::vector<uint32_t> encoded_histogram_1 = {0, 1, 2, 3, 4, 5, 4, 3, 2, 1};
  std::vector<uint32_t> encoded_histogram_2 = {1, 2, 3, 4, 5, 6, 5, 4, 3, 2};
  std::vector<uint32_t> encoded_histogram_3 = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4};

  std::vector<std::vector<uint32_t>> encoded_histograms = {encoded_histogram_1, encoded_histogram_2,
                                                           encoded_histogram_3};

  auto estimate = histogram_sum_estimator.ComputeSum(encoded_histograms);
  std::vector<double> expected_sums = {6.0, 8.0, 12.0, 16.0, 20.0, 14.0, 20.0, 16.0, 12.0, 8.0};
  std::vector<uint64_t> expected_underflow_counts = {1ul, 0ul, 0ul, 0ul, 0ul,
                                                     0ul, 0ul, 0ul, 0ul, 0ul};
  std::vector<uint64_t> expected_overflow_counts = {0ul, 0ul, 0ul, 0ul, 0ul,
                                                    1ul, 0ul, 0ul, 0ul, 0ul};
  for (size_t i = 0; i < num_buckets; i++) {
    const auto& [sum, underflow_count, overflow_count] = estimate[i];
    EXPECT_EQ(sum, expected_sums[i]) << "Bucket index " << i;
    EXPECT_EQ(underflow_count, expected_underflow_counts[i]) << "Bucket index " << i;
    EXPECT_EQ(overflow_count, expected_overflow_counts[i]) << "Bucket index " << i;
  }
}

TEST_F(HistogramEncoderTest, EstimateHistogramSumClamp) {
  // Parameters for estimating integer sums
  int64_t max_count = 10;
  uint32_t partitions = 5;
  auto integer_sum_estimator = MakeIntegerSumEstimatorClamp(max_count, partitions, /*p=*/0.0);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_sum_estimator = HistogramSumEstimator(num_buckets, integer_sum_estimator.get());

  // Since |partitions| is 5, the elements of the encoded histograms have values between 0 and 6
  // inclusive.
  std::vector<uint32_t> encoded_histogram_1 = {0, 1, 2, 3, 4, 5, 4, 3, 2, 1};
  std::vector<uint32_t> encoded_histogram_2 = {1, 2, 3, 4, 5, 6, 5, 4, 3, 2};
  std::vector<uint32_t> encoded_histogram_3 = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4};

  std::vector<std::vector<uint32_t>> encoded_histograms = {encoded_histogram_1, encoded_histogram_2,
                                                           encoded_histogram_3};

  auto estimate = histogram_sum_estimator.ComputeSum(encoded_histograms);
  std::vector<double> expected_sums = {6.0, 8.0, 12.0, 16.0, 20.0, 24.0, 20.0, 16.0, 12.0, 8.0};
  std::vector<uint64_t> expected_underflow_counts = {1ul, 0ul, 0ul, 0ul, 0ul,
                                                     0ul, 0ul, 0ul, 0ul, 0ul};
  std::vector<uint64_t> expected_overflow_counts = {0ul, 0ul, 0ul, 0ul, 0ul,
                                                    1ul, 0ul, 0ul, 0ul, 0ul};
  for (size_t i = 0; i < num_buckets; i++) {
    const auto& [sum, underflow_count, overflow_count] = estimate[i];
    EXPECT_EQ(sum, expected_sums[i]) << "Bucket index " << i;
    EXPECT_EQ(underflow_count, expected_underflow_counts[i]) << "Bucket index " << i;
    EXPECT_EQ(overflow_count, expected_overflow_counts[i]) << "Bucket index " << i;
  }
}

}  // namespace cobalt

#include "src/algorithms/experimental/histogram_encoder.h"

#include <tuple>

#include "src/algorithms/experimental/integer_encoder.h"
#include "src/algorithms/experimental/random.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

using testing::ElementsAre;
using testing::FloatNear;
using testing::Pointwise;

namespace cobalt {

class HistogramEncoderTest : public ::testing::Test {
 protected:
  void SetUp() override { gen_ = std::make_unique<RandomNumberGenerator>(); }

  RandomNumberGenerator* GetGenerator() { return gen_.get(); }

  // Create an IntegerEncoder for the range [0, |max_count|] where the number of partitions is equal
  // to |max_count|, and where the probability of a data-independent response is 0. Both the initial
  // fixed-point encoding and the final output of the encoder are deterministic in this case.
  std::unique_ptr<IntegerEncoder> MakeDeterministicIntegerEncoder(int64_t max_count) {
    return std::make_unique<IntegerEncoder>(GetGenerator(), /*min_int=*/0, /*max_int=*/max_count,
                                            /*partitions=*/max_count,
                                            /*p=*/0.0);
  }

  // Create an IntegerSumEstimator for the output of the IntegerEncoder returned by
  // MakeDeterministicIntegerEncoder.
  std::unique_ptr<IntegerSumEstimator> MakeIntegerSumEstimator(int64_t max_count) {
    return std::make_unique<IntegerSumEstimator>(/*min_int=*/0, /*max_int=*/max_count,
                                                 /*partitions=*/max_count, /*p=*/0.0);
  }

 private:
  std::unique_ptr<RandomNumberGenerator> gen_;
};

TEST_F(HistogramEncoderTest, Encode) {
  // Parameters for encoding bucket counts
  int64_t max_count = 5;
  auto integer_encoder = MakeDeterministicIntegerEncoder(max_count);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_encoder = HistogramEncoder(num_buckets, integer_encoder.get());

  std::vector<int64_t> histogram = {-1, 0, 1, 2, 3, 4, 5, 6, 0, 1};
  std::vector<uint32_t> encoded = histogram_encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(0, 0, 1, 2, 3, 4, 5, 5, 0, 1));
}

TEST_F(HistogramEncoderTest, EstimateSum) {
  // Parameters for encoding bucket counts
  int64_t max_count = 5;
  auto integer_sum_estimator = MakeIntegerSumEstimator(max_count);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_sum_estimator = HistogramSumEstimator(num_buckets, integer_sum_estimator.get());

  // Since |partitions| is 5, the elements of the encoded histograms have values between 0 and 5
  // inclusive.
  std::vector<uint32_t> encoded_histogram_1 = {0, 1, 2, 3, 4, 5, 4, 3, 2, 1};
  std::vector<uint32_t> encoded_histogram_2 = {1, 2, 3, 4, 5, 5, 4, 3, 2, 1};
  std::vector<uint32_t> encoded_histogram_3 = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4};

  std::vector<std::vector<uint32_t>> encoded_histograms = {encoded_histogram_1, encoded_histogram_2,
                                                           encoded_histogram_3};

  auto estimate = histogram_sum_estimator.ComputeSum(encoded_histograms);
  std::vector<double> expected_sums = {5.0, 7.0, 9.0, 11.0, 13.0, 14.0, 12.0, 10.0, 8.0, 6.0};
  EXPECT_THAT(estimate, Pointwise(FloatNear(0.001), expected_sums));
}

}  // namespace cobalt

#include "src/algorithms/experimental/histogram_encoder.h"

#include "src/algorithms/experimental/integer_encoder.h"
#include "src/algorithms/experimental/random.h"
#include "src/algorithms/experimental/randomized_response.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

using testing::ElementsAre;
using testing::FloatNear;
using testing::Pair;
using testing::Pointwise;

namespace cobalt {

class HistogramEncoderTest : public ::testing::Test {
 protected:
  void SetUp() override { gen_ = std::make_unique<RandomNumberGenerator>(0); }

  RandomNumberGenerator* GetGenerator() { return gen_.get(); }

  // Create an IntegerEncoder for the range [0, |max_count|] where the number of partitions is equal
  // to |max_count|, and where the probability of a data-independent response is 0. Both the initial
  // fixed-point encoding and the final output of the encoder are deterministic in this case.
  std::unique_ptr<IntegerEncoder> MakeDeterministicIntegerEncoder(int64_t max_count) {
    return std::make_unique<IntegerEncoder>(GetGenerator(), /*min_int=*/0, /*max_int=*/max_count,
                                            /*partitions=*/max_count,
                                            /*p=*/0.0);
  }

  // Create an IntegerSumEstimator for an IntegerEncoder with range [0, |max_count|] and where the
  // number of partitions is equal to |max_count|.
  std::unique_ptr<IntegerSumEstimator> MakeIntegerSumEstimator(int64_t max_count, double p) {
    return std::make_unique<IntegerSumEstimator>(/*min_int=*/0, /*max_int=*/max_count,
                                                 /*partitions=*/max_count, p);
  }

 private:
  std::unique_ptr<RandomNumberGenerator> gen_;
};

// Bucket-wise encoding with p = 0. The encoder should snap negative counts to 0 and overflow
// counts to |max_count|, but otherwise leave counts unchanged.
TEST_F(HistogramEncoderTest, BucketWiseEncode) {
  // Parameters for encoding bucket counts
  int64_t max_count = 5;
  auto integer_encoder = MakeDeterministicIntegerEncoder(max_count);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_encoder = BucketWiseHistogramEncoder(num_buckets, integer_encoder.get());

  std::vector<int64_t> histogram = {-1, 0, 1, 2, 3, 4, 5, 6, 0, 1};
  std::vector<uint32_t> encoded = histogram_encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(0, 0, 1, 2, 3, 4, 5, 5, 0, 1));
}

// Bucket-wise encoding with p > 0. The expected result depends on the seed passed to |gen_|.
TEST_F(HistogramEncoderTest, BucketWiseEncodeNonzeroP) {
  // Parameters for encoding occurrences
  int64_t max_count = 5;
  double p = 0.1;
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_encoder =
      OccurrenceWiseHistogramEncoder(GetGenerator(), num_buckets, max_count, p);

  std::vector<uint64_t> histogram = {0, 0, 1, 2, 3, 4, 5, 6, 0, 1};
  std::vector<uint64_t> encoded = histogram_encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(0, 0, 2, 2, 3, 4, 5, 4, 0, 1));
}

TEST_F(HistogramEncoderTest, BucketWiseEstimateSum) {
  // Parameters for encoding bucket counts
  int64_t max_count = 5;
  double p = 0.0;
  auto integer_sum_estimator = MakeIntegerSumEstimator(max_count, p);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_sum_estimator =
      BucketWiseHistogramSumEstimator(num_buckets, integer_sum_estimator.get());

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

TEST_F(HistogramEncoderTest, BucketWiseEstimateSumNonzeroP) {
  // Parameters for encoding bucket counts
  int64_t max_count = 5;
  double p = 0.1;
  auto integer_sum_estimator = MakeIntegerSumEstimator(max_count, p);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_sum_estimator =
      BucketWiseHistogramSumEstimator(num_buckets, integer_sum_estimator.get());

  // Since |max_count| is 5 and this is equal to the number of partitions, the elements of the
  // encoded histograms have values between 0 and 5 inclusive.
  std::vector<uint32_t> encoded_histogram_1 = {0, 1, 2, 3, 4, 5, 4, 3, 2, 1};
  std::vector<uint32_t> encoded_histogram_2 = {1, 2, 3, 4, 5, 5, 4, 3, 2, 1};
  std::vector<uint32_t> encoded_histogram_3 = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4};

  std::vector<std::vector<uint32_t>> encoded_histograms = {encoded_histogram_1, encoded_histogram_2,
                                                           encoded_histogram_3};

  auto estimate = histogram_sum_estimator.ComputeSum(encoded_histograms);
  std::vector<double> expected_sums = {4.722,  6.944,  9.166,  11.388, 13.611,
                                       14.722, 12.500, 10.277, 8.0555, 5.833};
  EXPECT_THAT(estimate, Pointwise(FloatNear(0.001), expected_sums));
}

// Occurrence-wise encoding with p = 0. The encoder should snap overflow counts to the max count,
// but otherwise leave counts unchanged.
TEST_F(HistogramEncoderTest, OccurrenceWiseEncode) {
  // Parameters for encoding occurrences
  int64_t max_count = 5;
  double p = 0.0;
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_encoder =
      OccurrenceWiseHistogramEncoder(GetGenerator(), num_buckets, max_count, p);

  std::vector<uint64_t> histogram = {0, 0, 1, 2, 3, 4, 5, 6, 0, 1};
  std::vector<uint64_t> encoded = histogram_encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(0, 0, 1, 2, 3, 4, 5, 5, 0, 1));
}

// Occurrence-wise encoding with p > 0. The expected result depends on the seed passed to |gen_|.
TEST_F(HistogramEncoderTest, OccurrenceWiseEncodeNonzeroP) {
  // Parameters for encoding occurrences
  int64_t max_count = 5;
  double p = 0.1;
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_encoder =
      OccurrenceWiseHistogramEncoder(GetGenerator(), num_buckets, max_count, p);

  std::vector<uint64_t> histogram = {0, 0, 1, 2, 3, 4, 5, 6, 0, 1};
  std::vector<uint64_t> encoded = histogram_encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(0, 0, 2, 2, 3, 4, 5, 4, 0, 1));
}

TEST_F(HistogramEncoderTest, OccurrenceWiseEstimateSum) {
  // Noise parameter for encoded occurrences
  double p = 0.0;
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_sum_estimator = OccurrenceWiseHistogramSumEstimator(num_buckets, p);

  std::vector<uint64_t> encoded_histogram_1 = {0, 1, 2, 3, 4, 5, 4, 3, 2, 1};
  std::vector<uint64_t> encoded_histogram_2 = {1, 2, 3, 4, 5, 5, 4, 3, 2, 1};
  std::vector<uint64_t> encoded_histogram_3 = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
  std::vector<std::vector<uint64_t>> encoded_histograms = {encoded_histogram_1, encoded_histogram_2,
                                                           encoded_histogram_3};
  auto estimate = histogram_sum_estimator.ComputeSum(encoded_histograms);
  std::vector<double> expected_sums = {5.0, 7.0, 9.0, 11.0, 13.0, 14.0, 12.0, 10.0, 8.0, 6.0};
  EXPECT_THAT(estimate, Pointwise(FloatNear(0.001), expected_sums));
}

// Occurrence-wise estimation with p > 0.
TEST_F(HistogramEncoderTest, OccurrenceWiseEstimateSumNonzeroP) {
  // Noise parameter for encoded occurrences
  double p = 0.1;
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_sum_estimator = OccurrenceWiseHistogramSumEstimator(num_buckets, p);

  std::vector<uint64_t> encoded_histogram_1 = {0, 1, 2, 3, 4, 5, 4, 3, 2, 1};
  std::vector<uint64_t> encoded_histogram_2 = {1, 2, 3, 4, 5, 5, 4, 3, 2, 1};
  std::vector<uint64_t> encoded_histogram_3 = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
  std::vector<std::vector<uint64_t>> encoded_histograms = {encoded_histogram_1, encoded_histogram_2,
                                                           encoded_histogram_3};
  auto estimate = histogram_sum_estimator.ComputeSum(encoded_histograms);
  std::vector<double> expected_sums = {4.500,  6.722,  8.944,  11.166, 13.388,
                                       14.500, 12.277, 10.055, 7.833,  5.611};
  EXPECT_THAT(estimate, Pointwise(FloatNear(0.001), expected_sums));
}

TEST_F(HistogramEncoderTest, TwoDimRapporHistogramEncoderAllZero) {
  auto encoder = TwoDimRapporHistogramEncoder(GetGenerator(), 20, 50, 0.0002);
  std::vector<uint64_t> histogram(20, 0);
  auto encoded = encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(Pair(17, 39)));
}

TEST_F(HistogramEncoderTest, TwoDimRapporHistogramEncoder) {
  auto encoder = TwoDimRapporHistogramEncoder(GetGenerator(), 20, 50, 0.0002);
  std::vector<uint64_t> histogram = {0, 0, 1, 0, 0, 10, 60, 1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0};
  auto encoded = encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(Pair(2, 1), Pair(5, 10), Pair(6, 50), Pair(7, 1), Pair(15, 3),
                                   Pair(17, 34)));
}

TEST_F(HistogramEncoderTest, TwoDimRapporHistogramSumEstimator) {
  auto estimator = TwoDimRapporHistogramSumEstimator(20, 50, 0.0002);
  auto estimate = estimator.ComputeSum({{{2, 1}, {5, 10}, {6, 50}},
                                        {{2, 1}, {7, 1}, {15, 3}, {17, 34}},
                                        {{0, 1}, {2, 3}, {15, 6}, {19, 25}}},
                                       3);
  std::vector<double> expected_sums = {0.988, 0.000, 4.953, 0.000,  0.000, 9.883, 49.419,
                                       0.988, 0.000, 0.000, 0.000,  0.000, 0.000, 0.000,
                                       0.000, 8.895, 0.000, 33.605, 0.000, 24.709};
  EXPECT_THAT(estimate, Pointwise(FloatNear(0.001), expected_sums));
}

}  // namespace cobalt

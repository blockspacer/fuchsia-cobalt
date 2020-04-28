#include "src/algorithms/experimental/archived/bucket_wise_histogram_encoder.h"

#include "src/algorithms/experimental/archived/krr_integer_encoder.h"
#include "src/algorithms/experimental/random.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

using testing::ElementsAre;
using testing::FloatNear;
using testing::Pointwise;

namespace cobalt {

class ArchivedBucketWiseHistogramEncoderTest : public ::testing::Test {
 protected:
  void SetUp() override { gen_ = std::make_unique<RandomNumberGenerator>(0); }

  RandomNumberGenerator* GetGenerator() { return gen_.get(); }

  // Create an ArchivedKrrIntegerEncoder for the range [0, |max_count|] where the number of
  // partitions is equal to |max_count|, and where the probability of a data-independent response is
  // 0. Both the initial fixed-point encoding and the final output of the encoder are deterministic
  // in this case.
  std::unique_ptr<ArchivedKrrIntegerEncoder> MakeDeterministicIntegerEncoder(int64_t max_count) {
    return std::make_unique<ArchivedKrrIntegerEncoder>(GetGenerator(), /*min_int=*/0,
                                                       /*max_int=*/max_count,
                                                       /*partitions=*/max_count,
                                                       /*p=*/0.0);
  }

  // Create an ArchivedKrrIntegerSumEstimator for an ArchivedKrrIntegerEncoder with range [0,
  // |max_count|] and where the number of partitions is equal to |max_count|.
  std::unique_ptr<ArchivedKrrIntegerSumEstimator> MakeIntegerSumEstimator(int64_t max_count,
                                                                          double p) {
    return std::make_unique<ArchivedKrrIntegerSumEstimator>(/*min_int=*/0, /*max_int=*/max_count,
                                                            /*partitions=*/max_count, p);
  }

 private:
  std::unique_ptr<RandomNumberGenerator> gen_;
};

// Bucket-wise encoding with p = 0. The encoder should snap negative counts to 0 and overflow
// counts to |max_count|, but otherwise leave counts unchanged.
TEST_F(ArchivedBucketWiseHistogramEncoderTest, Encode) {
  // Parameters for encoding bucket counts
  int64_t max_count = 5;
  auto integer_encoder = MakeDeterministicIntegerEncoder(max_count);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_encoder = ArchivedBucketWiseHistogramEncoder(num_buckets, integer_encoder.get());

  std::vector<int64_t> histogram = {-1, 0, 1, 2, 3, 4, 5, 6, 0, 1};
  std::vector<uint32_t> encoded = histogram_encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(0, 0, 1, 2, 3, 4, 5, 5, 0, 1));
}

// Bucket-wise encoding with p > 0. The expected result depends on the seed passed to |gen_|.
TEST_F(ArchivedBucketWiseHistogramEncoderTest, EncodeNonzeroP) {
  // Parameters for encoding occurrences
  int64_t max_count = 5;
  double p = 0.1;
  auto integer_encoder =
      std::make_unique<ArchivedKrrIntegerEncoder>(GetGenerator(), 0, max_count, max_count, p);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_encoder = ArchivedBucketWiseHistogramEncoder(num_buckets, integer_encoder.get());

  std::vector<int64_t> histogram = {0, 0, 1, 2, 3, 4, 5, 6, 0, 1};
  std::vector<uint32_t> encoded = histogram_encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(0, 0, 1, 2, 3, 4, 5, 5, 0, 1));
}

TEST_F(ArchivedBucketWiseHistogramEncoderTest, EstimateSum) {
  // Parameters for encoding bucket counts
  int64_t max_count = 5;
  double p = 0.0;
  auto integer_sum_estimator = MakeIntegerSumEstimator(max_count, p);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_sum_estimator =
      ArchivedBucketWiseHistogramSumEstimator(num_buckets, integer_sum_estimator.get());

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

TEST_F(ArchivedBucketWiseHistogramEncoderTest, EstimateSumNonzeroP) {
  // Parameters for encoding bucket counts
  int64_t max_count = 5;
  double p = 0.1;
  auto integer_sum_estimator = MakeIntegerSumEstimator(max_count, p);
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_sum_estimator =
      ArchivedBucketWiseHistogramSumEstimator(num_buckets, integer_sum_estimator.get());

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

}  // namespace cobalt

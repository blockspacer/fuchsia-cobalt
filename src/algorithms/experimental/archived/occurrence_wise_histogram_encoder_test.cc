#include "src/algorithms/experimental/archived/occurrence_wise_histogram_encoder.h"

#include "src/algorithms/experimental/random.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

using testing::ElementsAre;
using testing::FloatNear;
using testing::Pointwise;

namespace cobalt {

class ArchivedOccurrenceWiseHistogramEncoderTest : public ::testing::Test {
 protected:
  void SetUp() override { gen_ = std::make_unique<RandomNumberGenerator>(0); }

  RandomNumberGenerator* GetGenerator() { return gen_.get(); }

 private:
  std::unique_ptr<RandomNumberGenerator> gen_;
};

// Occurrence-wise encoding with p = 0. The encoder should snap overflow counts to the max count,
// but otherwise leave counts unchanged.
TEST_F(ArchivedOccurrenceWiseHistogramEncoderTest, Encode) {
  // Parameters for encoding occurrences
  int64_t max_count = 5;
  double p = 0.0;
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_encoder =
      ArchivedOccurrenceWiseHistogramEncoder(GetGenerator(), num_buckets, max_count, p);

  std::vector<uint64_t> histogram = {0, 0, 1, 2, 3, 4, 5, 6, 0, 1};
  std::vector<uint64_t> encoded = histogram_encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(0, 0, 1, 2, 3, 4, 5, 5, 0, 1));
}

// Occurrence-wise encoding with p > 0. The expected result depends on the seed passed to |gen_|.
TEST_F(ArchivedOccurrenceWiseHistogramEncoderTest, EncodeNonzeroP) {
  // Parameters for encoding occurrences
  int64_t max_count = 5;
  double p = 0.1;
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_encoder =
      ArchivedOccurrenceWiseHistogramEncoder(GetGenerator(), num_buckets, max_count, p);

  std::vector<uint64_t> histogram = {0, 0, 1, 2, 3, 4, 5, 6, 0, 1};
  std::vector<uint64_t> encoded = histogram_encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(0, 0, 2, 2, 3, 4, 5, 4, 0, 1));
}

TEST_F(ArchivedOccurrenceWiseHistogramEncoderTest, EstimateSum) {
  // Noise parameter for encoded occurrences
  double p = 0.0;
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_sum_estimator = ArchivedOccurrenceWiseHistogramSumEstimator(num_buckets, p);

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
TEST_F(ArchivedOccurrenceWiseHistogramEncoderTest, EstimateSumNonzeroP) {
  // Noise parameter for encoded occurrences
  double p = 0.1;
  // Number of buckets in the top-level histogram
  uint32_t num_buckets = 10;
  auto histogram_sum_estimator = ArchivedOccurrenceWiseHistogramSumEstimator(num_buckets, p);

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

}  // namespace cobalt

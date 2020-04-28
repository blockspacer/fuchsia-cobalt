#include "src/algorithms/experimental/archived/krr_integer_encoder.h"

#include "src/algorithms/experimental/archived/response_randomizer.h"
#include "src/algorithms/experimental/random.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

using ::testing::AnyOf;
using ::testing::Eq;

namespace cobalt {

class ArchivedKrrIntegerEncoderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    gen_ = std::make_unique<RandomNumberGenerator>();
    seeded_gen_ = std::make_unique<RandomNumberGenerator>(1);
  }

  RandomNumberGenerator* GetGenerator() { return gen_.get(); }
  RandomNumberGenerator* GetSeededGenerator() { return seeded_gen_.get(); }

 private:
  std::unique_ptr<RandomNumberGenerator> gen_;
  std::unique_ptr<RandomNumberGenerator> seeded_gen_;
};

// Encode when the number of partitions is equal to the width of the range, and when the probability
// of a data-independent response is 0. The encoding is deterministic in this case.
TEST_F(ArchivedKrrIntegerEncoderTest, EncodeWithIntegerBuckets) {
  int64_t min_int = 0;
  int64_t max_int = 10;
  uint32_t partitions = 10;
  double p = 0.0;
  auto encoder = ArchivedKrrIntegerEncoder(GetGenerator(), min_int, max_int, partitions, p);

  EXPECT_EQ(encoder.Encode(-1), 0u);
  EXPECT_EQ(encoder.Encode(0), 0u);
  EXPECT_EQ(encoder.Encode(1), 1u);
  EXPECT_EQ(encoder.Encode(2), 2u);
  EXPECT_EQ(encoder.Encode(3), 3u);
  EXPECT_EQ(encoder.Encode(4), 4u);
  EXPECT_EQ(encoder.Encode(5), 5u);
  EXPECT_EQ(encoder.Encode(6), 6u);
  EXPECT_EQ(encoder.Encode(7), 7u);
  EXPECT_EQ(encoder.Encode(8), 8u);
  EXPECT_EQ(encoder.Encode(9), 9u);
  EXPECT_EQ(encoder.Encode(10), 10u);
  EXPECT_EQ(encoder.Encode(20), 10u);
}

// Encode when the probability of a data-independent response is 0, and each partition has width 2.
// The encoding is equivalent to randomized rounding in this case.
TEST_F(ArchivedKrrIntegerEncoderTest, EncodeWithCoarseBuckets) {
  int64_t min_int = 0;
  int64_t max_int = 10;
  uint32_t partitions = 5;
  double p = 0.0;
  auto encoder = ArchivedKrrIntegerEncoder(GetGenerator(), min_int, max_int, partitions, p);

  EXPECT_EQ(encoder.Encode(-1), 0u);
  EXPECT_EQ(encoder.Encode(0), 0u);
  EXPECT_THAT(encoder.Encode(1), AnyOf(Eq(0u), Eq(1u)));
  EXPECT_EQ(encoder.Encode(2), 1u);
  EXPECT_THAT(encoder.Encode(3), AnyOf(Eq(1u), Eq(2u)));
  EXPECT_EQ(encoder.Encode(4), 2u);
  EXPECT_THAT(encoder.Encode(5), AnyOf(Eq(2u), Eq(3u)));
  EXPECT_EQ(encoder.Encode(6), 3u);
  EXPECT_THAT(encoder.Encode(7), AnyOf(Eq(3u), Eq(4u)));
  EXPECT_EQ(encoder.Encode(8), 4u);
  EXPECT_THAT(encoder.Encode(9), AnyOf(Eq(4u), Eq(5u)));
  EXPECT_EQ(encoder.Encode(10), 5u);
  EXPECT_EQ(encoder.Encode(20), 5u);
}

// Encode when the number of partitions is equal to the width of the range, and when the probability
// of a data-independent response is not 0.
TEST_F(ArchivedKrrIntegerEncoderTest, EncodeWithIntegerBucketsNonzeroP) {
  int64_t min_int = 0;
  int64_t max_int = 10;
  uint32_t partitions = 10;
  double p = 0.1;
  auto encoder = ArchivedKrrIntegerEncoder(GetSeededGenerator(), min_int, max_int, partitions, p);

  EXPECT_EQ(encoder.Encode(-1), 0u);
  EXPECT_EQ(encoder.Encode(0), 0u);
  EXPECT_EQ(encoder.Encode(1), 1u);
  EXPECT_EQ(encoder.Encode(2), 2u);
  EXPECT_EQ(encoder.Encode(3), 10u);
  EXPECT_EQ(encoder.Encode(4), 4u);
  EXPECT_EQ(encoder.Encode(5), 5u);
  EXPECT_EQ(encoder.Encode(6), 6u);
  EXPECT_EQ(encoder.Encode(7), 7u);
  EXPECT_EQ(encoder.Encode(8), 8u);
  EXPECT_EQ(encoder.Encode(9), 9u);
  EXPECT_EQ(encoder.Encode(10), 10u);
  EXPECT_EQ(encoder.Encode(20), 10u);
}

// Encode when the probability of a data-independent response is not 0, and each partition has
// width 2.
TEST_F(ArchivedKrrIntegerEncoderTest, EncodeWithCoarseBucketsNonzeroP) {
  int64_t min_int = 0;
  int64_t max_int = 10;
  uint32_t partitions = 5;
  double p = 0.1;
  auto encoder = ArchivedKrrIntegerEncoder(GetSeededGenerator(), min_int, max_int, partitions, p);

  EXPECT_EQ(encoder.Encode(-1), 4u);
  EXPECT_EQ(encoder.Encode(0), 0u);
  EXPECT_EQ(encoder.Encode(1), 1u);
  EXPECT_EQ(encoder.Encode(2), 1u);
  EXPECT_EQ(encoder.Encode(3), 2u);
  EXPECT_EQ(encoder.Encode(4), 2u);
  EXPECT_EQ(encoder.Encode(5), 2u);
  EXPECT_EQ(encoder.Encode(6), 3u);
  EXPECT_EQ(encoder.Encode(7), 4u);
  EXPECT_EQ(encoder.Encode(8), 4u);
  EXPECT_EQ(encoder.Encode(9), 3u);
  EXPECT_EQ(encoder.Encode(10), 1u);
  EXPECT_EQ(encoder.Encode(20), 5u);
}

TEST(ArchivedKrrIntegerSumEstimatorTest, EstimateWithIntegerBucketsZeroP) {
  int64_t min_int = 0;
  int64_t max_int = 10;
  uint32_t partitions = 10;
  double p = 0.0;
  auto estimator = ArchivedKrrIntegerSumEstimator(min_int, max_int, partitions, p);
  // Encoded values should be in the range [0, |partitions|] inclusive.
  std::vector<uint32_t> encoded_vals = {0,  1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                        10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
  // The estimated sum should be equal to the true sum of the values corresponding to the indices in
  // |encoded_vals|.
  double estimated_sum = estimator.ComputeSum(encoded_vals);
  EXPECT_DOUBLE_EQ(estimated_sum, 110.0);
}

TEST(ArchivedKrrIntegerSumEstimatorTest, EstimateWithCoarseBucketsZeroP) {
  int64_t min_int = 0;
  int64_t max_int = 10;
  uint32_t partitions = 5;
  double p = 0.0;
  auto estimator = ArchivedKrrIntegerSumEstimator(min_int, max_int, partitions, p);
  // Encoded values should be in the range [0, |partitions|] inclusive.
  std::vector<uint32_t> encoded_vals = {0, 1, 2, 3, 4, 5, 5, 4, 3, 2, 1, 0};
  // The estimated sum should be equal to the true sum of the values corresponding to the indices in
  // |encoded_vals|.
  double estimated_sum = estimator.ComputeSum(encoded_vals);
  EXPECT_DOUBLE_EQ(estimated_sum, 60.0);
}

TEST(ArchivedKrrIntegerSumEstimatorTest, EstimateWithIntegerBucketsNonzeroP) {
  int64_t min_int = 0;
  int64_t max_int = 10;
  uint32_t partitions = 10;
  double p = 0.10;
  auto estimator = ArchivedKrrIntegerSumEstimator(min_int, max_int, partitions, p);
  // Encoded values should be in the range [0, |partitions|] inclusive.
  std::vector<uint32_t> encoded_vals = {1, 2, 2, 3};
  // The raw sum is 8, and the debiased sum (based on 4 input values) is 6.666.
  double estimated_sum = estimator.ComputeSum(encoded_vals);
  EXPECT_NEAR(estimated_sum, 6.666, .001);
}

TEST(ArchivedKrrIntegerSumEstimatorTest, EstimateWithCoarseBucketsNonzeroP) {
  int64_t min_int = 0;
  int64_t max_int = 10;
  uint32_t partitions = 5;
  double p = 0.10;
  auto estimator = ArchivedKrrIntegerSumEstimator(min_int, max_int, partitions, p);
  // Encoded values should be in the range [0, |partitions|] inclusive.
  std::vector<uint32_t> encoded_vals = {1, 2, 2, 3};
  // The raw sum of the numeric values corresponding to |encoded_vals| is 16, and
  // the debiased sum (based on 4 input values) is 15.555.
  double estimated_sum = estimator.ComputeSum(encoded_vals);
  EXPECT_NEAR(estimated_sum, 15.555, .001);
}

TEST(ArchivedKrrIntegerSumEstimatorTest, EstimateWithShiftedRange) {
  int64_t min_int = 10;
  int64_t max_int = 20;
  uint32_t partitions = 5;
  double p = 0.10;
  auto estimator = ArchivedKrrIntegerSumEstimator(min_int, max_int, partitions, p);
  // Encoded values should be in the range [0, |partitions|] inclusive.
  std::vector<uint32_t> encoded_vals = {1, 2, 2, 3};
  // The raw sum of the numeric values corresponding to |encoded_vals| is 56,
  // and the debiased sum (based on 4 input values) is 55.555.
  double estimated_sum = estimator.ComputeSum(encoded_vals);
  EXPECT_NEAR(estimated_sum, 55.555, .001);
}

}  // namespace cobalt

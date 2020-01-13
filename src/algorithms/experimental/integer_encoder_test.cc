#include "src/algorithms/experimental/integer_encoder.h"

#include "src/algorithms/experimental/random.h"
#include "src/algorithms/experimental/randomized_response.h"
#include "src/registry/buckets_config.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

using ::testing::AnyOf;
using ::testing::Eq;

namespace cobalt {
namespace {

IntegerBuckets MakeIntegerBuckets(int64_t min_int, uint32_t num_buckets, uint32_t step_size) {
  IntegerBuckets buckets;
  auto linear_buckets = buckets.mutable_linear();
  linear_buckets->set_floor(min_int);
  linear_buckets->set_num_buckets(num_buckets);
  linear_buckets->set_step_size(step_size);
  return buckets;
}

}  // namespace

class IntegerEncoderTest : public ::testing::Test {
 protected:
  void SetUp() override { gen_ = std::make_unique<RandomNumberGenerator>(); }

  RandomNumberGenerator* GetGenerator() { return gen_.get(); }

 private:
  std::unique_ptr<RandomNumberGenerator> gen_;
};

// Construct an IntegerEncoder using an IntegerBuckets proto and encode some values. Set the
// probability of a data-independent response to zero and use deterministic rounding.
TEST_F(IntegerEncoderTest, EncodeFromBuckets) {
  int64_t min_int = 0;
  uint32_t num_buckets = 5;
  uint32_t step_size = 2;
  double p = 0.0;
  auto buckets = MakeIntegerBuckets(min_int, num_buckets, step_size);
  auto encoder =
      IntegerEncoder(GetGenerator(), buckets, p, /*rounding_strategy=*/IntegerEncoder::kFloor);
  EXPECT_EQ(encoder.Encode(-1), 0u);
  EXPECT_EQ(encoder.Encode(0), 1u);
  EXPECT_EQ(encoder.Encode(1), 1u);
  EXPECT_EQ(encoder.Encode(4), 3u);
  EXPECT_EQ(encoder.Encode(5), 3u);
  EXPECT_EQ(encoder.Encode(8), 5u);
  EXPECT_EQ(encoder.Encode(9), 5u);
  EXPECT_EQ(encoder.Encode(10), 6u);
  EXPECT_EQ(encoder.Encode(20), 6u);
}

// Construct an IntegerEncoder using parameters to specify the buckets, and then encode some values.
// Set the probability of a data-independent response to zero and use deterministic rounding.
TEST_F(IntegerEncoderTest, EncodeFromParams) {
  int64_t min_int = 0;
  int64_t max_int = 10;
  uint32_t partitions = 5;
  double p = 0.0;
  auto encoder = IntegerEncoder(GetGenerator(), min_int, max_int, partitions, p,
                                /*rounding_strategy=*/IntegerEncoder::kFloor);
  EXPECT_EQ(encoder.Encode(-1), 0u);
  EXPECT_EQ(encoder.Encode(0), 1u);
  EXPECT_EQ(encoder.Encode(1), 1u);
  EXPECT_EQ(encoder.Encode(4), 3u);
  EXPECT_EQ(encoder.Encode(5), 3u);
  EXPECT_EQ(encoder.Encode(8), 5u);
  EXPECT_EQ(encoder.Encode(9), 5u);
  EXPECT_EQ(encoder.Encode(10), 6u);
  EXPECT_EQ(encoder.Encode(20), 6u);
}

// Use random rounding to compute the true bucket index, then encode deterministically.
//
// Any value which is equal to a bucket floor(i.e., any multiple of 2 between 0 and 10), or which
// falls in an under/overflow bucket, should be rounded to the index of that bucket. Values
// which fall between any other two bucket floors may be rounded to the index of either of those
// buckets.
TEST_F(IntegerEncoderTest, RandomRounding) {
  int64_t min_int = 0;
  uint32_t num_buckets = 5;
  uint32_t step_size = 2;
  double p = 0.0;
  auto buckets = MakeIntegerBuckets(min_int, num_buckets, step_size);
  auto encoder =
      IntegerEncoder(GetGenerator(), buckets, p, /*rounding_strategy=*/IntegerEncoder::kRandom);

  EXPECT_EQ(encoder.Encode(-1), 0u);
  EXPECT_EQ(encoder.Encode(0), 1u);
  EXPECT_THAT(encoder.Encode(1), AnyOf(Eq(1u), Eq(2u)));
  EXPECT_EQ(encoder.Encode(4), 3u);
  EXPECT_THAT(encoder.Encode(5), AnyOf(Eq(3u), Eq(4u)));
  EXPECT_EQ(encoder.Encode(8), 5u);
  EXPECT_THAT(encoder.Encode(9), AnyOf(Eq(5u), Eq(6u)));
  EXPECT_EQ(encoder.Encode(10), 6u);
  EXPECT_EQ(encoder.Encode(20), 6u);
}

// Construct an IntegerSumEstimator using an IntegerBuckets proto, and then estimate the sum of some
// encoded values while discarding under/overflow indices. Set |p| to zero, so that the estimated
// sum is the exact sum of the integers corresponding to the indices in |encoded_values| after
// dropping all under/overflow indices.
TEST(IntegerSumEstimatorTest, FromBucketsDiscard) {
  int64_t min_int = 0;
  uint32_t partitions = 10;
  uint32_t step_size = 1;
  double p = 0.0;
  auto buckets = MakeIntegerBuckets(min_int, partitions, step_size);
  auto estimator =
      IntegerSumEstimator(buckets, p, /*underflow_strategy=*/IntegerSumEstimator::kDiscard,
                          /*overflow_strategy=*/IntegerSumEstimator::kDiscard);
  std::vector<uint32_t> encoded_vals = {0,  1,  2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                        11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,  0};
  auto [sum, underflow_count, overflow_count] = estimator.ComputeSum(encoded_vals);
  EXPECT_EQ(sum, 90.0);
  EXPECT_EQ(underflow_count, 2ul);
  EXPECT_EQ(overflow_count, 2ul);
}

// Construct an IntegerSumEstimator using parameters to specify the bucket ranges, and then estimate
// the sum of some encoded values while discarding under/overflow indices. Set |p| to zero and set
// the bucket width to 1, so that the estimated sum is the exact sum of the integers corresponding
// to the indices in |encoded_values| after dropping under/overflow indices.
TEST(IntegerSumEstimatorTest, FromParamsDiscard) {
  int64_t min_int = 0;
  int64_t max_int = 10;
  uint32_t partitions = 10;
  double p = 0.0;
  auto estimator = IntegerSumEstimator(min_int, max_int, partitions, p,
                                       /*underflow_strategy=*/IntegerSumEstimator::kDiscard,
                                       /*overflow_strategy=*/IntegerSumEstimator::kDiscard);
  std::vector<uint32_t> encoded_vals = {0,  1,  2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                        11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,  0};
  const auto& [sum, underflow_count, overflow_count] = estimator.ComputeSum(encoded_vals);
  EXPECT_EQ(sum, 90.0);
  EXPECT_EQ(underflow_count, 2ul);
  EXPECT_EQ(overflow_count, 2ul);
}

// Construct an IntegerSumEstimator using an IntegerBuckets proto, and then estimate the sum of some
// encoded values while clamping under/overflow indices. Set |p| to zero and set the bucket width
// to 1, so that the estimated sum is the exact sum of the integers corresponding to the indices in
// |encoded_values| after clamping under/overflow values.
TEST(IntegerSumEstimatorTest, FromBucketsClamp) {
  int64_t min_int = 0;
  uint32_t partitions = 10;
  uint32_t step_size = 1;
  double p = 0.0;
  auto buckets = MakeIntegerBuckets(min_int, partitions, step_size);
  auto estimator =
      IntegerSumEstimator(buckets, p, /*underflow_strategy=*/IntegerSumEstimator::kClamp,
                          /*overflow_strategy=*/IntegerSumEstimator::kClamp);
  std::vector<uint32_t> encoded_vals = {0,  1,  2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                        11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,  0};
  auto [sum, underflow_count, overflow_count] = estimator.ComputeSum(encoded_vals);
  EXPECT_EQ(sum, 110.0);
  EXPECT_EQ(underflow_count, 2ul);
  EXPECT_EQ(overflow_count, 2ul);
}

// Construct an IntegerSumEstimator using parameters to specify the bucket ranges, and then estimate
// the sum of some encoded values while clamping under/overflow indices. Set |p| to zero and set
// the bucket width to 1, so that the estimated sum is the exact sum of the integers corresponding
// to the indices in |encoded_values| after clamping under/overflow values.
TEST(IntegerSumEstimatorTest, FromParamsClamp) {
  int64_t min_int = 0;
  int64_t max_int = 10;
  uint32_t partitions = 10;
  double p = 0.0;
  auto estimator = IntegerSumEstimator(min_int, max_int, partitions, p,
                                       /*underflow_strategy=*/IntegerSumEstimator::kClamp,
                                       /*overflow_strategy=*/IntegerSumEstimator::kClamp);
  std::vector<uint32_t> encoded_vals = {0,  1,  2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                        11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,  0};
  const auto& [sum, underflow_count, overflow_count] = estimator.ComputeSum(encoded_vals);
  EXPECT_EQ(sum, 110.0);
  EXPECT_EQ(underflow_count, 2ul);
  EXPECT_EQ(overflow_count, 2ul);
}

// Construct an encoder using buckets with step size greater than 1, and estimate the sum of some
// encoded values while discarding under/overflow indices.
TEST(IntegerSumEstimatorTest, FromLargeBucketsDiscard) {
  int64_t min_int = 0;
  uint32_t partitions = 5;
  uint32_t step_size = 2;
  double p = 0.0;
  auto buckets = MakeIntegerBuckets(min_int, partitions, step_size);
  auto estimator =
      IntegerSumEstimator(buckets, p, /*underflow_strategy=*/IntegerSumEstimator::kDiscard,
                          /*overflow_strategy_=*/IntegerSumEstimator::kDiscard);
  std::vector<uint32_t> encoded_vals = {0, 1, 2, 3, 4, 5, 6, 6, 5, 4, 3, 2, 1, 0};
  const auto& [sum, underflow_count, overflow_count] = estimator.ComputeSum(encoded_vals);
  EXPECT_EQ(sum, 40.0);
  EXPECT_EQ(underflow_count, 2ul);
  EXPECT_EQ(overflow_count, 2ul);
}

// Construct an encoder using buckets with step size greater than 1, and estimate the sum of some
// encoded values while clamping under/overflow indices.
TEST(IntegerSumEstimatorTest, LargeBucketsClamp) {
  int64_t min_int = 0;
  uint32_t partitions = 5;
  uint32_t step_size = 2;
  double p = 0.0;
  auto buckets = MakeIntegerBuckets(min_int, partitions, step_size);
  auto estimator =
      IntegerSumEstimator(buckets, p, /*underflow_strategy=*/IntegerSumEstimator::kClamp,
                          /*overflow_strategy_=*/IntegerSumEstimator::kClamp);
  std::vector<uint32_t> encoded_vals = {0, 1, 2, 3, 4, 5, 6, 6, 5, 4, 3, 2, 1, 0};
  const auto& [sum, underflow_count, overflow_count] = estimator.ComputeSum(encoded_vals);
  EXPECT_EQ(sum, 60.0);
  EXPECT_EQ(underflow_count, 2ul);
  EXPECT_EQ(overflow_count, 2ul);
}

// Construct an IntegerSumEstimator using an IntegerBuckets proto, and then estimate the sum of some
// encoded values while discarding under/overflow indices. Set |p| to zero, so that the estimated
// sum is the exact sum of the integers corresponding to the indices in |encoded_values| after
// discarding under/overflow indices.
TEST(IntegerSumEstimatorTest, NonzeroPDiscard) {
  int64_t min_int = 0;
  uint32_t partitions = 10;
  uint32_t step_size = 1;
  double p = 0.1;
  auto buckets = MakeIntegerBuckets(min_int, partitions, step_size);
  auto estimator =
      IntegerSumEstimator(buckets, p, /*underflow_strategy=*/IntegerSumEstimator::kDiscard,
                          /*overflow_strategy_=*/IntegerSumEstimator::kDiscard);
  std::vector<uint32_t> encoded_vals = {0,  1,  2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                        11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,  0};
  auto [sum, underflow_count, overflow_count] = estimator.ComputeSum(encoded_vals);
  // The raw sum is 90.0 and the bias is 9.0.
  EXPECT_EQ(sum, 81.0);
  EXPECT_EQ(underflow_count, 2ul);
  EXPECT_EQ(overflow_count, 2ul);
}

// Construct an IntegerSumEstimator using an IntegerBuckets proto, and then estimate the sum of some
// encoded values while discarding under/overflow indices. Set |p| to zero, so that the estimated
// sum is the exact sum of the integers corresponding to the indices in |encoded_values| after
// clamping under/overflow indices.
TEST(IntegerSumEstimatorTest, NonzeroPClamp) {
  int64_t min_int = 0;
  uint32_t partitions = 10;
  uint32_t step_size = 1;
  double p = 0.1;
  auto buckets = MakeIntegerBuckets(min_int, partitions, step_size);
  auto estimator =
      IntegerSumEstimator(buckets, p, /*underflow_strategy=*/IntegerSumEstimator::kClamp,
                          /*overflow_strategy_=*/IntegerSumEstimator::kClamp);
  std::vector<uint32_t> encoded_vals = {0,  1,  2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                        11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,  0};
  auto [sum, underflow_count, overflow_count] = estimator.ComputeSum(encoded_vals);
  // The raw sum is 110.0 and the bias is 11.0.
  EXPECT_EQ(sum, 99.0);
  EXPECT_EQ(underflow_count, 2ul);
  EXPECT_EQ(overflow_count, 2ul);
}

// Construct an encoder using buckets with step size greater than 1, lower bound greater than 0, and
// p > 0, and estimate the sum of some encoded values while discarding under/overflow indices.
TEST(IntegerSumEstimatorTest, ShiftedRangeNonzeroPDiscard) {
  int64_t min_int = 10;
  uint32_t partitions = 5;
  uint32_t step_size = 2;
  double p = 0.1;
  auto buckets = MakeIntegerBuckets(min_int, partitions, step_size);
  auto estimator =
      IntegerSumEstimator(buckets, p, /*underflow_strategy=*/IntegerSumEstimator::kDiscard,
                          /*overflow_strategy_=*/IntegerSumEstimator::kDiscard);
  std::vector<uint32_t> encoded_vals = {0, 1, 2, 3, 4, 5, 6, 6, 5, 4, 3, 2, 1, 0};
  const auto& [sum, underflow_count, overflow_count] = estimator.ComputeSum(encoded_vals);
  // The raw sum is 140.0 and the bias is 14.0.
  EXPECT_EQ(sum, 126.0);
  EXPECT_EQ(underflow_count, 2ul);
  EXPECT_EQ(overflow_count, 2ul);
}

// Construct an encoder using buckets with step size greater than 1, lower bound greater than 0, and
// p > 0, and estimate the sum of some encoded values while clamping under/overflow indices.
TEST(IntegerSumEstimatorTest, ShiftedRangeNonzeroPClamp) {
  int64_t min_int = 10;
  uint32_t partitions = 5;
  uint32_t step_size = 2;
  double p = 0.1;
  auto buckets = MakeIntegerBuckets(min_int, partitions, step_size);
  auto estimator =
      IntegerSumEstimator(buckets, p, /*underflow_strategy=*/IntegerSumEstimator::kClamp,
                          /*overflow_strategy_=*/IntegerSumEstimator::kClamp);
  std::vector<uint32_t> encoded_vals = {0, 1, 2, 3, 4, 5, 6, 6, 5, 4, 3, 2, 1, 0};
  const auto& [sum, underflow_count, overflow_count] = estimator.ComputeSum(encoded_vals);
  // The raw sum is 200.0 and the bias is 20.0.
  EXPECT_EQ(sum, 180.0);
  EXPECT_EQ(underflow_count, 2ul);
  EXPECT_EQ(overflow_count, 2ul);
}

}  // namespace cobalt

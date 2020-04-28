#include "src/algorithms/experimental/archived/two_dim_rappor_histogram_encoder.h"

#include "src/algorithms/experimental/random.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

using testing::ElementsAre;
using testing::FloatNear;
using testing::Pair;
using testing::Pointwise;

namespace cobalt {

class ArchivedTwoDimRapporHistogramEncoderTest : public ::testing::Test {
 protected:
  void SetUp() override { gen_ = std::make_unique<RandomNumberGenerator>(0); }

  RandomNumberGenerator* GetGenerator() { return gen_.get(); }

 private:
  std::unique_ptr<RandomNumberGenerator> gen_;
};

TEST_F(ArchivedTwoDimRapporHistogramEncoderTest, EncodeAllZero) {
  auto encoder = ArchivedTwoDimRapporHistogramEncoder(GetGenerator(), 20, 50, 0.0002);
  std::vector<uint64_t> histogram(20, 0);
  auto encoded = encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(Pair(17, 39)));
}

TEST_F(ArchivedTwoDimRapporHistogramEncoderTest, Encode) {
  auto encoder = ArchivedTwoDimRapporHistogramEncoder(GetGenerator(), 20, 50, 0.0002);
  std::vector<uint64_t> histogram = {0, 0, 1, 0, 0, 10, 60, 1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0};
  auto encoded = encoder.Encode(histogram);
  EXPECT_THAT(encoded, ElementsAre(Pair(2, 1), Pair(5, 10), Pair(6, 50), Pair(7, 1), Pair(15, 3),
                                   Pair(17, 34)));
}

TEST_F(ArchivedTwoDimRapporHistogramEncoderTest, EstimateSum) {
  auto estimator = ArchivedTwoDimRapporHistogramSumEstimator(20, 50, 0.0002);
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

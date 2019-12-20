#include "src/algorithms/experimental/randomized_response.h"

#include "src/algorithms/experimental/random.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

using testing::ElementsAre;

namespace cobalt {

class ResponseRandomizerTest : public ::testing::Test {
 protected:
  void SetUp() override { gen_ = std::make_unique<RandomNumberGenerator>(); }
  RandomNumberGenerator* GetGenerator() { return gen_.get(); }

 private:
  std::unique_ptr<RandomNumberGenerator> gen_;
};

// Encode each index from 0 to 999 inclusive, giving a data-independent response with probability
// p = 0.25 and a max index of 999. The probability that an index is equal to its own encoding is:
// (1 - p) + p / (max_index + 1) = 0.7525.
//
// Count how many of the 1000 indices differ from their encoding. Fail if a Z-test at level 0.01
// rejects the hypothesis that Pr[i == randomizer.Encode(i)] is equal to 0.2475.
TEST_F(ResponseRandomizerTest, Encode) {
  uint32_t max_index = 999;
  double p = 0.25;
  auto randomizer = ResponseRandomizer(GetGenerator(), max_index, p);
  std::vector<uint32_t> encoded_indices;
  for (uint32_t i = 0; i <= max_index; i++) {
    encoded_indices.push_back(randomizer.Encode(i));
  }
  int diff = 0;
  for (uint32_t i = 0; i < max_index; i++) {
    if (i != encoded_indices[i]) {
      ++diff;
    }
  }
  // The test passes if the absolute value of the following statistic is less than z_{.99} = 2.576:
  // ( diff - 0.2475 * 1000 ) / ( sqrt( diff * ( 1 - diff / ( 1000 ))))
  // This condition is equivalent to 215 <= diff <= 284.
  EXPECT_GE(diff, 215);
  EXPECT_LE(diff, 284);
}

// Compute the frequency of each index in a test vector.
TEST_F(ResponseRandomizerTest, EstimateFrequencies) {
  uint32_t max_index = 9;
  auto estimator = FrequencyEstimator(max_index);
  std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4};
  auto frequencies = estimator.GetFrequencies(indices);
  EXPECT_THAT(frequencies, ElementsAre(3, 3, 3, 3, 3, 0, 0, 0, 0, 0));
}

}  // namespace cobalt

#include "src/algorithms/experimental/distributions.h"

#include "src/algorithms/experimental/random.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

class DistributionsTest : public ::testing::Test {
 protected:
  void SetUp() override { gen_ = std::make_unique<RandomNumberGenerator>(55); }
  RandomNumberGenerator* GetGenerator() { return gen_.get(); }

 private:
  std::unique_ptr<RandomNumberGenerator> gen_;
};

TEST_F(DistributionsTest, BernoulliSample) {
  double p_0 = 0.0;
  auto b_0 = BernoulliDistribution(GetGenerator(), p_0);
  auto sample_0 = b_0.Sample();
  EXPECT_FALSE(sample_0);

  double p_1 = 1.0;
  auto b_1 = BernoulliDistribution(GetGenerator(), p_1);
  auto sample_1 = b_1.Sample();
  EXPECT_TRUE(sample_1);

  double p = 0.5;
  auto b = BernoulliDistribution(GetGenerator(), p);
  auto sample = b.Sample();
  EXPECT_TRUE(sample || !sample);
}

TEST_F(DistributionsTest, BinomialSample) {
  uint64_t num_trials = 100;
  double p_0 = 0.0;
  auto b_0 = BinomialDistribution(GetGenerator(), num_trials, p_0);
  auto sample_0 = b_0.Sample();
  EXPECT_EQ(sample_0, 0ul);

  double p_1 = 1.0;
  auto b_1 = BinomialDistribution(GetGenerator(), num_trials, p_1);
  auto sample_1 = b_1.Sample();
  EXPECT_EQ(sample_1, 100ul);

  // Depends on the seed passed to |gen_|.
  double p = 0.5;
  auto b = BinomialDistribution(GetGenerator(), num_trials, p);
  auto sample = b.Sample();
  EXPECT_EQ(sample, 55ul);
}

TEST_F(DistributionsTest, DiscreteUniformSample) {
  uint32_t min = 0;
  uint32_t max = 9;
  auto u = DiscreteUniformDistribution(GetGenerator(), min, max);
  for (int i = 0; i < 1000; i++) {
    auto sample = u.Sample();
    EXPECT_GE(sample, min);
    EXPECT_LE(sample, max);
  }
}

TEST_F(DistributionsTest, PoissonSample) {
  int mean = 5;
  int sigma = mean;
  auto u = PoissonDistribution(GetGenerator(), mean);
  int count_more_than_2_sigma = 0;
  for (int i = 0; i < 1000; i++) {
    auto sample = u.Sample();
    if (sample > mean + 2 * sigma || sample < mean - 2 * sigma) {
      count_more_than_2_sigma++;
    }
  }
  EXPECT_LT(count_more_than_2_sigma, 50);
}

}  // namespace cobalt

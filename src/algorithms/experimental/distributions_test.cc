#include "src/algorithms/experimental/distributions.h"

#include "src/algorithms/experimental/random.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

class DistributionsTest : public ::testing::Test {
 protected:
  void SetUp() override { gen_ = std::make_unique<RandomNumberGenerator>(); }
  RandomNumberGenerator* GetGenerator() { return gen_.get(); }

 private:
  std::unique_ptr<RandomNumberGenerator> gen_;
};

TEST_F(DistributionsTest, DiscreteUniformSample) {
  uint32_t min = 0;
  uint32_t max = 9;
  auto u = DiscreteUniformDistribution(GetGenerator(), min, max);
  auto sample = u.Sample();
  EXPECT_GE(sample, min);
  EXPECT_LE(sample, max);
}

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

}  // namespace cobalt

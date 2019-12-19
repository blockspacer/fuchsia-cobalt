#include "src/algorithms/experimental/random.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

TEST(RandomNumberGenerator, Generate) {
  auto gen = RandomNumberGenerator();
  uint32_t val = gen();
  EXPECT_GE(val, 0u);
  EXPECT_LE(val, UINT32_MAX);
}

TEST(RandomNumberGenerator, MinMax) {
  auto gen = RandomNumberGenerator();
  EXPECT_EQ(gen.min(), 0u);
  EXPECT_EQ(gen.max(), UINT32_MAX);
}

TEST(FakeRandomNumberGenerator, Generate) {
  uint32_t val = 100;
  auto fake_gen = FakeRandomNumberGenerator(val);
  EXPECT_EQ(fake_gen(), val);
}

TEST(FakeRandomNumberGenerator, MinMax) {
  uint32_t val = 100;
  auto fake_gen = FakeRandomNumberGenerator(val);
  EXPECT_EQ(fake_gen.min(), 0u);
  EXPECT_EQ(fake_gen.max(), UINT32_MAX);
}

}  // namespace cobalt

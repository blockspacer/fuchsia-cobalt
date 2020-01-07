// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/algorithms/experimental/count_min.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

TEST(CountMin, Basic) {
  std::vector<std::pair<std::string, uint32_t>> test_cases = {
      {"Hello World", 100},
      {"Hallo Welt", 20},
      {"Hola Mundo", 73},
      {"Bonjour, Monde", 22},
  };
  CountMin count_min(2000, 10);
  for (auto [data, count] : test_cases) {
    count_min.Increment(data, count);
  }

  for (auto [data, count] : test_cases) {
    EXPECT_EQ(count, count_min.GetCount(data));
  }
}

TEST(CountMin, GetSketch) {
  std::vector<std::pair<std::string, uint32_t>> test_cases = {
      {"Hello World", 100},
      {"Hallo Welt", 20},
      {"Hola Mundo", 73},
      {"Bonjour, Monde", 22},
  };
  CountMin count_min(2000, 10);
  for (auto [data, count] : test_cases) {
    count_min.Increment(data, count);
  }

  auto count_min_cells = count_min.GetSketch();
  CountMin count_min_copy(2000, 10, *count_min_cells);

  for (auto [data, count] : test_cases) {
    EXPECT_EQ(count, count_min_copy.GetCount(data));
  }
}

}  // namespace cobalt

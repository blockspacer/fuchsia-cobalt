// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "algorithms/forculus/polynomial_computations.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace forculus {

namespace {
FieldElement FromBytes(std::vector<byte>&& bytes) {
  return FieldElement(std::move(bytes));
}

FieldElement FromInt(uint32_t x) {
  std::vector<byte> bytes(sizeof(x));
  std::memcpy(bytes.data(), &x, sizeof(x));
  return FieldElement(std::move(bytes));
}
}  // namespace

TEST(PolynomialComputationsTest, TestEvaluateSmallPolynomial) {
  // NOTE: This test only makes sense with our temporary implementation of
  // FieldElement. When we switch to the real implementation this test
  // will have to change.

  // Construct the 2nd degree polynomial 5 + 7x + 9x^2
  std::vector<FieldElement> coefficients;
  for (byte i = 5; i <= 9; i += 2) {
    coefficients.emplace_back(FromInt(i));
  }

  // When we evaluate a polynomial at x=0 we should get the constant term.
  EXPECT_EQ(coefficients[0], Evaluate(coefficients, FieldElement(false)));

  // When we evaluate a polynomial at x=1 we should get the sum of the
  // coefficients.
  FieldElement sum(false);
  for (const FieldElement& el : coefficients) {
    sum += el;
  }
  EXPECT_EQ(sum, Evaluate(coefficients, FieldElement(true)));

  // Evaluate at x = 2. Expect 5 + 14 + 36 = 55.
  EXPECT_EQ(FromInt(55), Evaluate(coefficients, FromInt(2)));

  // Evaluate at x = 10. Expect 5 + 70 + 900 = 975 = ox3CF.
  EXPECT_EQ(FromBytes({0xCF, 3}), Evaluate(coefficients, FromInt(10)));
}

TEST(PolynomialComputationsTest, TestEvaluateLargerPolynomial) {
  // Construct a 19th degree polynomial.
  std::vector<FieldElement> coefficients;
  for (byte i = 1; i < 21; i++) {
    coefficients.emplace_back(FromInt(i));
  }

  // When we evaluate a polynomial at x=0 we should get the constant term.
  EXPECT_EQ(coefficients[0], Evaluate(coefficients, FieldElement(false)));

  // When we evaluate a polynomial at x=1 we should get the sum of the
  // coefficients.
  FieldElement sum(false);
  for (const FieldElement& el : coefficients) {
    sum += el;
  }
  EXPECT_EQ(sum, Evaluate(coefficients, FieldElement(true)));
}

TEST(PolynomialComputationsTest, TestInterpolateSmallPolynomial) {
  // NOTE: This test only makes sense with our temporary implementation of
  // FieldElement. When we switch to the real implementation this test
  // will have to change.

  // Construct the 2nd degree polynomial 5 + 7x + 9x^2
  std::vector<FieldElement> coefficients;
  for (byte i = 5; i <= 9; i += 2) {
    coefficients.emplace_back(FromInt(i));
  }

  // Construct the x-values 2, 3, 4
  std::vector<FieldElement> x_values;
  for (byte i = 2; i < 5; i++) {
    x_values.emplace_back(FromInt(i));
  }

  // Evaluate the 3 corresponding y values.
  std::vector<FieldElement> y_values(3, FieldElement(false));
  size_t i = 0;
  for (const FieldElement& x : x_values) {
    y_values[i++] = Evaluate(coefficients, x);
  }

  // The InterpolateConstant function wants vectors of pointers.
  std::vector<const FieldElement*> x_value_pointers(3);
  std::vector<const FieldElement*> y_value_pointers(3);
  for (size_t i = 0; i < 3; i++) {
    x_value_pointers[i] = &x_values[i];
    y_value_pointers[i] = &y_values[i];
  }

  // Interpolate to recover the constant term.
  FieldElement constant_term =
      InterpolateConstant(x_value_pointers, y_value_pointers);

  EXPECT_EQ(coefficients[0], constant_term);
}

// Constructs the polynomial f(x) = c0 + c1*x + ... c_{n-1}*x^{n-1}
// where ci = c0 + i*c_step and n = num_points.
//
// Constructs n x-values: x0, x1, ... x_{n-1} where x_i = x.
//
// Evaluates n y-values: y0, y1, ... y_{n-1} where y_i = f(x_i)
//
// Invokes the function InterpolateConstat() and checks that we get back c0.
void DoInterpolationTest(size_t num_points, uint32_t c0, uint32_t c_step,
                         uint32_t x0, uint32_t x_step) {
  // Construct the coefficients of the polynomial.
  std::vector<FieldElement> coefficients;
  uint32_t c = c0;
  for (size_t i = 0; i < num_points; i++) {
    coefficients.emplace_back(FromInt(c));
    c += c_step;
  }

  // Construct x values x0=10, x1=11, x2=12, ... x{n-1}=10+{n-1}.
  std::vector<FieldElement> x_values;
  uint32_t x = x0;
  for (size_t i = 0; i < num_points; i++) {
    x_values.emplace_back(FromInt(x));
    x += x_step;
  }

  // Evaluate the polynomial at each of the x values.
  std::vector<FieldElement> y_values(num_points, FieldElement(false));
  for (size_t i = 0; i < num_points; i++) {
    y_values[i] = Evaluate(coefficients, x_values[i]);
  }

  // The InterpolateConstant function wants vectors of pointers.
  std::vector<const FieldElement*> x_value_pointers(num_points);
  std::vector<const FieldElement*> y_value_pointers(num_points);
  for (size_t i = 0; i < num_points; i++) {
    x_value_pointers[i] = &x_values[i];
    y_value_pointers[i] = &y_values[i];
  }

  // Interpolate to recover the constant term.
  FieldElement constant_term =
      InterpolateConstant(x_value_pointers, y_value_pointers);

  // Check that we got the right constant term.
  EXPECT_EQ(coefficients[0], constant_term)
      << num_points << ", " << c0 << ", " << c_step << ", " << x0 << ", "
      << x_step;
}

TEST(PolynomialComputationsTest, TestInterpolate) {
  std::vector<size_t> num_points_cases({2, 3, 20, 50});
  std::vector<uint32_t> c0_cases({1, 10000, 100000, 1000000000});
  std::vector<uint32_t> c_step_cases({1, 7, 111});
  std::vector<uint32_t> x0_cases({1, 999});
  for (size_t num_points : num_points_cases) {
    for (uint32_t c0 : c0_cases) {
      for (int32_t c_step : c_step_cases) {
        for (int32_t x0 : x0_cases) {
          DoInterpolationTest(num_points, c0, c_step, x0, 1);
        }
      }
    }
  }
}

}  // namespace forculus
}  // namespace cobalt

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

namespace cobalt::forculus {

FieldElement Evaluate(const std::vector<FieldElement>& coefficients,
                      const FieldElement& x) {
  size_t num_coefficients = coefficients.size();
  FieldElement y = coefficients[num_coefficients - 1];
  for (int i = static_cast<int>(num_coefficients) - 2; i >= 0; i--) {
    y *= x;
    y += coefficients[i];
  }
  return y;
}

FieldElement InterpolateConstant(
    const std::vector<const FieldElement*>& x_values,
    const std::vector<const FieldElement*>& y_values) {
  size_t num_values = x_values.size();
  // Our goal is to find c0, the constant term of the polynomial that passes
  // through all of the points we were given. We use Lagrange Interpolation:
  // https://en.wikipedia.org/wiki/Lagrange_polynomial

  // Start by computing to the product of the x_i.
  FieldElement product_of_xi(true);  // initialize to one
  for (size_t i = 0; i < num_values; i++) {
    product_of_xi *= *x_values[i];
  }

  // Next compute :
  //
  //                              y_i
  // sigma = Sum_i  -----------------------------------
  //                 x_i * product_{j != i} (x_j - x_i)
  //
  //
  FieldElement sigma(false);  // initialize to zero
  for (size_t i = 0; i < num_values; i++) {
    FieldElement prod_delta_ji(true);  // initialize to one
    for (size_t j = 0; j < num_values; j++) {
      if (j == i) {
        continue;
      }
      prod_delta_ji *= (*x_values[j] - *x_values[i]);
    }
    sigma += *y_values[i] / (*x_values[i] * prod_delta_ji);
  }

  // Finally our desired value is product_of_xi * sigma.
  return product_of_xi * sigma;
}

}  // namespace cobalt::forculus

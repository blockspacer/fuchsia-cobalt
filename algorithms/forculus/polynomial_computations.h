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

#ifndef COBALT_ALGORITHMS_FORCULUS_POLYNOMIAL_COMPUTATIONS_H_
#define COBALT_ALGORITHMS_FORCULUS_POLYNOMIAL_COMPUTATIONS_H_

#include <vector>

#include "algorithms/forculus/field_element.h"

namespace cobalt::forculus {

// Some utility functions for computing with polynomials over the Forculus
// field.

// Computes f(x) where f is the polynomial c0 + c1*x + c2*x^2 + ... cn*x^n
// where n = coefficients.size() and ci = coefficients[i].
FieldElement Evaluate(const std::vector<FieldElement>& coefficients, const FieldElement& x);

// Computes the constant term c0 of the unique polynomial of degree d that
// passes through the points (x0, y0), (x1, y1), ... (x_{d}, y_{d})
// xi = x_values[i], yi = y_values[i] and d = x_values.size() - 1.
// REQUIRES: x_values.size() == y_value.size() and the x_values are distinct.
FieldElement InterpolateConstant(const std::vector<const FieldElement*>& x_values,
                                 const std::vector<const FieldElement*>& y_values);

}  // namespace cobalt::forculus

#endif  // COBALT_ALGORITHMS_FORCULUS_POLYNOMIAL_COMPUTATIONS_H_

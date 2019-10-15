// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/local_aggregation/aggregation_utils.h"

namespace cobalt::local_aggregation {

OnDeviceAggregationWindow MakeDayWindow(int num_days) {
  OnDeviceAggregationWindow window;
  window.set_days(AggregationDays(num_days));
  return window;
}

OnDeviceAggregationWindow MakeHourWindow(int num_hours) {
  OnDeviceAggregationWindow window;
  window.set_hours(AggregationHours(num_hours));
  return window;
}

}  // namespace cobalt::local_aggregation

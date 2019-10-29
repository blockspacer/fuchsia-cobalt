// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef COBALT_SRC_LOCAL_AGGREGATION_AGGREGATION_UTILS_H_
#define COBALT_SRC_LOCAL_AGGREGATION_AGGREGATION_UTILS_H_

#include "src/registry/aggregation_window.pb.h"

namespace cobalt::local_aggregation {

// Creates and returns an OnDeviceAggregationWindow of |num_days| days.
OnDeviceAggregationWindow MakeDayWindow(int num_days);

// Creates and returns an OnDeviceAggregationWindow of |num_hours| hours.
OnDeviceAggregationWindow MakeHourWindow(int num_hours);

}  // namespace cobalt::local_aggregation

#endif  // COBALT_SRC_LOCAL_AGGREGATION_AGGREGATION_UTILS_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef COBALT_SRC_LOCAL_AGGREGATION_AGGREGATION_UTILS_H_
#define COBALT_SRC_LOCAL_AGGREGATION_AGGREGATION_UTILS_H_

#include <optional>

#include "src/logger/status.h"
#include "src/registry/aggregation_window.pb.h"
#include "src/registry/report_definition.pb.h"

namespace cobalt::local_aggregation {

// Creates and returns an OnDeviceAggregationWindow of |num_days| days.
OnDeviceAggregationWindow MakeDayWindow(int num_days);

// Creates and returns an OnDeviceAggregationWindow of |num_hours| hours.
OnDeviceAggregationWindow MakeHourWindow(int num_hours);

// Encapsulates the logic for how a stored numeric aggregate should be updated given the
// |aggregation_type|. Returns an updated value if the status is kOK. Returns an error and 0
// otherwise.
//
// aggregation_type: the aggregation_type found in the ReportDefinition of the event aggregate being
//                   updated.
// stored_aggregate: the value stored in the AggregateStore. Should not contain a value if no
//                   aggregate exists.
// new_value: the value with which the aggregate should be updated.
std::tuple<logger::Status, int64_t> GetUpdatedAggregate(
    ReportDefinition::OnDeviceAggregationType aggregation_type,
    std::optional<int64_t> stored_aggregate, int64_t new_value);

}  // namespace cobalt::local_aggregation

#endif  // COBALT_SRC_LOCAL_AGGREGATION_AGGREGATION_UTILS_H_

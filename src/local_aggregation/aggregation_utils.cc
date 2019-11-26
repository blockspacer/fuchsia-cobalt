// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/local_aggregation/aggregation_utils.h"

#include <optional>

#include "src/logging.h"

namespace cobalt::local_aggregation {

using logger::kInvalidArguments;
using logger::kOK;

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

std::tuple<logger::Status, int64_t> GetUpdatedAggregate(
    ReportDefinition::OnDeviceAggregationType aggregation_type,
    std::optional<int64_t> stored_aggregate, int64_t new_value) {
  if (!stored_aggregate.has_value()) {
    return std::make_tuple(kOK, new_value);
  }

  switch (aggregation_type) {
    case ReportDefinition::SUM:
      return std::make_tuple(kOK, new_value + stored_aggregate.value());
    case ReportDefinition::MAX:
      return std::make_tuple(kOK, std::max(new_value, stored_aggregate.value()));
    case ReportDefinition::MIN:
      return std::make_tuple(kOK, std::min(new_value, stored_aggregate.value()));
    default:
      LOG(ERROR) << "Unexpected aggregation type " << aggregation_type;
      return std::make_tuple(kInvalidArguments, 0);
  }
}
}  // namespace cobalt::local_aggregation

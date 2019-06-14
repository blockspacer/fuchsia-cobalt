// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_TYPES_H_
#define COBALT_LOGGER_TYPES_H_

#include <memory>
#include <string>

#include "./event.pb.h"
#include "./observation2.pb.h"
#include "config/metric_definition.pb.h"
#include "config/report_definition.pb.h"

namespace cobalt {
namespace logger {

// A HistogramPtr provides a moveable way of passing the buckets of a Histogram.
typedef std::unique_ptr<google::protobuf::RepeatedPtrField<HistogramBucket>>
    HistogramPtr;

// A EventValuesPtr provides a moveable way of passing the dimensions of a
// custom event.
typedef std::unique_ptr<
    google::protobuf::Map<std::string, CustomDimensionValue>>
    EventValuesPtr;

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_TYPES_H_

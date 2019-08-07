// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOGGER_EVENT_RECORD_H_
#define COBALT_SRC_LOGGER_EVENT_RECORD_H_

#include <memory>

#include "src/pb/event.pb.h"
#include "src/registry/metric_definition.pb.h"

namespace cobalt {
namespace logger {

// A container for an Event proto message and the MetricDefinition for which
// that Event should be logged.
struct EventRecord {
  const MetricDefinition* metric;
  std::unique_ptr<Event> event = std::make_unique<Event>();
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_SRC_LOGGER_EVENT_RECORD_H_

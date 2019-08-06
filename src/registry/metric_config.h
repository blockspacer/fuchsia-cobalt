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

#ifndef COBALT_SRC_REGISTRY_METRIC_CONFIG_H_
#define COBALT_SRC_REGISTRY_METRIC_CONFIG_H_

#include "src/registry/config.h"
#include "src/registry/metrics.pb.h"

namespace cobalt {
namespace config {

// A container for all of the Metrics registered in Cobalt. This
// is used only on the Analyzer.
using MetricRegistry = Registry<RegisteredMetrics>;
using SystemProfileFields = google::protobuf::RepeatedField<int>;

// For ease of understanding we specify the interfaces below as if
// MetricRegistry were not a template specializations but a stand-alone class.

/*
class MetricRegistry {
 public:
  // Returns the number of Metrics in this registry.
  size_t size();

  // Returns the Metric with the given ID triple, or nullptr if there is
  // no such Metric. The caller does not take ownership of the returned
  // pointer.
  const Metric* const Get(uint32_t customer_id,
                          uint32_t project_id,
                          uint32_t id);
};
*/

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_SRC_REGISTRY_METRIC_CONFIG_H_

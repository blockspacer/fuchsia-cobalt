// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_PROJECT_CONTEXT_H_
#define COBALT_ENCODER_PROJECT_CONTEXT_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "src/registry/client_config.h"
#include "src/registry/encoding_config.h"
#include "src/registry/metric_config.h"

namespace cobalt {
namespace encoder {

// A ProjectContext represents a particular Cobalt project and contains
// a registry of the Metrics and EncodingConfigs contained in the project.
class ProjectContext {
 public:
  // Constructs a ProjectContext for the project with the given IDs
  // and ClientConfig.
  ProjectContext(uint32_t customer_id, uint32_t project_id,
                 const std::shared_ptr<config::ClientConfig>& client_config);

  // Returns the Metric with the given ID in the project, or nullptr if there is
  // no such Metric. The caller does not take ownership of the returned
  // pointer.
  [[nodiscard]] const class Metric* Metric(uint32_t id) const;

  // Returns the Metric with the given name in the project, or nullptr if there
  // is no such Metric. The caller does not take ownership of the returned
  // pointer.
  [[nodiscard]] const class Metric* Metric(const std::string& metric_name) const;

  // Returns a map from MetricPart names to default encoding_ids for the metric
  // given by |id|. If no such metric exists, returns an empty map.
  const std::unordered_map<std::string, uint32_t>& DefaultEncodingsForMetric(uint32_t id);

  // Returns the EncodingConfig with the given ID in the project, or nullptr if
  // there is no such EncodingConfig. The caller does not take ownership of the
  // returned pointer.
  [[nodiscard]] const EncodingConfig* EncodingConfig(uint32_t id) const;

  [[nodiscard]] uint32_t customer_id() const { return customer_id_; }

  [[nodiscard]] uint32_t project_id() const { return project_id_; }

 private:
  const uint32_t customer_id_, project_id_;

  // The keys are metric_ids and the values are maps from MetricPart names to
  // default encoding_ids.
  std::unordered_map<uint32_t, std::unordered_map<std::string, uint32_t>> default_encodings_;

  std::shared_ptr<config::ClientConfig> client_config_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_PROJECT_CONTEXT_H_

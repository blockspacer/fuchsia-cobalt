// Copyright 2017 The Fuchsia Authors
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

#ifndef COBALT_CONFIG_ANALYZER_CONFIG_H_
#define COBALT_CONFIG_ANALYZER_CONFIG_H_

#include <memory>
#include <string>
#include <utility>

#include "config/cobalt_registry.pb.h"
#include "config/encoding_config.h"
#include "config/encodings.pb.h"
#include "config/metric_config.h"
#include "config/metrics.pb.h"
#include "config/report_config.h"
#include "config/report_configs.pb.h"

namespace cobalt {
namespace config {

// AnalyzerConfig provides a convenient interface to the Cobalt configuration
// system that is intended to be used by the Analyzer server processes.
class AnalyzerConfig {
 public:
  // Constructs and returns an instance of AnalyzerConfig using information
  // from the flags to find the configuration data.
  static std::unique_ptr<AnalyzerConfig> CreateFromFlagsOrDie();

  // Constructs and returns an instance of AnalyzerConfig by swapping all of
  // the datat out of |config|.
  static std::unique_ptr<AnalyzerConfig> CreateFromCobaltRegistryProto(CobaltRegistry* config);

  // Constructs and returns an instance of AnalyzerConfig by first parsing
  // a CobaltRegistry proto message from cobalt_registry_proto_text which should
  // contain an ASCII text representation of a CobaltRegistryProto, and then
  // invoking CreateFromCobaltconfigProto.
  static std::unique_ptr<AnalyzerConfig> CreateFromCobaltRegistryProtoText(
      std::string cobalt_registry_proto_text);

  // Constructs an AnalyzerConfig that wraps the given registries.
  AnalyzerConfig(std::shared_ptr<config::EncodingRegistry> encoding_configs,
                 std::shared_ptr<config::MetricRegistry> metrics,
                 std::shared_ptr<config::ReportRegistry> report_configs);

  // Returns the EncodingConfig with the given ID triple, or nullptr if there is
  // no such EncodingConfig. The caller does not take ownership of the returned
  // pointer.
  const EncodingConfig* EncodingConfig(uint32_t customer_id, uint32_t project_id,
                                       uint32_t encoding_config_id);

  // Returns the Metric with the given ID triple, or nullptr if there is
  // no such Metric. The caller does not take ownership of the returned
  // pointer.
  const Metric* Metric(uint32_t customer_id, uint32_t project_id, uint32_t metric_id);

  // Returns the ReportConfig with the given ID triple, or nullptr if there is
  // no such ReportConfig. The caller does not take ownership of the returned
  // pointer.
  const ReportConfig* ReportConfig(uint32_t customer_id, uint32_t project_id,
                                   uint32_t report_config_id);

  // Returns a shared pointer to the ReportRegistry.
  std::shared_ptr<config::ReportRegistry> report_registry() { return report_configs_; }

 private:
  std::shared_ptr<config::EncodingRegistry> encoding_configs_;
  std::shared_ptr<config::MetricRegistry> metrics_;
  std::shared_ptr<config::ReportRegistry> report_configs_;
};

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_ANALYZER_CONFIG_H_

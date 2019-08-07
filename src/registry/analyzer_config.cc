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

#include "src/registry/analyzer_config.h"

#include <memory>
#include <string>
#include <utility>

#include "gflags/gflags.h"
#include "glog/logging.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "src/lib/util/log_based_metrics.h"
#include "src/registry/config_text_parser.h"
#include "src/registry/encoding_config.h"
#include "src/registry/encodings.pb.h"
#include "src/registry/metric_config.h"
#include "src/registry/metrics.pb.h"
#include "src/registry/report_config.h"
#include "src/registry/report_configs.pb.h"

namespace cobalt {
namespace config {

using google::protobuf::io::ColumnNumber;
using google::protobuf::io::ErrorCollector;

DEFINE_string(cobalt_config_dir, "", "Path to the Cobalt configuration directory");
DEFINE_string(cobalt_encoding_configs_file_name, "registered_encodings.txt",
              "Name of the file within cobalt_config_dir that contains the "
              "registered EncodingConfigs.");
DEFINE_string(cobalt_metrics_file_name, "registered_metrics.txt",
              "Name of the file within cobalt_config_dir that contains the "
              "registered Metrics.");
DEFINE_string(cobalt_report_configs_file_name, "registered_reports.txt",
              "Name of the file within cobalt_config_dir that contains the "
              "registered ReportConfigs.");

// Stackdriver metric constants
namespace {
constexpr char kAnalyzerConfigError[] = "analyzer-config-error";
constexpr char kCreateFromCobaltRegistryProtoFailure[] =
    "analyzer-config-create-from-cobalt-config-proto-failure";
}  // namespace

namespace {
class LoggingErrorCollector : public ErrorCollector {
 public:
  virtual ~LoggingErrorCollector() {}

  void AddError(int line, ColumnNumber column, const std::string& message) override {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAnalyzerConfigError)
        << "file: " << file_name << " line: " << line << " column: " << column << " " << message;
  }

  void AddWarning(int line, ColumnNumber column, const std::string& message) override {
    LOG(WARNING) << "file: " << file_name << " line: " << line << " column: " << column << " "
                 << message;
  }

  std::string file_name;
};

std::string ErrorMessage(Status status) {
  DCHECK(status != kOK) << "Invoke this only with an error status.";
  switch (status) {
    case kFileOpenError:
      return "Unable to open file: ";

    case kParsingError:
      return "Error while parsing file: ";

    case kDuplicateRegistration:
      return "Duplicate ID found in file: ";

    default:
      return "Unknown problem with: ";
  }
}

}  // namespace

std::unique_ptr<AnalyzerConfig> AnalyzerConfig::CreateFromFlagsOrDie() {
  CHECK(!FLAGS_cobalt_config_dir.empty()) << "-cobalt_config_dir is a mandatory flag";
  CHECK(!FLAGS_cobalt_encoding_configs_file_name.empty())
      << "-cobalt_encoding_configs_file_name is a mandatory flag";
  CHECK(!FLAGS_cobalt_metrics_file_name.empty()) << "-cobalt_metrics_file_name is a mandatory flag";
  CHECK(!FLAGS_cobalt_report_configs_file_name.empty())
      << "-cobalt_report_configs_file_name is a mandatory flag";

  LoggingErrorCollector error_collector;

  std::string file_path = FLAGS_cobalt_config_dir + "/" + FLAGS_cobalt_encoding_configs_file_name;
  error_collector.file_name = file_path;

  auto encodings = FromFile<RegisteredEncodings>(file_path, &error_collector);
  if (encodings.second != config::kOK) {
    LOG(FATAL) << "Error getting EncodingConfigs from registry. " << ErrorMessage(encodings.second)
               << file_path;
  }

  file_path = FLAGS_cobalt_config_dir + "/" + FLAGS_cobalt_metrics_file_name;
  error_collector.file_name = file_path;

  auto metrics = FromFile<RegisteredMetrics>(file_path, nullptr);
  if (metrics.second != config::kOK) {
    LOG(FATAL) << "Error getting Metrics from registry. " << ErrorMessage(metrics.second)
               << file_path;
  }

  file_path = FLAGS_cobalt_config_dir + "/" + FLAGS_cobalt_report_configs_file_name;
  error_collector.file_name = file_path;

  auto report_configs = FromFile<RegisteredReports>(file_path, nullptr);
  if (report_configs.second != config::kOK) {
    LOG(FATAL) << "Error getting ReportConfigs from registry. "
               << ErrorMessage(report_configs.second) << file_path;
  }

  LOG(INFO) << "Read Cobalt configuration from " << FLAGS_cobalt_config_dir << ".";

  return std::unique_ptr<AnalyzerConfig>(
      new AnalyzerConfig(std::shared_ptr<config::EncodingRegistry>(encodings.first.release()),
                         std::shared_ptr<config::MetricRegistry>(metrics.first.release()),
                         std::shared_ptr<config::ReportRegistry>(report_configs.first.release())));
}

std::unique_ptr<AnalyzerConfig> AnalyzerConfig::CreateFromCobaltRegistryProto(
    CobaltRegistry* config) {
  LoggingErrorCollector error_collector;

  RegisteredEncodings registered_encodings;
  registered_encodings.mutable_element()->Swap(config->mutable_encoding_configs());
  auto encodings = EncodingRegistry::TakeFrom(&registered_encodings, &error_collector);
  if (encodings.second != config::kOK) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kCreateFromCobaltRegistryProtoFailure)
        << "Error getting EncodingConfigs from registry. " << ErrorMessage(encodings.second);
    return std::unique_ptr<AnalyzerConfig>(nullptr);
  }

  RegisteredMetrics registered_metrics;
  registered_metrics.mutable_element()->Swap(config->mutable_metric_configs());
  auto metrics = MetricRegistry::TakeFrom(&registered_metrics, &error_collector);
  if (metrics.second != config::kOK) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kCreateFromCobaltRegistryProtoFailure)
        << "Error getting Metrics from registry. " << ErrorMessage(metrics.second);
    return std::unique_ptr<AnalyzerConfig>(nullptr);
  }

  RegisteredReports registered_reports;
  registered_reports.mutable_element()->Swap(config->mutable_report_configs());
  auto reports = ReportRegistry::TakeFrom(&registered_reports, &error_collector);
  if (reports.second != config::kOK) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kCreateFromCobaltRegistryProtoFailure)
        << "Error getting ReportConfigs from registry. " << ErrorMessage(reports.second);
    return std::unique_ptr<AnalyzerConfig>(nullptr);
  }

  return std::unique_ptr<AnalyzerConfig>(
      new AnalyzerConfig(std::shared_ptr<config::EncodingRegistry>(encodings.first.release()),
                         std::shared_ptr<config::MetricRegistry>(metrics.first.release()),
                         std::shared_ptr<config::ReportRegistry>(reports.first.release())));
}

std::unique_ptr<AnalyzerConfig> AnalyzerConfig::CreateFromCobaltRegistryProtoText(
    std::string cobalt_registry_proto_text) {
  CobaltRegistry cobalt_config;
  google::protobuf::TextFormat::Parser parser;
  if (!parser.ParseFromString(cobalt_registry_proto_text, &cobalt_config)) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kCreateFromCobaltRegistryProtoFailure)
        << "Error while parsing a CobaltRegistry ASCII proto string.";
    return std::unique_ptr<AnalyzerConfig>(nullptr);
  }
  return CreateFromCobaltRegistryProto(&cobalt_config);
}

AnalyzerConfig::AnalyzerConfig(std::shared_ptr<config::EncodingRegistry> encoding_configs,
                               std::shared_ptr<config::MetricRegistry> metrics,
                               std::shared_ptr<config::ReportRegistry> report_configs)
    : encoding_configs_(encoding_configs), metrics_(metrics), report_configs_(report_configs) {}

const EncodingConfig* AnalyzerConfig::EncodingConfig(uint32_t customer_id, uint32_t project_id,
                                                     uint32_t encoding_config_id) {
  return encoding_configs_->Get(customer_id, project_id, encoding_config_id);
}

const Metric* AnalyzerConfig::Metric(uint32_t customer_id, uint32_t project_id,
                                     uint32_t metric_id) {
  return metrics_->Get(customer_id, project_id, metric_id);
}

const ReportConfig* AnalyzerConfig::ReportConfig(uint32_t customer_id, uint32_t project_id,
                                                 uint32_t report_config_id) {
  return report_configs_->Get(customer_id, project_id, report_config_id);
}

}  // namespace config
}  // namespace cobalt

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

#include "gflags/gflags.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace config {

DECLARE_string(cobalt_config_dir);
DECLARE_string(cobalt_encoding_configs_file_name);
DECLARE_string(cobalt_metrics_file_name);
DECLARE_string(cobalt_report_configs_file_name);

TEST(AnalyzerConfigTest, ValidFiles) {
  // Read from the default files in the "demo" directory.
  FLAGS_cobalt_config_dir = "config/demo";
  auto config = AnalyzerConfig::CreateFromFlagsOrDie();

  // Read from specified files in the "test_files" directory.
  FLAGS_cobalt_config_dir = "config/test_files";
  FLAGS_cobalt_encoding_configs_file_name = "registered_encodings_valid.txt";
  FLAGS_cobalt_metrics_file_name = "registered_metrics_valid.txt";
  FLAGS_cobalt_report_configs_file_name = "registered_reports_valid.txt";
  config = AnalyzerConfig::CreateFromFlagsOrDie();
  // Sanity check the contents.
  EXPECT_NE(nullptr, config->EncodingConfig(1, 1, 3));
  EXPECT_NE(nullptr, config->EncodingConfig(1, 1, 4));
  EXPECT_EQ(nullptr, config->EncodingConfig(1, 1, 5));
  EXPECT_NE(nullptr, config->Metric(2, 1, 2));
  EXPECT_EQ(nullptr, config->Metric(2, 1, 3));
  EXPECT_NE(nullptr, config->ReportConfig(2, 1, 1));
  EXPECT_EQ(nullptr, config->ReportConfig(2, 2, 1));
}

TEST(AnalyzerConfigTest, BadDirectoryNameDeathTest) {
  FLAGS_cobalt_config_dir = "/there/is/no/such/directory";
  ASSERT_DEATH(AnalyzerConfig::CreateFromFlagsOrDie(), "Unable to open file");
}

TEST(AnalyzerConfigTest, BadFileNameDeathTest) {
  FLAGS_cobalt_config_dir = "config/demo";
  FLAGS_cobalt_encoding_configs_file_name = "bad_file_name.txt";
  ASSERT_DEATH(AnalyzerConfig::CreateFromFlagsOrDie(), "Unable to open file");
}

TEST(AnalyzerConfigTest, NotValidAsciiProtoFileDeathTest) {
  FLAGS_cobalt_config_dir = "config";
  FLAGS_cobalt_encoding_configs_file_name = "analyzer_config_test.cc";
  ASSERT_DEATH(AnalyzerConfig::CreateFromFlagsOrDie(), "Error while parsing file");
}

TEST(AnalyzerConfigTest, DuplicateRegistrationDeathTest) {
  FLAGS_cobalt_config_dir = "config/test_files";
  FLAGS_cobalt_encoding_configs_file_name = "registered_encodings_contains_duplicate.txt";
  ASSERT_DEATH(AnalyzerConfig::CreateFromFlagsOrDie(), "Duplicate ID found in file");
}

TEST(AnalyzerConfigTest, ValidCobaltRegistryProto) {
  CobaltRegistry cobalt_config;

  EncodingConfig *encoding;
  encoding = cobalt_config.add_encoding_configs();
  encoding->set_customer_id(1);
  encoding->set_project_id(1);
  encoding->set_id(3);

  encoding = cobalt_config.add_encoding_configs();
  encoding->set_customer_id(1);
  encoding->set_project_id(1);
  encoding->set_id(4);

  Metric *metric;
  metric = cobalt_config.add_metric_configs();
  metric->set_customer_id(2);
  metric->set_project_id(1);
  metric->set_id(2);

  metric = cobalt_config.add_metric_configs();
  metric->set_customer_id(2);
  metric->set_project_id(1);
  metric->set_id(3);

  ReportConfig *report;
  report = cobalt_config.add_report_configs();
  report->set_customer_id(1);
  report->set_project_id(1);
  report->set_id(2);

  report = cobalt_config.add_report_configs();
  report->set_customer_id(1);
  report->set_project_id(1);
  report->set_id(3);

  auto config = AnalyzerConfig::CreateFromCobaltRegistryProto(&cobalt_config);

  EXPECT_NE(nullptr, config->EncodingConfig(1, 1, 3));
  EXPECT_NE(nullptr, config->EncodingConfig(1, 1, 4));
  EXPECT_EQ(nullptr, config->EncodingConfig(1, 1, 5));

  EXPECT_NE(nullptr, config->Metric(2, 1, 2));
  EXPECT_NE(nullptr, config->Metric(2, 1, 3));
  EXPECT_EQ(nullptr, config->Metric(2, 1, 4));

  EXPECT_NE(nullptr, config->ReportConfig(1, 1, 2));
  EXPECT_NE(nullptr, config->ReportConfig(1, 1, 3));
  EXPECT_EQ(nullptr, config->ReportConfig(1, 1, 4));
}

}  // namespace config
}  // namespace cobalt

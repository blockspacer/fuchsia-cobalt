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

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_GENERATOR_ABSTRACT_TEST_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_GENERATOR_ABSTRACT_TEST_H_

#include "analyzer/report_master/report_generator.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./observation.pb.h"
#include "analyzer/report_master/report_exporter.h"
#include "config/config_text_parser.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/project_context.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

// This file contains type-parameterized tests of ReportGenerator.
//
// We use C++ templates along with the macros TYPED_TEST_CASE_P and
// TYPED_TEST_P in order to define test templates that may be instantiated to
// to produce concrete tests that use various implementations of Datastore.
//
// See report_generator_test.cc and report_generator_emulator_test.cc for the
// concrete instantiations.
//
// NOTE: If you add a new test to this file you must add its name to the
// invocation REGISTER_TYPED_TEST_CASE_P macro at the bottom of this file.

namespace cobalt {
namespace analyzer {

namespace testing {

const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;
const uint32_t kMetricId = 1;
const uint32_t kJointReportConfigId = 1;
const uint32_t kRawDumpReportConfigId = 1;
const uint32_t kGroupedReportConfigId = 3;
const uint32_t kGroupedRawDumpReportConfigId = 4;
const uint32_t kForculusEncodingConfigId = 1;
const uint32_t kBasicRapporEncodingConfigId = 2;
const uint32_t kNoOpEncodingConfigId = 3;
const char kPartName1[] = "Part1";
const char kPartName2[] = "Part2";
const size_t kForculusThreshold = 20;

// This unix timestamp corresponds to Friday Dec 2, 2016 in UTC
const time_t kSomeTimestamp = 1480647356;
// This is the day index for Friday Dec 2, 2016
const uint32_t kDayIndex = 17137;

const char* kMetricConfigText = R"(
# Metric 1 has two string parts.
element {
  customer_id: 1
  project_id: 1
  id: 1
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
    }
  }
  parts {
    key: "Part2"
    value {
    }
  }
}

)";

const char* kEncodingConfigText = R"(
# EncodingConfig 1 is Forculus.
element {
  customer_id: 1
  project_id: 1
  id: 1
  forculus {
    threshold: 20
  }
}

# EncodingConfig 2 is Basic RAPPOR.
element {
  customer_id: 1
  project_id: 1
  id: 2
  basic_rappor {
    prob_0_becomes_1: 0.25
    prob_1_stays_1: 0.75
    string_categories: {
      category: "Apple"
      category: "Banana"
      category: "Cantaloupe"
    }
  }
}

# EncodingConfig 3 is NoOp.
element {
  customer_id: 1
  project_id: 1
  id: 3
  no_op_encoding {
  }
}

)";

const char* kReportConfigText = R"(
# ReportConfig 1 specifies a JOINT report of both variables of Metric 1.
# We use this config only in order to run HISTOGRAM reports on the
# two variables separately since JOINT reports are not currently
# implemented.
element {
  customer_id: 1
  project_id: 1
  id: 1
  metric_id: 1
  variable {
    metric_part: "Part1"
  }
  variable {
    metric_part: "Part2"
  }
  report_type: JOINT
  export_configs {
    csv {}
    gcs {
      bucket: "BUCKET-NAME"
    }
  }
}

# ReportConfig 2 specifies a RAW_DUMP report of both variables of Metric 1.
element {
  customer_id: 1
  project_id: 1
  id: 2
  metric_id: 1
  variable {
    metric_part: "Part1"
  }
  variable {
    metric_part: "Part2"
  }
  report_type: RAW_DUMP
  export_configs {
    csv {}
    gcs {
      bucket: "BUCKET-NAME"
    }
  }
}

element {
  customer_id: 1
  project_id: 1
  id: 3
  metric_id: 1
  variable {
    metric_part: "Part1"
  }
  variable {
    metric_part: "Part2"
  }
  system_profile_field: [BOARD_NAME]
  report_type: JOINT
  export_configs {
    csv {}
    gcs {
      bucket: "BUCKET-NAME"
    }
  }
}

element {
  customer_id: 1
  project_id: 1
  id: 4
  metric_id: 1
  variable {
    metric_part: "Part1"
  }
  variable {
    metric_part: "Part2"
  }
  system_profile_field: [BOARD_NAME]
  report_type: RAW_DUMP
  export_configs {
    csv {}
    gcs {
      bucket: "BUCKET-NAME"
    }
  }
}


)";

// An implementation of GcsUploadInterface that saves its parameters and
// returns OK.
struct FakeGcsUploader : public GcsUploadInterface {
  grpc::Status UploadToGCS(const std::string& bucket, const std::string& path,
                           const std::string& mime_type,
                           ReportStream* report_stream) override {
    this->upload_was_invoked = true;
    this->bucket = bucket;
    this->path = path;
    this->mime_type = mime_type;
    this->serialized_report =
        std::string(std::istreambuf_iterator<char>(*report_stream), {});
    return grpc::Status::OK;
  }

  bool upload_was_invoked = false;
  std::string bucket;
  std::string path;
  std::string mime_type;
  std::string serialized_report;
};

}  // namespace testing

// ReportGeneratorAbstractTest is templatized on the parameter
// |StoreFactoryClass| which must be the name of a class that contains the
// following method: static DataStore* NewStore()
// See MemoryStoreFactory in sotre/memory_store_test_helper.h and
// BigtableStoreEmulatorFactory in store/bigtable_emulator_helper.h.
template <class StoreFactoryClass>
class ReportGeneratorAbstractTest : public ::testing::Test {
 protected:
  ReportGeneratorAbstractTest()
      : data_store_(StoreFactoryClass::NewStore()),
        observation_store_(new store::ObservationStore(data_store_)),
        report_store_(new store::ReportStore(data_store_)),
        fake_uploader_(new testing::FakeGcsUploader()) {
    report_id_.set_customer_id(testing::kCustomerId);
    report_id_.set_project_id(testing::kProjectId);
    report_id_.set_report_config_id(testing::kJointReportConfigId);
  }

  void SetUp() {
    // Clear the DataStore.
    EXPECT_EQ(store::kOK,
              data_store_->DeleteAllRows(store::DataStore::kObservations));
    EXPECT_EQ(store::kOK,
              data_store_->DeleteAllRows(store::DataStore::kReportMetadata));
    EXPECT_EQ(store::kOK,
              data_store_->DeleteAllRows(store::DataStore::kReportRows));

    // Parse the metric config string
    auto metric_parse_result = config::FromString<RegisteredMetrics>(
        testing::kMetricConfigText, nullptr);
    EXPECT_EQ(config::kOK, metric_parse_result.second);
    std::shared_ptr<config::MetricRegistry> metric_registry(
        metric_parse_result.first.release());

    // Parse the encoding config string
    auto encoding_parse_result = config::FromString<RegisteredEncodings>(
        testing::kEncodingConfigText, nullptr);
    EXPECT_EQ(config::kOK, encoding_parse_result.second);
    std::shared_ptr<config::EncodingRegistry> encoding_config_registry(
        encoding_parse_result.first.release());

    // Parse the report config string
    auto report_parse_result = config::FromString<RegisteredReports>(
        testing::kReportConfigText, nullptr);
    EXPECT_EQ(config::kOK, report_parse_result.second);
    std::shared_ptr<config::ReportRegistry> report_config_registry(
        report_parse_result.first.release());

    // Make a ProjectContext
    project_.reset(new encoder::ProjectContext(
        testing::kCustomerId, testing::kProjectId,
        std::make_shared<config::ClientConfig>(encoding_config_registry,
                                               metric_registry)));

    std::shared_ptr<config::AnalyzerConfig> analyzer_config(
        new config::AnalyzerConfig(encoding_config_registry, metric_registry,
                                   report_config_registry));
    std::shared_ptr<config::AnalyzerConfigManager> analyzer_config_manager(
        new config::AnalyzerConfigManager(analyzer_config));

    // Make the ReportGenerator
    std::unique_ptr<ReportExporter> report_exporter(
        new ReportExporter(fake_uploader_));
    report_generator_.reset(
        new ReportGenerator(analyzer_config_manager, observation_store_,
                            report_store_, std::move(report_exporter)));
  }

  // Makes an Observation with two string parts, both of which have the
  // given |string_value|, using the encoding with the given encoding_config_id.
  std::unique_ptr<Observation> MakeObservation(std::string string_value,
                                               uint32_t encoding_config_id) {
    // Construct a new Encoder with a new client secret.
    encoder::Encoder encoder(project_,
                             encoder::ClientSecret::GenerateNewSecret());
    // Set a static current time so we know we have a static day_index.
    encoder.set_current_time(testing::kSomeTimestamp);

    // Construct the two-part value to add.
    encoder::Encoder::Value value;
    value.AddStringPart(encoding_config_id, testing::kPartName1, string_value);
    value.AddStringPart(encoding_config_id, testing::kPartName2, string_value);

    // Encode an observation.
    encoder::Encoder::Result result = encoder.Encode(testing::kMetricId, value);
    EXPECT_EQ(encoder::Encoder::kOK, result.status);
    EXPECT_TRUE(result.observation.get() != nullptr);
    EXPECT_EQ(2, result.observation->parts_size());
    return std::move(result.observation);
  }

  // Adds to the ObservationStore |num_clients| observations of our test metric
  // that each encode the given string |value| using the given
  // |encoding_config_id|. Each Observation is generated as if from a different
  // client.
  void AddObservations(std::string value, uint32_t encoding_config_id,
                       int num_clients) {
    AddObservations(value, encoding_config_id, num_clients,
                    std::make_unique<SystemProfile>());
  }
  void AddObservations(std::string value, uint32_t encoding_config_id,
                       int num_clients,
                       std::unique_ptr<SystemProfile> profile) {
    std::vector<Observation> observations;
    for (int i = 0; i < num_clients; i++) {
      observations.emplace_back(*MakeObservation(value, encoding_config_id));
    }
    ObservationMetadata metadata;
    metadata.set_customer_id(testing::kCustomerId);
    metadata.set_project_id(testing::kProjectId);
    metadata.set_metric_id(testing::kMetricId);
    metadata.set_day_index(testing::kDayIndex);
    metadata.set_allocated_system_profile(profile.release());
    EXPECT_EQ(store::kOK,
              observation_store_->AddObservationBatch(metadata, observations));
  }

  struct GeneratedReport {
    ReportMetadataLite metadata;
    ReportRows rows;
  };

  // Uses the ReportGenerator to generate a HISTOGRAM report that analyzes the
  // specified variable of our two-variable test metric. |variable_index| must
  // be either 0 or 1. It will also be used for the sequence_num.
  // If |export_report| is true then the report will be exported using
  // our FakeGcsUploader. If |in_store| is true the report will be saved
  // to the ReportStore.
  GeneratedReport GenerateHistogramReport(int variable_index,
                                          bool export_report, bool in_store) {
    // Complete the report_id by specifying the sequence_num.
    report_id_.set_sequence_num(variable_index);

    // Start a report for the specified variable, for the interval of days
    // [kDayIndex, kDayIndex].
    std::string export_name = export_report ? "export_name" : "";
    EXPECT_EQ(store::kOK, report_store_->StartNewReport(
                              testing::kDayIndex, testing::kDayIndex, true,
                              export_name, in_store, HISTOGRAM,
                              {(uint32_t)variable_index}, &report_id_));

    // Generate the report
    EXPECT_TRUE(report_generator_->GenerateReport(report_id_).ok());

    // Fetch the report from the ReportStore.
    GeneratedReport report;
    EXPECT_EQ(store::kOK, report_store_->GetReport(report_id_, &report.metadata,
                                                   &report.rows));

    return report;
  }

  GeneratedReport GenerateGroupedHistogramReport(int variable_index,
                                                 bool export_report,
                                                 bool in_store) {
    report_id_.set_report_config_id(testing::kGroupedReportConfigId);
    report_id_.set_sequence_num(variable_index);
    std::string export_name = export_report ? "export_name" : "";
    EXPECT_EQ(store::kOK, report_store_->StartNewReport(
                              testing::kDayIndex, testing::kDayIndex, true,
                              export_name, in_store, HISTOGRAM,
                              {(uint32_t)variable_index}, &report_id_));
    EXPECT_TRUE(report_generator_->GenerateReport(report_id_).ok());

    GeneratedReport report;
    EXPECT_EQ(store::kOK, report_store_->GetReport(report_id_, &report.metadata,
                                                   &report.rows));

    return report;
  }

  GeneratedReport GenerateRawDumpReport(bool export_report, bool in_store) {
    report_id_.set_sequence_num(0);

    // Start a report for the specified variable, for the interval of days
    // [kDayIndex, kDayIndex].
    std::string export_name = export_report ? "export_name" : "";
    EXPECT_EQ(store::kOK,
              report_store_->StartNewReport(
                  testing::kDayIndex, testing::kDayIndex, true, export_name,
                  in_store, RAW_DUMP, {0, 1}, &report_id_));

    // Generate the report
    EXPECT_TRUE(report_generator_->GenerateReport(report_id_).ok());

    // Fetch the report from the ReportStore.
    GeneratedReport report;
    EXPECT_EQ(store::kOK, report_store_->GetReport(report_id_, &report.metadata,
                                                   &report.rows));

    return report;
  }

  GeneratedReport GenerateGroupedRawDumpReport(bool export_report,
                                               bool in_store) {
    report_id_.set_report_config_id(testing::kGroupedRawDumpReportConfigId);
    report_id_.set_sequence_num(0);
    // Start a report for the specified variable, for the interval of days
    // [kDayIndex, kDayIndex].
    std::string export_name = export_report ? "export_name" : "";
    EXPECT_EQ(store::kOK,
              report_store_->StartNewReport(
                  testing::kDayIndex, testing::kDayIndex, true, export_name,
                  in_store, RAW_DUMP, {0, 1}, &report_id_));

    // Generate the report
    EXPECT_TRUE(report_generator_->GenerateReport(report_id_).ok());

    // Fetch the report from the ReportStore.
    GeneratedReport report;
    EXPECT_EQ(store::kOK, report_store_->GetReport(report_id_, &report.metadata,
                                                   &report.rows));

    return report;
  }

  // Adds to the ObservationStore a bunch of Observations of our test metric
  // that use our test Forculus encoding config in which the Forculus threshold
  // is 20. Each Observation is generated as if from a different client.
  // We simulate 20 clients adding "hello", 19 clients adding "goodbye", and
  // 21 clients adding "peace". Thus we expect "hello" and "peace" to appear
  // in the generated report but not "goodybe".
  void AddForculusObservations() {
    // Add 20 copies of the Observation "hello"
    AddObservations("hello", testing::kForculusEncodingConfigId,
                    testing::kForculusThreshold);

    // Add 19 copies of the Observation "goodbye"
    AddObservations("goodbye", testing::kForculusEncodingConfigId,
                    testing::kForculusThreshold - 1);

    // Add 21 copies of the Observation "peace"
    AddObservations("peace", testing::kForculusEncodingConfigId,
                    testing::kForculusThreshold + 1);
  }

  // This is the CSV that should be generated when the report for metric part 2
  // is exported, when Forculus Observations are added, based on the
  // Observations that are added in AddForculusObservations() above.
  const char* const kExpectedPart2ForculusCSV = R"(date,Part2,count,err
2016-12-2,"hello",20.000,0
2016-12-2,"peace",21.000,0
)";

  // This method should be invoked after invoking AddForculusObservations()
  // and then GenerateReport. It checks the generated Report to make sure
  // it is correct given the Observations that were added and the Forculus
  // config.
  void CheckForculusReport(const GeneratedReport& report, uint variable_index,
                           const std::string& expected_export_csv) {
    EXPECT_EQ(HISTOGRAM, report.metadata.report_type());
    EXPECT_EQ(1, report.metadata.variable_indices_size());
    EXPECT_EQ(variable_index, report.metadata.variable_indices(0));
    if (report.metadata.in_store()) {
      EXPECT_EQ(2, report.rows.rows_size());
      for (const auto& report_row : report.rows.rows()) {
        EXPECT_EQ(0, report_row.histogram().std_error());
        ValuePart recovered_value;
        EXPECT_TRUE(report_row.histogram().has_value());
        recovered_value = report_row.histogram().value();

        EXPECT_EQ(ValuePart::kStringValue, recovered_value.data_case());
        std::string string_value = recovered_value.string_value();
        int count_estimate = report_row.histogram().count_estimate();
        switch (count_estimate) {
          case 20:
            EXPECT_EQ("hello", string_value);
            break;
          case 21:
            EXPECT_EQ("peace", string_value);
            break;
          default:
            FAIL();
        }
      }
    } else {
      EXPECT_EQ(0, report.rows.rows_size());
    }
    if (report.metadata.export_name() == "") {
      EXPECT_FALSE(this->fake_uploader_->upload_was_invoked);
    } else {
      EXPECT_TRUE(this->fake_uploader_->upload_was_invoked);
      // Reset for next time
      this->fake_uploader_->upload_was_invoked = false;
      EXPECT_EQ("BUCKET-NAME", fake_uploader_->bucket);
      EXPECT_EQ("1_1_1/export_name.csv", fake_uploader_->path);
      EXPECT_EQ("text/csv", fake_uploader_->mime_type);
      EXPECT_EQ(expected_export_csv, fake_uploader_->serialized_report);
    }
  }

  // Adds to the ObservationStore a bunch of Observations of our test metric
  // that use our test BasicRappor encoding config. We add 100 observations of
  // "Apple", 200 observations of "Banana", and 300 observations of
  // "Cantaloupe".
  void AddBasicRapporObservations() {
    AddObservations("Apple", testing::kBasicRapporEncodingConfigId, 100);
    AddObservations("Banana", testing::kBasicRapporEncodingConfigId, 200);
    AddObservations("Cantaloupe", testing::kBasicRapporEncodingConfigId, 300);
  }

  std::unique_ptr<SystemProfile> MakeProfile(std::string board_name) {
    auto profile = std::make_unique<SystemProfile>();
    *profile->mutable_board_name() = board_name;
    return profile;
  }
  void AddGroupedBasicRapporObservations() {
    AddObservations("Apple", testing::kBasicRapporEncodingConfigId, 50,
                    MakeProfile("foo"));
    AddObservations("Apple", testing::kBasicRapporEncodingConfigId, 50,
                    MakeProfile("bar"));
    AddObservations("Banana", testing::kBasicRapporEncodingConfigId, 100,
                    MakeProfile("foo"));
    AddObservations("Banana", testing::kBasicRapporEncodingConfigId, 100,
                    MakeProfile("bar"));
    AddObservations("Cantaloupe", testing::kBasicRapporEncodingConfigId, 150,
                    MakeProfile("foo"));
    AddObservations("Cantaloupe", testing::kBasicRapporEncodingConfigId, 150,
                    MakeProfile("bar"));
  }

  void AddUnencodedObservations() {
    AddObservations("Apple", testing::kNoOpEncodingConfigId, 1);
    AddObservations("Banana", testing::kNoOpEncodingConfigId, 2);
    AddObservations("Cantaloupe", testing::kNoOpEncodingConfigId, 3);
  }

  void AddGroupedUnencodedObservations() {
    AddObservations("Apple", testing::kNoOpEncodingConfigId, 1,
                    MakeProfile("foo"));
    AddObservations("Apple", testing::kNoOpEncodingConfigId, 1,
                    MakeProfile("bar"));
    AddObservations("Banana", testing::kNoOpEncodingConfigId, 2,
                    MakeProfile("foo"));
    AddObservations("Banana", testing::kNoOpEncodingConfigId, 2,
                    MakeProfile("bar"));
    AddObservations("Cantaloupe", testing::kNoOpEncodingConfigId, 3,
                    MakeProfile("foo"));
    AddObservations("Cantaloupe", testing::kNoOpEncodingConfigId, 3,
                    MakeProfile("bar"));
  }

  // This method should be invoked after invoking AddBasicRapporObservations()
  // and then GenerateReport. It checks the generated Report to make sure
  // it is correct given the Observations that were added. We are not attempting
  // to validate the Basic RAPPOR algorithm here so we simply test that the
  // all three strings appear with a non-zero count and under the correct
  // variable index.
  void CheckBasicRapporReport(const GeneratedReport& report,
                              uint variable_index) {
    EXPECT_EQ(HISTOGRAM, report.metadata.report_type());
    EXPECT_EQ(1, report.metadata.variable_indices_size());
    EXPECT_EQ(variable_index, report.metadata.variable_indices(0));
    if (report.metadata.in_store()) {
      EXPECT_EQ(3, report.rows.rows_size());
      for (const auto& report_row : report.rows.rows()) {
        EXPECT_NE(0, report_row.histogram().std_error());
        ValuePart recovered_value;
        EXPECT_TRUE(report_row.histogram().has_value());
        recovered_value = report_row.histogram().value();
        break;

        EXPECT_EQ(ValuePart::kStringValue, recovered_value.data_case());
        std::string string_value = recovered_value.string_value();
        EXPECT_TRUE(string_value == "Apple" || string_value == "Banana" ||
                    string_value == "Cantaloupe");

        EXPECT_GT(report_row.histogram().count_estimate(), 0);
      }
    } else {
      EXPECT_EQ(0, report.rows.rows_size());
    }
    if (report.metadata.export_name() == "") {
      EXPECT_FALSE(this->fake_uploader_->upload_was_invoked);
    } else {
      EXPECT_TRUE(this->fake_uploader_->upload_was_invoked);
      // Reset for next time
      this->fake_uploader_->upload_was_invoked = false;
      EXPECT_EQ("BUCKET-NAME", fake_uploader_->bucket);
      EXPECT_EQ("1_1_1/export_name.csv", fake_uploader_->path);
      EXPECT_EQ("text/csv", fake_uploader_->mime_type);
      EXPECT_FALSE(fake_uploader_->serialized_report.empty());
    }
  }

  void CheckGroupedRapporReport(const GeneratedReport& report,
                                uint variable_index) {
    EXPECT_EQ(HISTOGRAM, report.metadata.report_type());
    EXPECT_EQ(1, report.metadata.variable_indices_size());
    EXPECT_EQ(variable_index, report.metadata.variable_indices(0));
    int foo_count = 0;
    int bar_count = 0;
    if (report.metadata.in_store()) {
      EXPECT_EQ(6, report.rows.rows_size());
      for (const auto& report_row : report.rows.rows()) {
        EXPECT_NE(0, report_row.histogram().std_error());
        EXPECT_TRUE(report_row.histogram().has_value());
        std::string board_name =
            report_row.histogram().system_profile().board_name();
        if (board_name == "foo") foo_count += 1;
        if (board_name == "bar") bar_count += 1;
      }
      EXPECT_EQ(3, foo_count);
      EXPECT_EQ(3, bar_count);
    } else {
      EXPECT_EQ(0, report.rows.rows_size());
    }
    if (report.metadata.export_name() == "") {
      EXPECT_FALSE(this->fake_uploader_->upload_was_invoked);
    } else {
      EXPECT_TRUE(this->fake_uploader_->upload_was_invoked);
      this->fake_uploader_->upload_was_invoked = false;
      EXPECT_EQ("BUCKET-NAME", fake_uploader_->bucket);
      EXPECT_EQ("1_1_3/export_name.csv", fake_uploader_->path);
      EXPECT_EQ("text/csv", fake_uploader_->mime_type);
      EXPECT_FALSE(fake_uploader_->serialized_report.empty());
    }
  }

  void CheckRawDumpReport(const GeneratedReport& report) {
    EXPECT_EQ(RAW_DUMP, report.metadata.report_type());
    EXPECT_EQ(2, report.metadata.variable_indices_size());
    EXPECT_EQ(0u, report.metadata.variable_indices(0));
    EXPECT_EQ(1u, report.metadata.variable_indices(1));
    if (report.metadata.in_store()) {
      EXPECT_EQ(6, report.rows.rows_size());
    } else {
      EXPECT_EQ(0, report.rows.rows_size());
    }
    if (report.metadata.export_name() == "") {
      EXPECT_FALSE(this->fake_uploader_->upload_was_invoked);
    } else {
      EXPECT_TRUE(this->fake_uploader_->upload_was_invoked);
      // Reset for next time
      this->fake_uploader_->upload_was_invoked = false;
      EXPECT_EQ("BUCKET-NAME", fake_uploader_->bucket);
      EXPECT_EQ("1_1_1/export_name.csv", fake_uploader_->path);
      EXPECT_EQ("text/csv", fake_uploader_->mime_type);
      // Take the export CSV file and split it into lines.
      std::stringstream csv_stream(fake_uploader_->serialized_report);
      std::vector<std::string> csv_lines;
      std::string line;
      while (std::getline(csv_stream, line)) {
        csv_lines.push_back(line);
      }
      EXPECT_EQ(7u, csv_lines.size());
      // Check the header line.
      EXPECT_EQ("date,Part1,Part2", csv_lines[0]);
      // Check the body of the report. They are in random order so we
      // need to count them and check the totals.
      size_t apple_lines = 0;
      size_t banana_lines = 0;
      size_t cantaloupe_lines = 0;
      for (auto i = 1ul; i < csv_lines.size(); i++) {
        if (csv_lines[i] == "2016-12-2,\"Apple\",\"Apple\"") {
          apple_lines++;
        } else if (csv_lines[i] == "2016-12-2,\"Banana\",\"Banana\"") {
          banana_lines++;
        } else if (csv_lines[i] == "2016-12-2,\"Cantaloupe\",\"Cantaloupe\"") {
          cantaloupe_lines++;
        }
      }
      EXPECT_EQ(1u, apple_lines);
      EXPECT_EQ(2u, banana_lines);
      EXPECT_EQ(3u, cantaloupe_lines);
    }
  }

  void CheckGroupedRawDumpReport(const GeneratedReport& report) {
    EXPECT_EQ(RAW_DUMP, report.metadata.report_type());
    EXPECT_EQ(2, report.metadata.variable_indices_size());
    EXPECT_EQ(0u, report.metadata.variable_indices(0));
    EXPECT_EQ(1u, report.metadata.variable_indices(1));
    if (report.metadata.in_store()) {
      EXPECT_EQ(6, report.rows.rows_size());
    } else {
      EXPECT_EQ(0, report.rows.rows_size());
    }
    if (report.metadata.export_name() == "") {
      EXPECT_FALSE(this->fake_uploader_->upload_was_invoked);
    } else {
      EXPECT_TRUE(this->fake_uploader_->upload_was_invoked);
      // Reset for next time
      this->fake_uploader_->upload_was_invoked = false;
      EXPECT_EQ("BUCKET-NAME", fake_uploader_->bucket);
      EXPECT_EQ("1_1_4/export_name.csv", fake_uploader_->path);
      EXPECT_EQ("text/csv", fake_uploader_->mime_type);
      LOG(INFO) << "COUNT OF ROWS: " << report.rows.rows_size();
      for (const auto& row : report.rows.rows()) {
        LOG(INFO) << row.raw_dump().system_profile().board_name();
      }
      // Take the export CSV file and split it into lines.
      std::stringstream csv_stream(fake_uploader_->serialized_report);
      std::vector<std::string> csv_lines;
      std::string line;
      while (std::getline(csv_stream, line)) {
        csv_lines.push_back(line);
      }
      EXPECT_EQ(13u, csv_lines.size());
      // Check the header line.
      EXPECT_EQ("date,Part1,Part2,Board_Name", csv_lines[0]);
      // Check the body of the report. They are in random order so we
      // need to count them and check the totals.
      size_t apple_foo_lines = 0;
      size_t banana_foo_lines = 0;
      size_t cantaloupe_foo_lines = 0;
      size_t apple_bar_lines = 0;
      size_t banana_bar_lines = 0;
      size_t cantaloupe_bar_lines = 0;
      for (auto i = 1ul; i < csv_lines.size(); i++) {
        LOG(INFO) << csv_lines[i];
        if (csv_lines[i] == "2016-12-2,\"Apple\",\"Apple\",\"foo\"") {
          apple_foo_lines++;
        } else if (csv_lines[i] == "2016-12-2,\"Banana\",\"Banana\",\"foo\"") {
          banana_foo_lines++;
        } else if (csv_lines[i] ==
                   "2016-12-2,\"Cantaloupe\",\"Cantaloupe\",\"foo\"") {
          cantaloupe_foo_lines++;
        } else if (csv_lines[i] == "2016-12-2,\"Apple\",\"Apple\",\"bar\"") {
          apple_bar_lines++;
        } else if (csv_lines[i] == "2016-12-2,\"Banana\",\"Banana\",\"bar\"") {
          banana_bar_lines++;
        } else if (csv_lines[i] ==
                   "2016-12-2,\"Cantaloupe\",\"Cantaloupe\",\"bar\"") {
          cantaloupe_bar_lines++;
        }
      }
      EXPECT_EQ(1u, apple_foo_lines);
      EXPECT_EQ(2u, banana_foo_lines);
      EXPECT_EQ(3u, cantaloupe_foo_lines);
      EXPECT_EQ(1u, apple_bar_lines);
      EXPECT_EQ(2u, banana_bar_lines);
      EXPECT_EQ(3u, cantaloupe_bar_lines);
    }
  }

  ReportId report_id_;
  std::shared_ptr<encoder::ProjectContext> project_;
  std::shared_ptr<store::DataStore> data_store_;
  std::shared_ptr<store::ObservationStore> observation_store_;
  std::shared_ptr<store::ReportStore> report_store_;
  std::unique_ptr<ReportGenerator> report_generator_;
  std::shared_ptr<testing::FakeGcsUploader> fake_uploader_;
};

TYPED_TEST_CASE_P(ReportGeneratorAbstractTest);

// Tests that the ReportGenerator correctly generates a report for both
// variables of our two-variable metric when the ObservationStore has been
// filled with Observations of that metric that use our Forculus encoding.
// Note that *joint* reports have not yet been implemented.
TYPED_TEST_P(ReportGeneratorAbstractTest, Forculus) {
  this->AddForculusObservations();
  {
    SCOPED_TRACE("variable_index = 0");
    int variable_index = 0;
    // Don't export the report. Do store it to the store.
    bool in_store = true;
    auto report =
        this->GenerateHistogramReport(variable_index, false, in_store);
    this->CheckForculusReport(report, variable_index, "");
  }
  {
    SCOPED_TRACE("variable_index = 1");
    int variable_index = 1;
    // Do export the report. Do store it to the store.
    bool in_store = true;
    auto report = this->GenerateHistogramReport(variable_index, true, in_store);
    this->CheckForculusReport(report, variable_index,
                              this->kExpectedPart2ForculusCSV);
  }
  {
    SCOPED_TRACE("variable_index = 0");
    int variable_index = 0;
    // Don't export the report. Don't store it to the store.
    bool in_store = false;
    auto report =
        this->GenerateHistogramReport(variable_index, false, in_store);
    this->CheckForculusReport(report, variable_index, "");
  }
  {
    SCOPED_TRACE("variable_index = 1");
    int variable_index = 1;
    // Do export the report. Don't store it to the store.
    bool in_store = false;
    auto report = this->GenerateHistogramReport(variable_index, true, in_store);
    this->CheckForculusReport(report, variable_index,
                              this->kExpectedPart2ForculusCSV);
  }
}

// Tests that the ReportGenerator correctly generates a report for both
// variables of our two-variable metric when the ObservationStore has been
// filled with Observations of that metric that use our Basic RAPPOR encoding.
// Note that *joint* reports have not yet been implemented.
TYPED_TEST_P(ReportGeneratorAbstractTest, BasicRappor) {
  this->AddBasicRapporObservations();
  {
    SCOPED_TRACE("variable_index = 0");
    int variable_index = 0;
    // Do exort the report. Do store it to the store.
    bool in_store = true;
    auto report = this->GenerateHistogramReport(variable_index, true, in_store);
    this->CheckBasicRapporReport(report, variable_index);
  }
  {
    SCOPED_TRACE("variable_index = 1");
    int variable_index = 1;
    // Don't export the report. Do store it to the store.
    bool in_store = true;
    auto report =
        this->GenerateHistogramReport(variable_index, false, in_store);
    this->CheckBasicRapporReport(report, variable_index);
  }
}

TYPED_TEST_P(ReportGeneratorAbstractTest, GroupedBasicRappor) {
  this->AddGroupedBasicRapporObservations();
  {
    SCOPED_TRACE("variable_index = 0");
    int variable_index = 0;
    // Don't export the report. Do store it to the store.
    bool in_store = true;
    auto report =
        this->GenerateGroupedHistogramReport(variable_index, true, in_store);
    this->CheckGroupedRapporReport(report, variable_index);
  }
  {
    SCOPED_TRACE("variable_index = 1");
    int variable_index = 1;
    // Don't export the report. Do store it to the store.
    bool in_store = true;
    auto report =
        this->GenerateGroupedHistogramReport(variable_index, true, in_store);
    this->CheckGroupedRapporReport(report, variable_index);
  }
}

TYPED_TEST_P(ReportGeneratorAbstractTest, RawDump) {
  this->AddUnencodedObservations();
  // Do exort the report. Don't store it to the store.
  bool in_store = false;
  auto report = this->GenerateRawDumpReport(true, in_store);
  this->CheckRawDumpReport(report);
}

TYPED_TEST_P(ReportGeneratorAbstractTest, GroupedRawDump) {
  this->AddGroupedUnencodedObservations();
  // Do exort the report. Don't store it to the store.
  bool in_store = false;
  auto report = this->GenerateGroupedRawDumpReport(true, in_store);
  this->CheckGroupedRawDumpReport(report);
}

REGISTER_TYPED_TEST_CASE_P(ReportGeneratorAbstractTest, Forculus, BasicRappor,
                           RawDump, GroupedBasicRappor, GroupedRawDump);

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_GENERATOR_ABSTRACT_TEST_H_

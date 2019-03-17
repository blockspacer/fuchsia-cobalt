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

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_MASTER_SERVICE_ABSTRACT_TEST_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_MASTER_SERVICE_ABSTRACT_TEST_H_

#include "analyzer/report_master/report_master_service.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./observation.pb.h"
#include "analyzer/store/report_store_test_utils.h"
#include "config/config_text_parser.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/project_context.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/datetime_util.h"

// This file contains type-parameterized tests of ReportMasterService.
//
// We use C++ templates along with the macros TYPED_TEST_CASE_P and
// TYPED_TEST_P in order to define test templates that may be instantiated to
// to produce concrete tests that use various implementations of Datastore.
//
// See report_master_service_test.cc and
// report_master_service_emulator_test.cc for the concrete instantiations.
//
// NOTE: If you add a new test to this file you must add its name to the
// invocation REGISTER_TYPED_TEST_CASE_P macro at the bottom of this file.

namespace cobalt {
namespace analyzer {

using util::CalendarDate;

static const uint32_t kCustomerId = 1;
static const uint32_t kProjectId = 1;
static const uint32_t kMetricId1 = 1;
static const uint32_t kMetricId2 = 2;
static const uint32_t kMetricId3 = 3;
static const uint32_t kReportConfigId1 = 1;
static const uint32_t kReportConfigId2 = 2;
static const uint32_t kReportConfigId3 = 3;
static const uint32_t kReportConfigId4 = 4;
static const uint32_t kReportConfigId5 = 5;
static const uint32_t kReportConfigId6 = 6;
static const uint32_t kForculusEncodingConfigId = 1;
static const uint32_t kBasicRapporStringEncodingConfigId = 2;
static const uint32_t kBasicRapporIntEncodingConfigId = 3;
static const uint32_t kBasicRapporIndexEncodingConfigId = 4;
static const uint32_t kNoOpEncodingConfigId = 5;
static const char kPartName1[] = "Part1";
static const char kPartName2[] = "Part2";
static const size_t kForculusThreshold = 20;

// This unix timestamp corresponds to Friday Dec 2, 2016 in UTC
static const time_t kSomeTimestamp = 1480647356;
// This is the day index for Friday Dec 2, 2016
static const uint32_t kDayIndex = 17137;
// We will use a fake clock with the time fixed to this time in order
// to test that time-related fields are set correctly by ReportMasterService.
static const int64_t kFixedTimeSeconds = 1234567;

static const char* kMetricConfigText = R"(
# Metric 1 has one string part and one integer part.
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
      data_type: INT
    }
  }
}

# Metric 2 has one string part and one integer part.
element {
  customer_id: 1
  project_id: 1
  id: 2
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
    }
  }
  parts {
    key: "Part2"
    value {
      data_type: INT
    }
  }
  system_profile_field: [BOARD_NAME]
}

# Metric 3 has one INDEX part.
element {
  customer_id: 1
  project_id: 1
  id: 3
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
      data_type: INDEX
    }
  }
  system_profile_field: [BOARD_NAME]
}

)";

static const char* kEncodingConfigText = R"(
# EncodingConfig 1 is Forculus.
element {
  customer_id: 1
  project_id: 1
  id: 1
  forculus {
    threshold: 20
  }
}

# EncodingConfig 2 is Basic RAPPOR with string candidates (non-stochastic)
element {
  customer_id: 1
  project_id: 1
  id: 2
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    string_categories: {
      category: "Apple"
      category: "Banana"
      category: "Cantaloupe"
    }
  }
}

# EncodingConfig 3 is Basic RAPPOR with integer candidates (non-stochastic).
element {
  customer_id: 1
  project_id: 1
  id: 3
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    int_range_categories: {
      first: 1
      last:  10
    }
  }
}

# EncodingConfig 4 is Basic RAPPOR with INDEX categories (non-stochastic).
element {
  customer_id: 1
  project_id: 1
  id: 4
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    indexed_categories: {
      num_categories: 100
    }
  }
}

# EncodingConfig 5 is NoOp.
element {
  customer_id: 1
  project_id: 1
  id: 5
  no_op_encoding {
  }
}

)";

static const char* kReportConfigText = R"(
# ReportConfig 1 specifies a report with one variable: part 1 of Metric 1.
element {
  customer_id: 1
  project_id: 1
  id: 1
  metric_id: 1
  variable {
    metric_part: "Part1"
  }
  scheduling {
    report_finalization_days: 3
    aggregation_epoch_type: DAY
  }
  export_configs {
    csv {}
    gcs {
      bucket: "bucket.name.1"
    }
  }
}

# ReportConfig 2 specifies a report with 2 variables: Both parts of Metric 2.
element {
  customer_id: 1
  project_id: 1
  id: 2
  metric_id: 2
  report_type: JOINT
  variable {
    metric_part: "Part1"
  }
  variable {
    metric_part: "Part2"
  }
}

# ReportConfig 3 is for metric 3 and gives labels for encoding config 4.
element {
  customer_id: 1
  project_id: 1
  id: 3
  metric_id: 3
  variable {
    metric_part: "Part1"
    index_labels {
      labels {
         key: 0
         value: "Event A"
      }
      labels {
         key: 1
         value: "Event B"
      }
      labels {
         key: 25
         value: "Event Z"
      }
    }
  }
  scheduling {
    # report_finalization_days will default to 0.
    # aggregation_epoch_type will default to DAY.
  }
  export_configs {
    csv {}
    gcs {
      bucket: "bucket.name.3"
    }
  }
}

# ReportConfig 4 specifies a RAW_DUMP report of both variables of Metric 2.
element {
  customer_id: 1
  project_id: 1
  id: 4
  metric_id: 2
  variable {
    metric_part: "Part1"
  }
  variable {
    metric_part: "Part2"
  }
  report_type: RAW_DUMP
  scheduling {
    # report_finalization_days will default to 0.
    # aggregation_epoch_type will default to DAY.
  }
  export_configs {
    csv {}
    gcs {
      bucket: "bucket.name.4"
    }
  }
}

# ReportConfig 5 specifies a RAW_DUMP report of both variables of Metric 2,
# grouped by BOARD_NAME.
element {
  customer_id: 1
  project_id: 1
  id: 5
  metric_id: 2
  variable {
    metric_part: "Part1"
  }
  variable {
    metric_part: "Part2"
  }
  report_type: RAW_DUMP
  scheduling {
    # report_finalization_days will default to 0.
    # aggregation_epoch_type will default to DAY.
  }
  export_configs {
    csv {}
    gcs {
      bucket: "bucket.name.5"
    }
  }
  system_profile_field: [BOARD_NAME]
}

# ReportConfig 6 is for metric 3 and gives labels for encoding config 4, grouped
# by BOARD_NAME.
element {
  customer_id: 1
  project_id: 1
  id: 6
  metric_id: 3
  variable {
    metric_part: "Part1"
    index_labels {
      labels {
         key: 0
         value: "Event A"
      }
      labels {
         key: 1
         value: "Event B"
      }
      labels {
         key: 25
         value: "Event Z"
      }
    }
  }
  scheduling {
    # report_finalization_days will default to 0.
    # aggregation_epoch_type will default to DAY.
  }
  export_configs {
    csv {}
    gcs {
      bucket: "bucket.name.6"
    }
  }
system_profile_field: [BOARD_NAME]
}

)";

// An implementation of grpc::Writer that keeps a copy of each object
// written for later checking.
class TestingQueryReportsResponseWriter
    : public grpc::WriterInterface<QueryReportsResponse> {
 public:
  bool Write(const QueryReportsResponse& response,
             grpc::WriteOptions options) override {
    responses.emplace_back(response);
    return true;
  }

  std::vector<QueryReportsResponse> responses;
};

// An implementation of GcsUploadInterface that saves its parameters and
// returns OK.
struct FakeGcsUploader : public GcsUploadInterface {
  grpc::Status UploadToGCS(const std::string& bucket, const std::string& path,
                           const std::string& mime_type,
                           ReportStream* report_stream) override {
    buckets.push_back(bucket);
    paths.push_back(path);
    mime_types.push_back(mime_type);
    std::string serialized_report(
        std::istreambuf_iterator<char>(*report_stream), {});
    reports.push_back(serialized_report);
    return grpc::Status::OK;
  }

  std::vector<std::string> buckets;
  std::vector<std::string> paths;
  std::vector<std::string> mime_types;
  std::vector<std::string> reports;
};

// ReportMasterServiceAbstractTest is templatized on the parameter
// |StoreFactoryClass| which must be the name of a class that contains the
// following method: static DataStore* NewStore()
// See MemoryStoreFactory in store/memory_store_test_helper.h and
// BigtableStoreEmulatorFactory in store/bigtable_emulator_helper.h.
template <class StoreFactoryClass>
class ReportMasterServiceAbstractTest : public ::testing::Test {
 protected:
  ReportMasterServiceAbstractTest()
      : data_store_(StoreFactoryClass::NewStore()),
        observation_store_(new store::ObservationStore(data_store_)),
        report_store_(new store::ReportStore(data_store_)),
        clock_(new util::IncrementingClock()) {
    clock_->set_time(util::FromUnixSeconds(kFixedTimeSeconds));
    clock_->set_increment(std::chrono::seconds(0));
    report_store_->set_clock(clock_);
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
    auto metric_parse_result =
        config::FromString<RegisteredMetrics>(kMetricConfigText, nullptr);
    EXPECT_EQ(config::kOK, metric_parse_result.second);
    std::shared_ptr<config::MetricRegistry> metric_registry(
        metric_parse_result.first.release());

    // Parse the encoding config string
    auto encoding_parse_result =
        config::FromString<RegisteredEncodings>(kEncodingConfigText, nullptr);
    EXPECT_EQ(config::kOK, encoding_parse_result.second);
    std::shared_ptr<config::EncodingRegistry> encoding_config_registry(
        encoding_parse_result.first.release());

    // Parse the report config string
    auto report_parse_result =
        config::FromString<RegisteredReports>(kReportConfigText, nullptr);
    EXPECT_EQ(config::kOK, report_parse_result.second);
    std::shared_ptr<config::ReportRegistry> report_config_registry(
        report_parse_result.first.release());
    report_config_registry_ = report_config_registry;

    // Make a ProjectContext
    project_.reset(new encoder::ProjectContext(
        kCustomerId, kProjectId,
        std::make_shared<config::ClientConfig>(encoding_config_registry,
                                               metric_registry)));

    // Make an AnalyzerConfig
    std::shared_ptr<config::AnalyzerConfig> analyzer_config(
        new config::AnalyzerConfig(encoding_config_registry, metric_registry,
                                   report_config_registry_));

    // Make an AnalyzerConfigManager
    std::shared_ptr<config::AnalyzerConfigManager> config_manager(
        new config::AnalyzerConfigManager(analyzer_config));
    config_manager_ = config_manager;

    std::shared_ptr<AuthEnforcer> auth_enforcer(new NullEnforcer());

    fake_uploader_.reset(new FakeGcsUploader());
    std::unique_ptr<ReportExporter> report_exporter(
        new ReportExporter(fake_uploader_));

    report_master_service_.reset(new ReportMasterService(
        0, observation_store_, report_store_, config_manager,
        grpc::InsecureServerCredentials(), auth_enforcer,
        std::move(report_exporter)));

    report_master_service_->StartWorkerThread();
  }

  void WaitUntilIdle() { report_master_service_->WaitUntilIdle(); }

  // Makes an Observation with one string part and one int part, using the
  // two given values and the two given encodings for the given metric.
  std::unique_ptr<Observation> MakeObservation(std::string part1_value,
                                               int part2_value,
                                               uint32_t metric_id,
                                               uint32_t encoding_config_id1,
                                               uint32_t encoding_config_id2) {
    // Construct a new Encoder with a new client secret.
    encoder::Encoder encoder(project_,
                             encoder::ClientSecret::GenerateNewSecret());
    // Set a static current time so we know we have a static day_index.
    encoder.set_current_time(kSomeTimestamp);

    // Construct the two-part value to add.
    encoder::Encoder::Value value;
    value.AddStringPart(encoding_config_id1, kPartName1, part1_value);
    value.AddIntPart(encoding_config_id2, kPartName2, part2_value);

    // Encode an observation.
    encoder::Encoder::Result result = encoder.Encode(metric_id, value);
    EXPECT_EQ(encoder::Encoder::kOK, result.status);
    EXPECT_TRUE(result.observation.get() != nullptr);
    EXPECT_EQ(2, result.observation->parts_size());
    return std::move(result.observation);
  }

  // Makes an Observation with one INDEX value for the given metric and
  // encoding.
  std::unique_ptr<Observation> MakeIndexObservation(
      uint32_t index, uint32_t metric_id, uint32_t encoding_config_id) {
    // Construct a new Encoder with a new client secret.
    encoder::Encoder encoder(project_,
                             encoder::ClientSecret::GenerateNewSecret());
    // Set a static current time so we know we have a static day_index.
    encoder.set_current_time(kSomeTimestamp);

    auto result = encoder.EncodeIndex(metric_id, encoding_config_id, index);

    EXPECT_EQ(encoder::Encoder::kOK, result.status);
    CHECK_EQ(encoder::Encoder::kOK, result.status);
    EXPECT_TRUE(result.observation.get() != nullptr);
    CHECK(result.observation.get() != nullptr);
    EXPECT_EQ(1, result.observation->parts_size());
    return std::move(result.observation);
  }

  // Adds to the ObservationStore |num_clients| two-part observations that each
  // encode the given two values using the given metric and the
  // given two encodings. Each Observation is generated as if from a different
  // client.
  void AddObservations(std::string part1_value, int part2_value,
                       uint32_t metric_id, uint32_t encoding_config_id1,
                       uint32_t encoding_config_id2, int num_clients,
                       uint32_t day_index) {
    std::vector<Observation> observations;
    for (int i = 0; i < num_clients; i++) {
      observations.emplace_back(*MakeObservation(part1_value, part2_value,
                                                 metric_id, encoding_config_id1,
                                                 encoding_config_id2));
    }
    ObservationMetadata metadata;
    metadata.set_customer_id(kCustomerId);
    metadata.set_project_id(kProjectId);
    metadata.set_metric_id(metric_id);
    metadata.set_day_index(day_index);
    metadata.mutable_system_profile()->set_board_name("DummyBoard");
    EXPECT_EQ(store::kOK,
              observation_store_->AddObservationBatch(metadata, observations));
  }

  // Adds to the ObservationStore |num_clients| Observations with one INDEX
  // value using the given metric and encoding. Each Observation is generated
  // as if from a different client.
  void AddIndexObservations(uint32_t index, uint32_t metric_id,
                            uint32_t encoding_config_id1, int num_clients,
                            uint32_t day_index) {
    std::vector<Observation> observations;
    for (int i = 0; i < num_clients; i++) {
      observations.emplace_back(
          *MakeIndexObservation(index, metric_id, encoding_config_id1));
    }
    ObservationMetadata metadata;
    metadata.set_customer_id(kCustomerId);
    metadata.set_project_id(kProjectId);
    metadata.set_metric_id(metric_id);
    metadata.set_day_index(day_index);
    metadata.mutable_system_profile()->set_board_name("DummyBoard");
    EXPECT_EQ(store::kOK,
              observation_store_->AddObservationBatch(metadata, observations));
  }

  // Invokes ReportMaster::GetReport() and checks the returned ReportMetadata.
  void GetReportAndCheck(const std::string& report_id,
                         uint32_t expected_report_config_id, bool expect_part1,
                         bool expect_part2, bool check_completed,
                         Report* report_out) {
    GetReportRequest get_request;
    get_request.set_report_id(report_id);
    auto status =
        report_master_service_->GetReport(nullptr, &get_request, report_out);
    EXPECT_TRUE(status.ok()) << "error_code=" << status.error_code()
                             << " error_message=" << status.error_message();

    // Check report metadata
    CheckMetadata(report_id, expected_report_config_id, expect_part1,
                  expect_part2, check_completed, kFixedTimeSeconds,
                  report_out->metadata());
  }

  // Checks a ReportMetadata returned from GetReport or QueryReports.
  void CheckMetadata(const std::string& report_id,
                     uint32_t expected_report_config_id, bool expect_part1,
                     bool expect_part2, bool check_completed,
                     int64_t expected_current_time_seconds,
                     const ReportMetadata& metadata) {
    EXPECT_EQ(report_id, metadata.report_id());
    EXPECT_EQ(kCustomerId, metadata.customer_id());
    EXPECT_EQ(kProjectId, metadata.project_id());
    EXPECT_EQ(expected_report_config_id, metadata.report_config_id());
    EXPECT_EQ(expected_current_time_seconds,
              metadata.creation_time().seconds());

    bool expect_joint_report = expect_part1 && expect_part2;

    if (check_completed) {
      // Currently JOINT reports are not implemented so we expect the report
      // to have failed.
      ReportState expected_completion_state =
          (expect_joint_report ? TERMINATED : COMPLETED_SUCCESSFULLY);
      EXPECT_EQ(expected_completion_state, metadata.state());
      EXPECT_TRUE(metadata.start_time().seconds() >=
                  metadata.creation_time().seconds());
      EXPECT_TRUE(metadata.finish_time().seconds() >=
                  metadata.start_time().seconds());
    }

    EXPECT_EQ(kDayIndex, metadata.first_day_index());
    EXPECT_EQ(kDayIndex, metadata.last_day_index());

    // Check the metric parts.
    int expected_num_parts = (expect_joint_report ? 2 : 1);
    ASSERT_EQ(expected_num_parts, metadata.metric_parts_size());
    if (expect_part1) {
      EXPECT_EQ("Part1", metadata.metric_parts(0));
    } else if (!expect_part1 && expect_part2) {
      EXPECT_EQ("Part2", metadata.metric_parts(0));
    }
    if (expect_joint_report) {
      EXPECT_EQ("Part2", metadata.metric_parts(1));
    }

    // Check the associated_report_ids.
    if (expect_joint_report) {
      EXPECT_EQ(2, metadata.associated_report_ids_size());
    } else {
      EXPECT_EQ(0, metadata.associated_report_ids_size());
    }

    EXPECT_TRUE(metadata.one_off());

    // Check info_messages.
    if (check_completed && expect_joint_report) {
      ASSERT_NE(0, metadata.info_messages_size());
      EXPECT_NE(std::string::npos,
                metadata.info_messages(0).message().find(
                    "Report type JOINT is not yet implemented"));
      EXPECT_EQ(expected_current_time_seconds,
                metadata.info_messages(0).timestamp().seconds());
    }
  }

  // Invokes GetReportAndCheck() on the given joint report. Then extracts the
  // IDs of the two marginal reports and invokes GetReportAndCheck() on those
  // also.
  void CheckJointReportAndTwoMarginals(std::string report_id_joint,
                                       uint32_t expected_report_config_id,
                                       bool check_completed,
                                       Report* first_marginal_report_out,
                                       Report* second_marginal_report_out) {
    // Get and check the metadata of the joint report.
    Report joint_report;
    bool expect_part1 = true;
    bool expect_part2 = true;
    this->GetReportAndCheck(report_id_joint, expected_report_config_id,
                            expect_part1, expect_part2, check_completed,
                            &joint_report);
    // Currently joint reports are not yet implemented so there should be
    // no report rows.
    EXPECT_FALSE(joint_report.has_rows());

    // Extract the IDs of the two marginal reports.
    std::string report_id_11 = joint_report.metadata().associated_report_ids(0);
    std::string report_id_12 = joint_report.metadata().associated_report_ids(1);

    // Get and check the metadata of the first marginal report.
    expect_part1 = true;
    expect_part2 = false;
    this->GetReportAndCheck(report_id_11, expected_report_config_id,
                            expect_part1, expect_part2, check_completed,
                            first_marginal_report_out);
    if (check_completed) {
      EXPECT_TRUE(first_marginal_report_out->has_rows());
    }

    // Get and check the metadata of the second marginal report.
    expect_part1 = false;
    expect_part2 = true;
    this->GetReportAndCheck(report_id_12, expected_report_config_id,
                            expect_part1, expect_part2, check_completed,
                            second_marginal_report_out);
    if (check_completed) {
      EXPECT_TRUE(second_marginal_report_out->has_rows());
    }
  }

  // Returns a version of the given |report| with the rows sorted.
  // |report| is expected to be a serialized report that consist of a series
  // of report rows separated by newlines. The first row of the report should be
  // the header row. This method will build a new serialized report that
  // has the same header row but with the remaining rows sorted.
  std::string WithSortedRows(std::string report) {
    std::istringstream istream(report);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(istream, line)) {
      lines.push_back(line);
    }
    auto begin = lines.begin();
    begin++;
    std::sort(begin, lines.end());
    std::ostringstream ostream;
    for (const auto& line : lines) {
      ostream << line;
    }
    return ostream.str();
  }

  void CheckRawDumpReport(const std::string& expected_report,
                          const std::string& report) {
    EXPECT_EQ(WithSortedRows(expected_report), WithSortedRows(report));
  }

  // Invokes ReportMaster::QueryReportsInternal() using our fixed customer and
  // project and the given report_config_id and time interval. The responses
  // will be written to the given |response_writer|. Returns true for success
  // or false for failure.
  bool QueryReports(uint32_t report_config_id, uint64_t first_time_seconds,
                    uint64_t limit_time_seconds,
                    TestingQueryReportsResponseWriter* response_writer) {
    QueryReportsRequest request;
    request.set_customer_id(kCustomerId);
    request.set_project_id(kProjectId);
    request.set_report_config_id(report_config_id);
    request.mutable_first_timestamp()->set_seconds(first_time_seconds);
    request.mutable_limit_timestamp()->set_seconds(limit_time_seconds);
    auto status = report_master_service_->QueryReportsInternal(
        nullptr, &request, response_writer);
    EXPECT_TRUE(status.ok()) << "error_code=" << status.error_code()
                             << " error_message=" << status.error_message();
    return status.ok();
  }

  void set_current_time_seconds(int64_t current_time_seconds) {
    clock_->set_time(util::FromUnixSeconds(current_time_seconds));
  }

  // Writes Metadata directly into the ReportStore simulating the case that
  // StarReport() was invoked many times to form |num_reports| different
  // instances of the report with the given |report_config_id|.
  //
  // The creation time and start time for report i will be
  // kFixedTimeSeconds + i.
  //
  // The implementation of this function breaks several layers of abstraction
  // and writes directly into the underlying ReporStore. This is a convenient
  // way to efficiently set up the ReportMetadata table in order test the
  // QueryReports function. If we were to use the gRPC API to accomplish this
  // it would require many RPC roundtrips which would take a long time.
  // There is no reason for the gRPC API to support an efficient implementation
  // of this function as it is not useful outside of a test.
  //
  // The vector of string report IDs from the gRPC API are returned so that
  // they may be used to query in the gRPC API.
  std::vector<std::string> WriteManyNewReports(uint64_t report_config_id,
                                               size_t num_reports) {
    ReportId report_id;
    report_id.set_customer_id(kCustomerId);
    report_id.set_project_id(kProjectId);
    report_id.set_report_config_id(report_config_id);
    std::vector<ReportId> report_ids(num_reports, report_id);

    ReportMetadataLite metadata;
    metadata.set_state(IN_PROGRESS);
    metadata.set_first_day_index(kDayIndex);
    metadata.set_last_day_index(kDayIndex);
    metadata.set_one_off(true);
    metadata.add_variable_indices(0);
    std::vector<ReportMetadataLite> report_metadata(num_reports, metadata);

    std::vector<std::string> string_report_ids(num_reports);
    for (size_t i = 0; i < num_reports; i++) {
      report_ids[i].set_creation_time_seconds(kFixedTimeSeconds + i);
      report_ids[i].set_instance_id(i);
      report_metadata[i].set_start_time_seconds(kFixedTimeSeconds + i);
      string_report_ids[i] =
          report_master_service_->MakeStringReportId(report_ids[i]);
    }
    // We write all the reports with a single RPC.
    store::ReportStoreTestUtils test_utils(report_store_);
    EXPECT_EQ(store::kOK,
              test_utils.WriteBulkMetadata(report_ids, report_metadata));
    return string_report_ids;
  }

  // Given a file_path of the form
  //    "1_1_1/report_1_1_1_20090314.csv"
  // returns the day_index corresponding to the calendar date encoded by the
  // last 8 digits (20090314 in the example above), or returns 0 if |file_path|
  // does not have the expected form.
  uint32_t ExtractDayIndexFromPath(const std::string& file_path,
                                   uint32_t report_config_id) {
    std::ostringstream stream;
    stream << kCustomerId << "_" << kProjectId << "_" << report_config_id
           << "/report_" << kCustomerId << "_" << kProjectId << "_"
           << report_config_id << "_";
    std::string expected_prefix = stream.str();
    if (file_path.find(expected_prefix) != 0) {
      ADD_FAILURE() << "file_path=" << file_path
                    << " expected_prefix=" << expected_prefix;
      return 0;
    }
    size_t left_index = expected_prefix.size();
    size_t index_of_dot = left_index + 8;
    EXPECT_EQ(index_of_dot + 4, file_path.size()) << file_path;
    EXPECT_EQ(".csv", file_path.substr(index_of_dot));
    CalendarDate cd;
    cd.year = std::stoi(file_path.substr(left_index, 4));
    cd.month = std::stoi(file_path.substr(left_index + 4, 2));
    cd.day_of_month = std::stoi(file_path.substr(left_index + 6, 2));
    return CalendarDateToDayIndex(cd);
  }

  // Replaces all occurrences of |date_token| within |report| with the string
  // representation of the date given by |day_index|.
  std::string ReplaceDateTokens(const std::string& report,
                                const std::string& date_token,
                                uint32_t day_index) {
    util::CalendarDate cd = util::DayIndexToCalendarDate(day_index);
    std::ostringstream stream;
    stream << cd.year << "-" << cd.month << "-" << cd.day_of_month;
    std::string date_string = stream.str();
    std::string report_out = report;
    size_t index = report_out.find(date_token);
    while (index != std::string::npos) {
      report_out = report_out.replace(index, date_token.size(), date_string);
      index = report_out.find(date_token);
    }
    return report_out;
  }

  std::shared_ptr<encoder::ProjectContext> project_;
  std::shared_ptr<store::DataStore> data_store_;
  std::shared_ptr<store::ObservationStore> observation_store_;
  std::shared_ptr<store::ReportStore> report_store_;
  std::unique_ptr<ReportMasterService> report_master_service_;
  std::shared_ptr<util::IncrementingClock> clock_;
  std::shared_ptr<config::ReportRegistry> report_config_registry_;
  std::shared_ptr<config::AnalyzerConfigManager> config_manager_;
  std::shared_ptr<FakeGcsUploader> fake_uploader_;
};

TYPED_TEST_CASE_P(ReportMasterServiceAbstractTest);

// Adds observations to the ObservationStore and then uses the ReportMaster
// to run two reports for our two registered ReportConfigs. Checks the
// results. From the ReportMaster API we test the methods StartReport and
// GetReport.
TYPED_TEST_P(ReportMasterServiceAbstractTest, StartAndGetReports) {
  // Add some observations for metric 1. We use Basic RAPPOR for
  // both parts. We add 20 observations of the pair ("Apple", 10).
  // Our report will only analyze part 1; part 2 will be ignored. We have
  // set the RAPPOR parameters p and q so there is no randomness. We therefore
  // will expect the report to produce the following results:
  // ("Apple", 20), ("Banana", 0), ("Cantaloupe", 0).
  this->AddObservations("Apple", 10, kMetricId1,
                        kBasicRapporStringEncodingConfigId,
                        kBasicRapporIntEncodingConfigId, 20, kDayIndex);

  // Add some observations for metric 2. We use Forculus for part 1
  // and BasicRappor for part 2. For the Forculus part there will be
  // 20 observations of "Apple", 19 observations of "Banana", and 21
  // observations of "Cantaloupe" so we expect to see "Apple" and "Cantaloupe"
  // in the report but not "Banana". For the Basic RAPPOR part there will
  // be 20 observations of |10|, 19 observations of |9|, and 21 observations
  // of |8|. Joint reports are not implemented yet so we will only be checking
  // the results of the two marginal reports.
  this->AddObservations("Apple", 10, kMetricId2, kForculusEncodingConfigId,
                        kBasicRapporIntEncodingConfigId, kForculusThreshold,
                        kDayIndex);
  this->AddObservations("Banana", 9, kMetricId2, kForculusEncodingConfigId,
                        kBasicRapporIntEncodingConfigId, kForculusThreshold - 1,
                        kDayIndex);
  this->AddObservations("Cantaloupe", 8, kMetricId2, kForculusEncodingConfigId,
                        kBasicRapporIntEncodingConfigId, kForculusThreshold + 1,
                        kDayIndex);

  // Start the first report. This is a one-variable report of
  // part 1 of metric 1.
  StartReportRequest start_request;
  start_request.set_customer_id(kCustomerId);
  start_request.set_project_id(kProjectId);
  start_request.set_report_config_id(kReportConfigId1);
  start_request.set_first_day_index(kDayIndex);
  start_request.set_last_day_index(kDayIndex);
  StartReportResponse start_response;
  auto status = this->report_master_service_->StartReport(
      nullptr, &start_request, &start_response);
  EXPECT_TRUE(status.ok()) << "error_code=" << status.error_code()
                           << " error_message=" << status.error_message();
  // Capture the ID for report 1.
  std::string report_id1 = start_response.report_id();
  EXPECT_FALSE(report_id1.empty());

  // Start the second report. This is a joint two-variable report of metric 2.
  // The two marginal reports will be automatically started also but the
  // returned report_id will be for the joint report. Since joint reports are
  // not implemented yet we will only be checking the results of the two
  // marginal reports but we will be checking the metadata of the joint
  // report too.
  start_request.set_report_config_id(kReportConfigId2);
  status = this->report_master_service_->StartReport(nullptr, &start_request,
                                                     &start_response);
  EXPECT_TRUE(status.ok()) << "error_code=" << status.error_code()
                           << " error_message=" << status.error_message();
  // Capture the ID for report 2. This is the ID of the joint report.
  std::string report_id2 = start_response.report_id();
  EXPECT_FALSE(report_id2.empty());

  // Check the meta-data of the first report. It should include part 1 and
  // not part 2.
  Report report1;
  bool expect_part1 = true;
  bool expect_part2 = false;
  // The report is generated asynchronously and we don't know that it is
  // done yet so don't check that it is completed.
  bool check_completed = false;
  {
    SCOPED_TRACE("");
    this->GetReportAndCheck(report_id1, kReportConfigId1, expect_part1,
                            expect_part2, check_completed, &report1);
  }

  // Check the meta-data of the second report. We should find a joint report
  // and two associated marginal reports and we check the metadata of all three.
  // The joint report will have meta-data only because joint reports are not
  // implemented yet. But the two marginals will be returned to us so we can
  // check them. (But not yet because we don't know that the report generation
  // is completed yet.)
  Report first_marginal_report;
  Report second_marginal_report;
  {
    SCOPED_TRACE("");
    this->CheckJointReportAndTwoMarginals(
        report_id2, kReportConfigId2, check_completed, &first_marginal_report,
        &second_marginal_report);
  }

  // Wait until the report generation for all reports completes.
  this->WaitUntilIdle();

  // Check the reports again but this time check that they are completed
  // and then check the actual contents of the report rows.
  check_completed = true;
  {
    SCOPED_TRACE("");
    this->GetReportAndCheck(report_id1, kReportConfigId1, expect_part1,
                            expect_part2, check_completed, &report1);
  }

  // Check the rows of report 1.
  // Recall that when adding observations to metric 1 above we used Basic
  // RAPPOR with no randomness so we expect to see the results
  // ("Apple", 20), ("Banana", 0), ("Cantaloupe", 0).
  ASSERT_EQ(3, report1.rows().rows_size());
  std::map<std::string, int> report1_results;
  for (int i = 0; i < 3; i++) {
    report1_results[report1.rows().rows(i).histogram().value().string_value()] =
        report1.rows().rows(i).histogram().count_estimate();
  }
  ASSERT_EQ(3u, report1_results.size());
  EXPECT_EQ(20, report1_results["Apple"]);
  EXPECT_EQ(0, report1_results["Banana"]);
  EXPECT_EQ(0, report1_results["Cantaloupe"]);

  // Check report 2 again including its associated marginal reports, this
  // time checking that they are complete.
  {
    SCOPED_TRACE("");
    this->CheckJointReportAndTwoMarginals(
        report_id2, kReportConfigId2, check_completed, &first_marginal_report,
        &second_marginal_report);
  }

  // Check the rows of the first marginal of report 2. Recall that when adding
  // rows to part 1 of metric 2 above we used Forculus and we expect to see the
  // results ("Apple", 20), ("Cantaloupe", 21) and not to see "Banana" because
  // it should not have been decrypted.
  ASSERT_EQ(2, first_marginal_report.rows().rows_size());
  std::map<std::string, int> first_marginal_results;
  for (int i = 0; i < 2; i++) {
    first_marginal_results[first_marginal_report.rows()
                               .rows(i)
                               .histogram()
                               .value()
                               .string_value()] =
        first_marginal_report.rows().rows(i).histogram().count_estimate();
  }
  ASSERT_EQ(2u, first_marginal_results.size());
  EXPECT_EQ(20, first_marginal_results["Apple"]);
  EXPECT_EQ(21, first_marginal_results["Cantaloupe"]);

  // Check the rows of the second marginal of report 2. Recall that when adding
  // rows to part 2 of metric 2 above we used Basic RAPPOR with no randomness
  // so we expect to see the following results:
  // (a) A count of 0 for the numbers 1, 2, 3, 4, 5, 6, 7
  // (b) (8, 21), (9, 19), (10, 20)
  ASSERT_EQ(10, second_marginal_report.rows().rows_size());
  std::map<int, int> second_marginal_results;
  for (int i = 0; i < 10; i++) {
    second_marginal_results[second_marginal_report.rows()
                                .rows(i)
                                .histogram()
                                .value()
                                .int_value()] =
        second_marginal_report.rows().rows(i).histogram().count_estimate();
  }
  ASSERT_EQ(10u, second_marginal_results.size());
  for (int i = 1; i <= 7; i++) {
    EXPECT_EQ(0, second_marginal_results[i]);
  }
  EXPECT_EQ(21, second_marginal_results[8]);
  EXPECT_EQ(19, second_marginal_results[9]);
  EXPECT_EQ(20, second_marginal_results[10]);

  // Expect that no exporting was perofrmed.
  EXPECT_TRUE(this->fake_uploader_->reports.empty());
}

// Tests Cobalt analyzer end-to-end using a (metric, encoding, report) trio
// in which Observations use the INDEX data type and the report config
// specifies human-readable labels for some of the indices.
TYPED_TEST_P(ReportMasterServiceAbstractTest, E2EWithIndexLabels) {
  for (int index = 0; index < 50; index++) {
    // Add |index| + 1 observations of |index|.
    this->AddIndexObservations(index, kMetricId3,
                               kBasicRapporIndexEncodingConfigId, index + 1,
                               kDayIndex);
  }

  // Start the report.
  StartReportRequest start_request;
  start_request.set_customer_id(kCustomerId);
  start_request.set_project_id(kProjectId);
  start_request.set_report_config_id(kReportConfigId3);
  start_request.set_first_day_index(kDayIndex);
  start_request.set_last_day_index(kDayIndex);
  StartReportResponse start_response;
  auto status = this->report_master_service_->StartReport(
      nullptr, &start_request, &start_response);
  EXPECT_TRUE(status.ok()) << "error_code=" << status.error_code()
                           << " error_message=" << status.error_message();
  // Capture the report ID.
  std::string report_id = start_response.report_id();
  EXPECT_FALSE(report_id.empty());

  // Wait until the report generation completes.
  this->WaitUntilIdle();

  Report report;
  {
    // Fetch the report and check the metadata.
    SCOPED_TRACE("");
    bool check_completed = true;
    bool expect_part1 = true;
    bool expect_part2 = false;
    this->GetReportAndCheck(report_id, kReportConfigId3, expect_part1,
                            expect_part2, check_completed, &report);
  }
  // Check the rows of the report including the labels.
  ASSERT_EQ(100, report.rows().rows_size());
  for (size_t i = 0; i < 100; i++) {
    auto index = report.rows().rows(i).histogram().value().index_value();
    auto count = report.rows().rows(i).histogram().count_estimate();
    auto label = report.rows().rows(i).histogram().label();
    auto expected_count = (index < 50 ? index + 1 : 0);
    EXPECT_EQ(expected_count, count)
        << "i=" << i << ", index=" << index << ", count=" << count;
    switch (index) {
      case 0:
        EXPECT_EQ("Event A", label);
        break;

      case 1:
        EXPECT_EQ("Event B", label);
        break;

      case 25:
        EXPECT_EQ("Event Z", label);
        break;

      default:
        EXPECT_EQ("", label);
    }
  }

  // Expect that no exporting was perofrmed.
  EXPECT_TRUE(this->fake_uploader_->reports.empty());
}

// Tests the method ReportMaster::QueryReports. We write into the ReportStore
// many instance of ReportConfig 1 and then invoke QueryReports() and check the
// results.
TYPED_TEST_P(ReportMasterServiceAbstractTest, QueryReportsTest) {
  // Write Metadata into the ReportStore for 210 reports associated with
  // ReporcConfig 1  with creation_times that start at kFixedTimeSeconds and
  // increment by 1 second for each report.
  std::vector<std::string> report_ids =
      this->WriteManyNewReports(kReportConfigId1, 210);

  // Now invoke QueryReports. We specify a time window that will omit the
  // first three and the last 3 reports. So there should be 204 reports
  // returned.
  TestingQueryReportsResponseWriter response_writer;
  ASSERT_TRUE(this->QueryReports(kReportConfigId1, kFixedTimeSeconds + 3,
                                 kFixedTimeSeconds + 207, &response_writer));

  // Since we know that reports are returned in batches of 100 we expect there
  // to be 3 batches: Two batches of size 100 and one batch of size 4.
  EXPECT_EQ(3u, response_writer.responses.size());

  // Check the first batch.
  bool expect_part1 = true;
  bool expect_part2 = false;
  // We don't know that the reports are completed yet.
  bool check_completed = false;
  EXPECT_EQ(100, response_writer.responses[0].reports_size());
  for (int i = 0; i < 100; i++) {
    this->CheckMetadata(report_ids[i + 3], kReportConfigId1, expect_part1,
                        expect_part2, check_completed,
                        kFixedTimeSeconds + 3 + i,
                        response_writer.responses[0].reports(i));
  }

  // Check the second batch.
  EXPECT_EQ(100, response_writer.responses[1].reports_size());
  for (int i = 0; i < 100; i++) {
    this->CheckMetadata(report_ids[i + 103], kReportConfigId1, expect_part1,
                        expect_part2, check_completed,
                        kFixedTimeSeconds + 103 + i,
                        response_writer.responses[1].reports(i));
  }

  // Check the third batch.
  EXPECT_EQ(4, response_writer.responses[2].reports_size());
  for (int i = 0; i < 4; i++) {
    this->CheckMetadata(report_ids[i + 203], kReportConfigId1, expect_part1,
                        expect_part2, check_completed,
                        kFixedTimeSeconds + 203 + i,
                        response_writer.responses[2].reports(i));
  }
}

// Tests the interaction of the ReportScheduler with the rest of the
// ReportMaster pipeline, including report exporting. We simulate 10 days of
// activity of the ReportScheduler and then we check the exported reports.
TYPED_TEST_P(ReportMasterServiceAbstractTest, EnableReportScheduling) {
  // First we populate the Observation Store with Observations so that the
  // reports will have something to analyzer. This part of the simulation is
  // unrealistic because we are going to add Observations for all of the days
  // at the beginning of the test instead of allowing the Observations to arrive
  // interspersed with the report generation. We add observations for metrics 1,
  // 2 and 3 for each day in the interval [kDayIndex - 30, kDayIndex + 15].
  for (uint32_t day_index = kDayIndex - 30; day_index < kDayIndex + 15;
       day_index++) {
    this->AddObservations("Apple", 1, kMetricId1,
                          kBasicRapporStringEncodingConfigId,
                          kBasicRapporIntEncodingConfigId, 20, day_index);
    this->AddObservations("a", 1, kMetricId2, kNoOpEncodingConfigId,
                          kNoOpEncodingConfigId, 1, day_index);
    this->AddObservations("b", 2, kMetricId2, kNoOpEncodingConfigId,
                          kNoOpEncodingConfigId, 2, day_index);
    this->AddObservations("c", 3, kMetricId2, kNoOpEncodingConfigId,
                          kNoOpEncodingConfigId, 3, day_index);
    this->AddIndexObservations(0, kMetricId3, kBasicRapporIndexEncodingConfigId,
                               5, day_index);
  }

  /// We construct a ReportScheduler that uses our ReportMasterService as
  // its ReportStarter.
  std::shared_ptr<ReportStarter> report_starter(
      new ReportStarter(this->report_master_service_.get()));
  std::unique_ptr<ReportScheduler> report_scheduler(
      new ReportScheduler(this->config_manager_, this->report_store_,
                          report_starter, std::chrono::milliseconds(1)));

  // We arrange that the ReportScheduler loops every 1 ms and that each ms
  // it simulates 4 hours of time passing.
  std::shared_ptr<util::IncrementingClock> clock(new util::IncrementingClock());
  std::chrono::system_clock::time_point start_time =
      util::FromUnixSeconds(kSomeTimestamp);
  clock->set_time(start_time);
  clock->set_increment(std::chrono::seconds(60 * 60 * 4));
  report_scheduler->SetClockForTesting(clock);

  // We arrange for the scheduler thread to notify this thread after 10
  // days of simulated time has occurred.
  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  std::chrono::system_clock::time_point stop_time =
      start_time + std::chrono::hours(24 * 10);

  clock->set_callback(
      [&cv, &mu, &done,
       stop_time](std::chrono::system_clock::time_point simulated_time) {
        if (simulated_time > stop_time) {
          std::lock_guard<std::mutex> lock(mu);
          done = true;
          cv.notify_all();
        }
      });

  // Start the scheduler thread.
  report_scheduler->Start();

  // We wait for the scheduler thread to notify this thread that 10 days of
  // simulated time has occurred.
  {
    std::unique_lock<std::mutex> lock(mu);
    cv.wait(lock, [&done] { return done; });
  }

  // We delete the ReportScheduler, which stops the scheduler thread.
  report_scheduler.reset();

  // Now we wait for the ReportExecutor's worker thread to finish generating
  // all of the reports.
  this->WaitUntilIdle();

  // Now we check the exported reports.

  static const char kDateToken[] = "<DATE>";
  static const char kExpectedReport1[] = R"(date,Part1,count,err
<DATE>,"Apple",20.000,0
<DATE>,"Banana",0,0
<DATE>,"Cantaloupe",0,0
)";

  static const char kExpectedReport3[] = R"(date,Part1,count,err
<DATE>,"Event A",5.000,0
<DATE>,"Event B",0,0
<DATE>,"Event Z",0,0
)";

  static const char kExpectedReport4[] = R"(date,Part1,Part2
<DATE>,"a",1
<DATE>,"b",2
<DATE>,"b",2
<DATE>,"c",3
<DATE>,"c",3
<DATE>,"c",3
)";

  static const char kExpectedReport5[] = R"(date,Part1,Part2,Board_Name
<DATE>,"a",1,"DummyBoard"
<DATE>,"b",2,"DummyBoard"
<DATE>,"b",2,"DummyBoard"
<DATE>,"c",3,"DummyBoard"
<DATE>,"c",3,"DummyBoard"
<DATE>,"c",3,"DummyBoard"
)";

  static const char kExpectedReport6[] = R"(date,Part1,Board_Name,count,err
<DATE>,"Event A","DummyBoard",5.000,0
<DATE>,"Event B","DummyBoard",0,0
<DATE>,"Event Z","DummyBoard",0,0
)";

  // The keys to these maps are day indices and the values are the number of
  // reports found for that day.
  std::map<uint32_t, size_t> day_counts_for_report_1;
  std::map<uint32_t, size_t> day_counts_for_report_3;
  std::map<uint32_t, size_t> day_counts_for_report_4;
  std::map<uint32_t, size_t> day_counts_for_report_5;
  std::map<uint32_t, size_t> day_counts_for_report_6;

  size_t num_reports = this->fake_uploader_->buckets.size();
  ASSERT_TRUE(num_reports >= 120) << num_reports;
  ASSERT_EQ(num_reports, this->fake_uploader_->paths.size());
  ASSERT_EQ(num_reports, this->fake_uploader_->reports.size());
  ASSERT_EQ(num_reports, this->fake_uploader_->mime_types.size());

  for (size_t i = 0; i < num_reports; i++) {
    uint32_t report_config_id;
    EXPECT_EQ("text/csv", this->fake_uploader_->mime_types[i]);
    std::string expected_report;
    std::map<uint32_t, size_t>* day_counts;
    if (this->fake_uploader_->buckets[i] == "bucket.name.1") {
      report_config_id = kReportConfigId1;
      day_counts = &day_counts_for_report_1;
      expected_report = std::string(kExpectedReport1);
    } else if (this->fake_uploader_->buckets[i] == "bucket.name.3") {
      report_config_id = kReportConfigId3;
      day_counts = &day_counts_for_report_3;
      expected_report = std::string(kExpectedReport3);
    } else if (this->fake_uploader_->buckets[i] == "bucket.name.4") {
      report_config_id = kReportConfigId4;
      day_counts = &day_counts_for_report_4;
      expected_report = std::string(kExpectedReport4);
    } else if (this->fake_uploader_->buckets[i] == "bucket.name.5") {
      report_config_id = kReportConfigId5;
      day_counts = &day_counts_for_report_5;
      expected_report = std::string(kExpectedReport5);
    } else if (this->fake_uploader_->buckets[i] == "bucket.name.6") {
      report_config_id = kReportConfigId6;
      day_counts = &day_counts_for_report_6;
      expected_report = std::string(kExpectedReport6);
    } else {
      FAIL() << this->fake_uploader_->buckets[i];
    }
    uint32_t day_index = this->ExtractDayIndexFromPath(
        this->fake_uploader_->paths[i], report_config_id);
    ASSERT_GE(day_index, kDayIndex - 30);
    ASSERT_LE(day_index, kDayIndex + 100);
    expected_report =
        this->ReplaceDateTokens(expected_report, kDateToken, day_index);
    if (report_config_id == kReportConfigId4 ||
        report_config_id == kReportConfigId5) {
      this->CheckRawDumpReport(expected_report,
                               this->fake_uploader_->reports[i]);
    } else {
      EXPECT_EQ(expected_report, this->fake_uploader_->reports[i]);
    }

    (*day_counts)[day_index]++;
  }

  for (uint32_t day_index = kDayIndex - 30; day_index < kDayIndex - 3;
       day_index++) {
    EXPECT_EQ(1u, day_counts_for_report_1[day_index]) << day_index;
    EXPECT_EQ(1u, day_counts_for_report_3[day_index]) << day_index;
    EXPECT_EQ(1u, day_counts_for_report_4[day_index]) << day_index;
  }
  for (uint32_t day_index = kDayIndex - 2; day_index <= kDayIndex + 9;
       day_index++) {
    EXPECT_TRUE(day_counts_for_report_1[day_index] >= 1) << day_index;
    EXPECT_EQ(1u, day_counts_for_report_3[day_index]) << day_index;
    EXPECT_EQ(1u, day_counts_for_report_4[day_index]) << day_index;
  }
}  // namespace analyzer

REGISTER_TYPED_TEST_CASE_P(ReportMasterServiceAbstractTest, StartAndGetReports,
                           E2EWithIndexLabels, QueryReportsTest,
                           EnableReportScheduling);

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_MASTER_SERVICE_ABSTRACT_TEST_H_

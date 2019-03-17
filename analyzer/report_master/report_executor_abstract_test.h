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

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_EXECUTOR_ABSTRACT_TEST_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_EXECUTOR_ABSTRACT_TEST_H_

#include "analyzer/report_master/report_executor.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./observation.pb.h"
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

static const uint32_t kCustomerId = 1;
static const uint32_t kProjectId = 1;
static const uint32_t kMetricId1 = 1;
static const uint32_t kMetricId2 = 2;
static const uint32_t kReportConfigId1 = 1;
static const uint32_t kReportConfigId2 = 2;
static const uint32_t kForculusEncodingConfigId = 1;
static const uint32_t kBasicRapporStringEncodingConfigId = 2;
static const uint32_t kBasicRapporIntEncodingConfigId = 3;
static const char kPartName1[] = "Part1";
static const char kPartName2[] = "Part2";
static const size_t kForculusThreshold = 20;

// This unix timestamp corresponds to Friday Dec 2, 2016 in UTC
static const time_t kSomeTimestamp = 1480647356;
// This is the day index for Friday Dec 2, 2016
static const uint32_t kDayIndex = 17137;

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

)";

static const char* kReportConfigText = R"(
# ReportConfig 1 specifies a report of both variables of Metric 1.
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
}

# ReportConfig 2 specifies a report of both variables of Metric 2.
element {
  customer_id: 1
  project_id: 1
  id: 2
  metric_id: 2
  variable {
    metric_part: "Part1"
  }
  variable {
    metric_part: "Part2"
  }
}

)";

// ReportExecutorAbstractTest is templatized on the parameter
// |StoreFactoryClass| which must be the name of a class that contains the
// following method: static DataStore* NewStore()
// See MemoryStoreFactory in store/memory_store_test_helper.h and
// BigtableStoreEmulatorFactory in store/bigtable_emulator_helper.h.
template <class StoreFactoryClass>
class ReportExecutorAbstractTest : public ::testing::Test {
 protected:
  ReportExecutorAbstractTest()
      : data_store_(StoreFactoryClass::NewStore()),
        observation_store_(new store::ObservationStore(data_store_)),
        report_store_(new store::ReportStore(data_store_)) {
    report_id1_.set_customer_id(kCustomerId);
    report_id1_.set_project_id(kProjectId);
    report_id1_.set_report_config_id(kReportConfigId1);
    report_id2_.set_customer_id(kCustomerId);
    report_id2_.set_project_id(kProjectId);
    report_id2_.set_report_config_id(kReportConfigId2);
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

    // Make a ProjectContext
    project_.reset(new encoder::ProjectContext(
        kCustomerId, kProjectId,
        std::make_shared<config::ClientConfig>(encoding_config_registry,
                                               metric_registry)));

    std::shared_ptr<config::AnalyzerConfig> analyzer_config(
        new config::AnalyzerConfig(encoding_config_registry, metric_registry,
                                   report_config_registry));
    std::shared_ptr<config::AnalyzerConfigManager> analyzer_config_manager(
        new config::AnalyzerConfigManager(analyzer_config));

    // Make a ReportGenerator
    std::unique_ptr<ReportGenerator> report_generator(new ReportGenerator(
        analyzer_config_manager, observation_store_, report_store_, nullptr));

    // Make a ReportExecutor
    report_executor_.reset(
        new ReportExecutor(report_store_, std::move(report_generator)));
  }
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

  // Adds to the ObservationStore |num_clients| two-part observations that each
  // encode the given two values using the given metric and the
  // given two encodings. Each Observation is generated as if from a different
  // client.
  void AddObservations(std::string part1_value, int part2_value,
                       uint32_t metric_id, uint32_t encoding_config_id1,
                       uint32_t encoding_config_id2, int num_clients) {
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
    metadata.set_day_index(kDayIndex);
    EXPECT_EQ(store::kOK,
              observation_store_->AddObservationBatch(metadata, observations));
  }

  // Checks that the report with the given ID completed successfully and has
  // the expected number of rows.
  void CheckReport(const ReportId report_id, int expected_num_rows) {
    ReportMetadataLite metadata;
    ReportRows rows;
    ASSERT_EQ(store::kOK, report_store_->GetReport(report_id, &metadata, &rows))
        << "report_id=" << store::ReportStore::ToString(report_id);
    ASSERT_EQ(COMPLETED_SUCCESSFULLY, metadata.state())
        << "report_id=" << store::ReportStore::ToString(report_id);
    EXPECT_EQ(expected_num_rows, rows.rows_size())
        << "report_id=" << store::ReportStore::ToString(report_id);
  }

  ReportId report_id1_, report_id2_;
  std::shared_ptr<encoder::ProjectContext> project_;
  std::shared_ptr<store::DataStore> data_store_;
  std::shared_ptr<store::ObservationStore> observation_store_;
  std::shared_ptr<store::ReportStore> report_store_;
  std::unique_ptr<ReportExecutor> report_executor_;
};

TYPED_TEST_CASE_P(ReportExecutorAbstractTest);

// We load up the ObservationStore with observations for our two metrics.
// Then we start a ReportExecutor and invoke EnqueueReportGeneration() on
// two dependency chains of reports-- one for parts 1 and 2 of ReportConfig 1
// and one for parts 1 and 2 of ReportConfig 2. We expect 4 reports to
// complete successfully and contain the expected number of rows.
TYPED_TEST_P(ReportExecutorAbstractTest, EnqueueReportGeneration) {
  // Add some observations for metric 1. We use Basic RAPPOR for
  // both parts.
  this->AddObservations("Apple", 10, kMetricId1,
                        kBasicRapporStringEncodingConfigId,
                        kBasicRapporIntEncodingConfigId, 20);

  // Add some observations for metric 2. We use Forculus for part 1
  // and BasicRappor for part 2. For the Forculus part there will be
  // 20 observations of "Apple" but only 19 observations of "Banana" so
  // we expect to see only Apple in the report.
  this->AddObservations("Apple", 10, kMetricId2, kForculusEncodingConfigId,
                        kBasicRapporIntEncodingConfigId, kForculusThreshold);
  this->AddObservations("Banana", 10, kMetricId2, kForculusEncodingConfigId,
                        kBasicRapporIntEncodingConfigId,
                        kForculusThreshold - 1);

  // Register the start of report 1, sequence_num 0, variable 0.
  ReportId report_id11 = this->report_id1_;
  this->report_store_->StartNewReport(kDayIndex, kDayIndex, true, "", true,
                                      HISTOGRAM, {0}, &report_id11);

  // Register the creation of report1, sequence_num 1, variable 1.
  ReportId report_id12 = report_id11;
  this->report_store_->CreateDependentReport(1, "", true, HISTOGRAM, {1},
                                             &report_id12);

  // Register the start of report2, sequence_num 0, variable 0.
  ReportId report_id21 = this->report_id2_;
  this->report_store_->StartNewReport(kDayIndex, kDayIndex, true, "", true,
                                      HISTOGRAM, {0}, &report_id21);

  // Register the creation of report2, sequence_num 1, variable 1.
  ReportId report_id22 = report_id21;
  this->report_store_->CreateDependentReport(1, "", true, HISTOGRAM, {1},
                                             &report_id22);

  // Create two dependency chains of reports. We have the variable 1 report
  // depend on the variable 0 report for both report IDs.
  std::vector<ReportId> chain1;
  chain1.push_back(report_id11);
  chain1.push_back(report_id12);

  std::vector<ReportId> chain2;
  chain2.push_back(report_id21);
  chain2.push_back(report_id22);

  // Start the ReportExecutor
  this->report_executor_->Start();

  // Enqueue chain 1.
  auto status =
      this->report_executor_->EnqueueReportGeneration(std::move(chain1));
  EXPECT_TRUE(status.ok()) << status.error_code() << " "
                           << status.error_message();

  // Enqueue chain 2.
  status = this->report_executor_->EnqueueReportGeneration(std::move(chain2));
  EXPECT_TRUE(status.ok()) << status.error_code() << " "
                           << status.error_message();

  // Wait for the processing to stop.
  this->report_executor_->WaitUntilIdle();

  // report_id11 analyzed Part 1 of metric 1 which received
  // Basic RAPPOR string observations with 3 categories.
  this->CheckReport(report_id11, 3);

  // report_id12 analyzed Part 2 of metric 1 which received
  // Basic RAPPOR int observations with 10 categories.
  this->CheckReport(report_id12, 10);

  // report_id21 analyzed Part 1 of metric 1 which received
  // Forculus observations in which there were 20 observations of Apple
  // but only 19 observations of Banana. So there should only be 1 row in
  // the report.
  this->CheckReport(report_id21, 1);

  // report_id22 of report 2 analyzes Part 2 of metric 2 which received
  // Basic RAPPOR int observations with 10 categories.
  this->CheckReport(report_id22, 10);
}

REGISTER_TYPED_TEST_CASE_P(ReportExecutorAbstractTest, EnqueueReportGeneration);

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_EXECUTOR_ABSTRACT_TEST_H_

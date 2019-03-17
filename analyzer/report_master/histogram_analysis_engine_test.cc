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

#include "analyzer/report_master/histogram_analysis_engine.h"

#include <string>
#include <utility>

#include "./observation.pb.h"
#include "config/config_text_parser.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/project_context.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {

using config::AnalyzerConfig;
using config::ClientConfig;
using config::EncodingRegistry;
using config::MetricRegistry;
using config::ReportRegistry;
using encoder::ClientSecret;
using encoder::Encoder;
using encoder::ProjectContext;

namespace {

const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;
const uint32_t kStringMetricId = 1;
const uint32_t kIndexMetricId = 2;
const uint32_t kIntBucketsMetricId = 3;
const uint32_t kForculusEncodingConfigId = 1;
const uint32_t kBasicRapporStringEncodingConfigId = 2;
const uint32_t kBasicRapporIndexEncodingConfigId = 3;
const uint32_t kStringRapporEncodingConfigId = 4;
const uint32_t kNoOpEncodingConfigId = 5;
const uint32_t kStringReportConfigId = 1;
const uint32_t kIndexReportConfigId = 2;
const uint32_t kIntBucketsReportConfigId = 3;
const uint32_t kGroupedStringReportConfigId = 4;
const char kPartName[] = "Part1";
const size_t kForculusThreshold = 20;

// This unix timestamp corresponds to Friday Dec 2, 2016 in UTC
const time_t kSomeTimestamp = 1480647356;
// This is the day index for Friday Dec 2, 2016
const uint32_t kDayIndex = 17137;

const char* kMetricConfigText = R"(
# Metric 1 has one string part.
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
}

# Metric 2 has one INDEX part.
element {
  customer_id: 1
  project_id: 1
  id: 2
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
      data_type: INDEX
    }
  }
}

# Metric 3 has one INT part with linear buckets.
element {
  customer_id: 1
  project_id: 1
  id: 3
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
      data_type: INT
      int_buckets: {
        linear: {
          floor: 0
          num_buckets: 5
          step_size: 10
        }
      }
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

# EncodingConfig 2 is Basic RAPPOR with string categories.
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

# EncodingConfig 3 is Basic RAPPOR with indexed categories.
element {
  customer_id: 1
  project_id: 1
  id: 3
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    indexed_categories: {
      num_categories: 100
    }
  }
}

# EncodingConfig 4 is String RAPPOR with no randomness.
element {
  customer_id: 1
  project_id: 1
  id: 4
  rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    num_bloom_bits: 8
    num_hashes: 2
    num_cohorts: 2
  }
}

# EncodingConfig 5 is the No-Op encoding.
element {
  customer_id: 1
  project_id: 1
  id: 5
  no_op_encoding {
  }
}

)";

const char* kReportConfigText = R"(
element {
  customer_id: 1
  project_id: 1
  id: 1
  metric_id: 1
  variable {
    metric_part: "Part1"
    rappor_candidates {
      candidates: "Apple"
      candidates: "Banana"
      candidates: "Cantaloupe"
    }
  }
}

element {
  customer_id: 1
  project_id: 1
  id: 2
  metric_id: 2
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
         key: 5
         value: "Event F"
      }
      labels {
         key: 25
         value: "Event Z"
      }
    }
  }
}

element {
  customer_id: 1
  project_id: 1
  id: 3
  metric_id: 3
  variable {
    metric_part: "Part1"
  }
}

element {
  customer_id: 1
  project_id: 1
  id: 4
  metric_id: 1
  variable {
    metric_part: "Part1"
    rappor_candidates {
      candidates: "Apple"
      candidates: "Banana"
      candidates: "Cantaloupe"
    }
  }
  system_profile_field: [BOARD_NAME]
}

)";

}  // namespace

class HistogramAnalysisEngineTest : public ::testing::Test {
 protected:
  void Init(uint32_t report_config_id) {
    report_id_.set_customer_id(kCustomerId);
    report_id_.set_project_id(kProjectId);
    report_id_.set_report_config_id(report_config_id);

    // Parse the metric config string
    auto metric_parse_result =
        config::FromString<RegisteredMetrics>(kMetricConfigText, nullptr);
    EXPECT_EQ(config::kOK, metric_parse_result.second);
    std::shared_ptr<MetricRegistry> metric_registry(
        metric_parse_result.first.release());

    // Parse the encoding config string
    auto encoding_parse_result =
        config::FromString<RegisteredEncodings>(kEncodingConfigText, nullptr);
    EXPECT_EQ(config::kOK, encoding_parse_result.second);
    std::shared_ptr<EncodingRegistry> encoding_registry(
        (encoding_parse_result.first.release()));

    // Parse the report config string
    auto report_parse_result =
        config::FromString<RegisteredReports>(kReportConfigText, nullptr);
    EXPECT_EQ(config::kOK, report_parse_result.second);
    report_registry_.reset((report_parse_result.first.release()));

    project_.reset(new ProjectContext(
        kCustomerId, kProjectId,
        std::make_shared<ClientConfig>(encoding_registry, metric_registry)));

    std::shared_ptr<AnalyzerConfig> analyzer_config(new AnalyzerConfig(
        encoding_registry, metric_registry, report_registry_));

    // Extract the ReportVariable from the ReportConfig.
    const auto* report_config =
        report_registry_->Get(kCustomerId, kProjectId, report_config_id);
    EXPECT_NE(nullptr, report_config);
    const ReportVariable* report_variable = &(report_config->variable(0));
    EXPECT_NE(nullptr, report_variable);

    const Metric* metric = analyzer_config->Metric(report_config->customer_id(),
                                                   report_config->project_id(),
                                                   report_config->metric_id());
    EXPECT_NE(nullptr, metric);
    const MetricPart* metric_part =
        &(metric->parts().at(report_variable->metric_part()));
    EXPECT_NE(nullptr, metric_part);

    analysis_engine_.reset(new HistogramAnalysisEngine(
        report_id_, report_variable, metric_part, analyzer_config));
  }

  // Makes an Observation with one string part which has the given
  // |string_value|, using the encoding with the given encoding_config_id.
  std::unique_ptr<Observation> MakeStringObservation(
      std::string string_value, uint32_t encoding_config_id) {
    // Construct a new Encoder with a new client secret.
    Encoder encoder(project_, ClientSecret::GenerateNewSecret());
    // Set a static current time so we know we have a static day_index.
    encoder.set_current_time(kSomeTimestamp);

    // Encode an observation.
    Encoder::Result result =
        encoder.EncodeString(kStringMetricId, encoding_config_id, string_value);
    EXPECT_EQ(Encoder::kOK, result.status);
    EXPECT_TRUE(result.observation.get() != nullptr);
    EXPECT_EQ(1, result.observation->parts_size());
    return std::move(result.observation);
  }

  // Makes an Observation with one INDEX part which has the given
  // |index| value, using the encoding with the given encoding_config_id.
  std::unique_ptr<Observation> MakeIndexObservation(
      uint32_t index, uint32_t encoding_config_id) {
    // Construct a new Encoder with a new client secret.
    Encoder encoder(project_, ClientSecret::GenerateNewSecret());
    // Set a static current time so we know we have a static day_index.
    encoder.set_current_time(kSomeTimestamp);

    // Encode an observation.
    Encoder::Result result =
        encoder.EncodeIndex(kIndexMetricId, encoding_config_id, index);
    EXPECT_EQ(Encoder::kOK, result.status);
    EXPECT_TRUE(result.observation.get() != nullptr);
    EXPECT_EQ(1, result.observation->parts_size());
    return std::move(result.observation);
  }

  // Makes an Observation with one INT part which has the given |value|, using
  // the encoding with the given encoding_config_id for the bucketed int metric.
  std::unique_ptr<Observation> MakeBucketedIntObservation(
      int64_t value, uint32_t encoding_config_id) {
    // Construct a new Encoder with a new client secret.
    Encoder encoder(project_, ClientSecret::GenerateNewSecret());
    // Set a static current time so we know we have a static day_index.
    encoder.set_current_time(kSomeTimestamp);

    // Encode an observation.
    Encoder::Result result =
        encoder.EncodeInt(kIntBucketsMetricId, encoding_config_id, value);
    EXPECT_EQ(Encoder::kOK, result.status);
    EXPECT_TRUE(result.observation.get() != nullptr);
    EXPECT_EQ(1, result.observation->parts_size());
    return std::move(result.observation);
  }

  // Makes an Observation with one IntBucketDistribution part which has the
  // given |counts| value, using the encoding with the given encoding_config_id
  // for the bucketed int metric.
  std::unique_ptr<Observation> MakeIntBucketDistributionObservation(
      const std::map<uint32_t, uint64_t>& counts, uint32_t encoding_config_id) {
    // Construct a new Encoder with a new client secret.
    Encoder encoder(project_, ClientSecret::GenerateNewSecret());
    // Set a static current time so we know we have a static day_index.
    encoder.set_current_time(kSomeTimestamp);

    // Encode an observation.
    Encoder::Result result = encoder.EncodeIntBucketDistribution(
        kIntBucketsMetricId, encoding_config_id, counts);
    EXPECT_EQ(Encoder::kOK, result.status);
    EXPECT_TRUE(result.observation.get() != nullptr);
    EXPECT_EQ(1, result.observation->parts_size());
    return std::move(result.observation);
  }

  // Makes an Observation with one string part which has the given
  // |string_value|, using the encoding with the given encoding_config_id.
  // Then passes the ObservationPart into
  // HistogramAnalysisEngine::ProcessObservationPart().
  bool MakeAndProcessStringObservationPart(std::string string_value,
                                           uint32_t encoding_config_id) {
    return MakeAndProcessStringObservationPart(
        string_value, encoding_config_id, std::make_unique<SystemProfile>());
  }
  bool MakeAndProcessStringObservationPart(
      std::string string_value, uint32_t encoding_config_id,
      std::unique_ptr<SystemProfile> profile) {
    std::unique_ptr<Observation> observation =
        MakeStringObservation(string_value, encoding_config_id);
    return analysis_engine_->ProcessObservationPart(
        kDayIndex, observation->parts().at(kPartName), std::move(profile));
  }

  // Makes an Observation with one INDEX part which has the given
  // |index| value, using the encoding with the given encoding_config_id.
  // Then passes the ObservationPart into
  // HistogramAnalysisEngine::ProcessObservationPart().
  bool MakeAndProcessIndexObservationPart(uint32_t index,
                                          uint32_t encoding_config_id) {
    return MakeAndProcessIndexObservationPart(
        index, encoding_config_id, std::make_unique<SystemProfile>());
  }
  bool MakeAndProcessIndexObservationPart(
      uint32_t index, uint32_t encoding_config_id,
      std::unique_ptr<SystemProfile> profile) {
    std::unique_ptr<Observation> observation =
        MakeIndexObservation(index, encoding_config_id);
    return analysis_engine_->ProcessObservationPart(
        kDayIndex, observation->parts().at(kPartName), std::move(profile));
  }

  // Makes an Observation with one INT part which has the given |value|, using
  // the encoding with the given encoding_config_id for the bucketed int metric.
  // Then passes the ObservationPart into
  // HistogramAnalysisEngine::ProcessObservationPart().
  bool MakeAndProcessBucketedIntObservationPart(int64_t value,
                                                uint32_t encoding_config_id) {
    return MakeAndProcessBucketedIntObservationPart(
        value, encoding_config_id, std::make_unique<SystemProfile>());
  }
  bool MakeAndProcessBucketedIntObservationPart(
      int64_t value, uint32_t encoding_config_id,
      std::unique_ptr<SystemProfile> profile) {
    std::unique_ptr<Observation> observation =
        MakeBucketedIntObservation(value, encoding_config_id);
    return analysis_engine_->ProcessObservationPart(
        kDayIndex, observation->parts().at(kPartName), std::move(profile));
  }

  // Makes an Observation with one IntBucketDistribution part which has the
  // given |counts| value, using the encoding with the given encoding_config_id
  // for the bucketed int metric.
  // Then passes the ObservationPart into
  // HistogramAnalysisEngine::ProcessObservationPart().
  bool MakeAndProcessIntBucketDistributionObservationPart(
      const std::map<uint32_t, uint64_t>& counts, uint32_t encoding_config_id) {
    return MakeAndProcessIntBucketDistributionObservationPart(
        counts, encoding_config_id, std::make_unique<SystemProfile>());
  }
  bool MakeAndProcessIntBucketDistributionObservationPart(
      const std::map<uint32_t, uint64_t>& counts, uint32_t encoding_config_id,
      std::unique_ptr<SystemProfile> profile) {
    std::unique_ptr<Observation> observation =
        MakeIntBucketDistributionObservation(counts, encoding_config_id);
    return analysis_engine_->ProcessObservationPart(
        kDayIndex, observation->parts().at(kPartName), std::move(profile));
  }

  // Invokes MakeAndProcessStringObservationPart many times using the Forculus
  // encoding. Three strings are encoded: "hello" 20 times,
  // "goodbye" 19 times and "peace" 21 times. The first and third will be
  // decrypted by Forculus and the 2nd will not.
  void MakeAndProcessForculusObservations() {
    // Add the word "hello" 20 times.
    for (size_t i = 0; i < kForculusThreshold; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "hello", kForculusEncodingConfigId));
    }
    // Add the word "goodbye" 19 times.
    for (size_t i = 0; i < kForculusThreshold - 1; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "goodbye", kForculusEncodingConfigId));
    }
    // Add the word "peace" 21 times.
    for (size_t i = 0; i < kForculusThreshold + 1; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "peace", kForculusEncodingConfigId));
    }
  }

  // Tests the HistogramAnalysisEngine when it is used on a homogeneous set of
  // Observations, all of which were encoded using the same Forculus
  // EncodingCofig.
  void DoForculusTest() {
    Init(kStringReportConfigId);
    MakeAndProcessForculusObservations();

    // Perform the analysis.
    std::vector<ReportRow> report_rows;
    EXPECT_TRUE(analysis_engine_->PerformAnalysis(&report_rows).ok());

    // Check the results.
    EXPECT_EQ(2u, report_rows.size());
    for (const auto& report_row : report_rows) {
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
  }

  // Invokes MakeAndProcessStringObservationPart many times using the
  // BasicRappor encoding.
  void MakeAndProcessBasicRapporStringObservations() {
    for (int i = 0; i < 100; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "Apple", kBasicRapporStringEncodingConfigId));
    }
    for (int i = 0; i < 200; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "Banana", kBasicRapporStringEncodingConfigId));
    }
    for (int i = 0; i < 300; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "Cantaloupe", kBasicRapporStringEncodingConfigId));
    }
  }

  void DoBasicRapporStringTest() {
    Init(kStringReportConfigId);
    MakeAndProcessBasicRapporStringObservations();

    // Perform the analysis.
    std::vector<ReportRow> report_rows;
    EXPECT_TRUE(analysis_engine_->PerformAnalysis(&report_rows).ok());

    // Check the results.
    EXPECT_EQ(3u, report_rows.size());
    for (const auto& report_row : report_rows) {
      EXPECT_NE(0, report_row.histogram().std_error());
      EXPECT_GT(report_row.histogram().count_estimate(), 0);
      ValuePart recovered_value;
      EXPECT_TRUE(report_row.histogram().has_value());
      recovered_value = report_row.histogram().value();

      EXPECT_EQ(ValuePart::kStringValue, recovered_value.data_case());
      std::string string_value = recovered_value.string_value();
      EXPECT_TRUE(string_value == "Apple" || string_value == "Banana" ||
                  string_value == "Cantaloupe");
    }
  }

  // Invokes MakeAndProcessIndexObservationPart several times: Once for
  // index 0, twice for index 1, ... 10 times for index 9.
  void MakeAndProcessBasicRapporIndexObservations() {
    for (int index = 0; index < 10; index++) {
      for (int count = 1; count <= index + 1; count++)
        EXPECT_TRUE(MakeAndProcessIndexObservationPart(
            index, kBasicRapporIndexEncodingConfigId));
    }
  }

  // Tests HistogramAnalysisEngine in the case where it performs a Basic RAPPROR
  // report using Observations of type INDEX. We test that the correct
  // human-readable labels from the ReportConfig are applied to the correct
  // report rows.
  void DoBasicRapporIndexTest() {
    Init(kIndexReportConfigId);
    MakeAndProcessBasicRapporIndexObservations();

    // Perform the analysis.
    std::vector<ReportRow> report_rows;
    EXPECT_TRUE(analysis_engine_->PerformAnalysis(&report_rows).ok());

    // Check the results.
    EXPECT_EQ(100u, report_rows.size());
    for (const auto& report_row : report_rows) {
      EXPECT_EQ(0, report_row.histogram().std_error());
      ValuePart recovered_value;
      EXPECT_TRUE(report_row.histogram().has_value());
      recovered_value = report_row.histogram().value();
      EXPECT_EQ(ValuePart::kIndexValue, recovered_value.data_case());
      uint32_t index = recovered_value.index_value();
      if (index > 9) {
        // We did not add any Observations for indices > 9.
        EXPECT_EQ(report_row.histogram().count_estimate(), 0);
        if (index == 25) {
          // Index 25 should be associated with the label 'Event Z'.
          EXPECT_EQ("Event Z", report_row.histogram().label());
        } else {
          EXPECT_EQ("", report_row.histogram().label());
        }
      } else {
        // For indices i=0..9 we added i+1 Observations.
        EXPECT_EQ(report_row.histogram().count_estimate(), index + 1);
        // We added labels for indices 0, 1 and 5 but not the others.
        switch (index) {
          case 0: {
            EXPECT_EQ("Event A", report_row.histogram().label());
            break;
          }
          case 1: {
            EXPECT_EQ("Event B", report_row.histogram().label());
            break;
          }
          case 5: {
            EXPECT_EQ("Event F", report_row.histogram().label());
            break;
          }
          default: { EXPECT_EQ("", report_row.histogram().label()); }
        }
      }
    }
  }

  // Invokes MakeAndProcessStringObservationPart many times using the
  // StringRappor encoding.
  void MakeAndProcessStringRapporObservations() {
    for (int i = 0; i < 100; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "Apple", kStringRapporEncodingConfigId));
    }
    for (int i = 0; i < 200; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "Banana", kStringRapporEncodingConfigId));
    }
    for (int i = 0; i < 300; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "Cantaloupe", kStringRapporEncodingConfigId));
    }
  }

  void DoStringRapporTest() {
    Init(kStringReportConfigId);
    MakeAndProcessStringRapporObservations();

    // Perform the analysis.
    std::vector<ReportRow> report_rows;
    auto status = analysis_engine_->PerformAnalysis(&report_rows).error_code();
    if (grpc::OK != status) {
      EXPECT_EQ(grpc::OK, status);
      return;
    }

    // Check the results.
    if (3u != report_rows.size()) {
      EXPECT_EQ(3u, report_rows.size());
      return;
    }
    for (const auto& report_row : report_rows) {
      EXPECT_GT(report_row.histogram().count_estimate(), 0);
      ValuePart recovered_value;
      EXPECT_TRUE(report_row.histogram().has_value());
      recovered_value = report_row.histogram().value();
      EXPECT_EQ(ValuePart::kStringValue, recovered_value.data_case());
      std::string string_value = recovered_value.string_value();
      EXPECT_TRUE(string_value == "Apple" || string_value == "Banana" ||
                  string_value == "Cantaloupe");
    }
  }

  std::unique_ptr<SystemProfile> MakeProfile(std::string board_name) {
    auto profile = std::make_unique<SystemProfile>();
    *profile->mutable_board_name() = board_name;
    return profile;
  }

  void MakeAndProcessGroupedStringRapporObservations() {
    for (int i = 0; i < 50; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "Apple", kStringRapporEncodingConfigId, MakeProfile("foo")));
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "Apple", kStringRapporEncodingConfigId, MakeProfile("bar")));
    }

    for (int i = 0; i < 100; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "Banana", kStringRapporEncodingConfigId, MakeProfile("foo")));
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "Banana", kStringRapporEncodingConfigId, MakeProfile("bar")));
    }

    for (int i = 0; i < 150; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "Cantaloupe", kStringRapporEncodingConfigId, MakeProfile("foo")));
      EXPECT_TRUE(MakeAndProcessStringObservationPart(
          "Cantaloupe", kStringRapporEncodingConfigId, MakeProfile("bar")));
    }
  }

  void DoGroupedStringRapporTest() {
    Init(kGroupedStringReportConfigId);
    MakeAndProcessGroupedStringRapporObservations();

    // Perform the analysis.
    std::vector<ReportRow> report_rows;
    auto status = analysis_engine_->PerformAnalysis(&report_rows).error_code();
    if (grpc::OK != status) {
      EXPECT_EQ(grpc::OK, status);
      return;
    }

    if (6u != report_rows.size()) {
      EXPECT_EQ(6u, report_rows.size());
      return;
    }

    int foo_count = 0;
    int bar_count = 0;
    for (const auto& report_row : report_rows) {
      EXPECT_GT(report_row.histogram().count_estimate(), 0);
      ValuePart recovered_value;
      EXPECT_TRUE(report_row.histogram().has_value());
      recovered_value = report_row.histogram().value();
      EXPECT_EQ(ValuePart::kStringValue, recovered_value.data_case());
      std::string string_value = recovered_value.string_value();
      EXPECT_TRUE(string_value == "Apple" || string_value == "Banana" ||
                  string_value == "Cantaloupe")
          << string_value;
      std::string board_name =
          report_row.histogram().system_profile().board_name();
      if (board_name == "foo") foo_count += 1;
      if (board_name == "bar") bar_count += 1;
    }
    EXPECT_EQ(3, foo_count);
    EXPECT_EQ(3, bar_count);
  }

  // Invokes MakeAndProcessStringObservationPart many times using the NoOp
  // encoding. Three strings are encoded: "hello" 20 times,
  // "goodbye" 19 times and "peace" 21 times.
  void MakeAndProcessUnencodedStringObservations() {
    // Add the word "hello" 20 times.
    for (size_t i = 0; i < 20; i++) {
      EXPECT_TRUE(
          MakeAndProcessStringObservationPart("hello", kNoOpEncodingConfigId));
    }
    // Add the word "goodbye" 19 times.
    for (size_t i = 0; i < 19; i++) {
      EXPECT_TRUE(MakeAndProcessStringObservationPart("goodbye",
                                                      kNoOpEncodingConfigId));
    }
    // Add the word "peace" 21 times.
    for (size_t i = 0; i < 21; i++) {
      EXPECT_TRUE(
          MakeAndProcessStringObservationPart("peace", kNoOpEncodingConfigId));
    }
  }

  // Tests the HistogramAnalysisEngine when it is used on a homogeneous set of
  // Observations, all of which were encoded using the same NoOp
  // EncodingConfig.
  void DoUnencodedStringTest() {
    Init(kStringReportConfigId);
    MakeAndProcessUnencodedStringObservations();

    // Perform the analysis.
    std::vector<ReportRow> report_rows;
    EXPECT_TRUE(analysis_engine_->PerformAnalysis(&report_rows).ok());

    // Check the results.
    EXPECT_EQ(3u, report_rows.size());
    for (const auto& report_row : report_rows) {
      EXPECT_EQ(0, report_row.histogram().std_error());
      ValuePart recovered_value;
      EXPECT_TRUE(report_row.histogram().has_value());
      recovered_value = report_row.histogram().value();

      EXPECT_EQ(ValuePart::kStringValue, recovered_value.data_case());
      std::string string_value = recovered_value.string_value();
      int count_estimate = report_row.histogram().count_estimate();
      switch (count_estimate) {
        case 19:
          EXPECT_EQ("goodbye", string_value);
          break;
        case 20:
          EXPECT_EQ("hello", string_value);
          break;
        case 21:
          EXPECT_EQ("peace", string_value);
          break;
        default:
          FAIL() << "Unexpected count for value " << string_value << ": "
                 << count_estimate;
      }
    }
  }

  // Invokes MakeAndProcessIndexObservationPart several times: Once for
  // index 0, twice for index 1, ... 10 times for index 9, all using the
  // ID of the NoOp encoding.
  void MakeAndProcessUnencodedIndexObservations() {
    for (int index = 0; index < 10; index++) {
      for (int count = 1; count <= index + 1; count++)
        EXPECT_TRUE(
            MakeAndProcessIndexObservationPart(index, kNoOpEncodingConfigId));
    }
  }

  // Tests HistogramAnalysisEngine in the case where it performs a NoOp
  // report using Observations of type INT and with bucketing enabled.
  void DoUnencodedIntBucketsTest() {
    Init(kIntBucketsReportConfigId);

    // We add a mix of distributions and individual integer observations.
    std::map<uint32_t, uint64_t> distribution1 = {
        {0, 6}, {1, 9}, {2, 2}, {3, 7}, {4, 3}, {5, 1}, {6, 7}};
    MakeAndProcessIntBucketDistributionObservationPart(distribution1,
                                                       kNoOpEncodingConfigId);
    MakeAndProcessBucketedIntObservationPart(-10, kNoOpEncodingConfigId);
    MakeAndProcessBucketedIntObservationPart(0, kNoOpEncodingConfigId);
    MakeAndProcessBucketedIntObservationPart(10, kNoOpEncodingConfigId);
    MakeAndProcessBucketedIntObservationPart(6000, kNoOpEncodingConfigId);
    std::map<uint32_t, uint64_t> distribution2 = {
        {0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6}, {6, 7}};
    MakeAndProcessIntBucketDistributionObservationPart(distribution2,
                                                       kNoOpEncodingConfigId);

    // Perform the analysis.
    std::vector<ReportRow> report_rows;
    EXPECT_TRUE(analysis_engine_->PerformAnalysis(&report_rows).ok());

    // Check that the results are as expected.
    std::map<uint32_t, uint64_t> expected = {{0, 8}, {1, 12}, {2, 6}, {3, 11},
                                             {4, 8}, {5, 7},  {6, 15}};

    EXPECT_EQ(7u, report_rows.size());
    for (const auto& report_row : report_rows) {
      EXPECT_EQ(0, report_row.histogram().std_error());
      ValuePart recovered_value;
      EXPECT_TRUE(report_row.histogram().has_value());
      recovered_value = report_row.histogram().value();
      EXPECT_EQ(ValuePart::kIndexValue, recovered_value.data_case());
      uint32_t index = recovered_value.index_value();
      EXPECT_EQ(expected[index], report_row.histogram().count_estimate())
          << index;
      // TODO(azani): Check the labels.
    }
  }

  // Tests HistogramAnalysisEngine in the case where it performs a NoOp
  // report using Observations of type INDEX. We test that the correct
  // human-readable labels from the ReportConfig are applied to the correct
  // report rows.
  void DoUnencodedIndexTest() {
    Init(kIndexReportConfigId);
    MakeAndProcessUnencodedIndexObservations();

    // Perform the analysis.
    std::vector<ReportRow> report_rows;
    EXPECT_TRUE(analysis_engine_->PerformAnalysis(&report_rows).ok());

    // Check the results.
    EXPECT_EQ(10u, report_rows.size());
    for (const auto& report_row : report_rows) {
      EXPECT_EQ(0, report_row.histogram().std_error());
      ValuePart recovered_value;
      EXPECT_TRUE(report_row.histogram().has_value());
      recovered_value = report_row.histogram().value();
      EXPECT_EQ(ValuePart::kIndexValue, recovered_value.data_case());
      uint32_t index = recovered_value.index_value();
      EXPECT_TRUE(index >= 0 && index <= 9) << index;
      // For indices i=0..9 we added i+1 Observations.
      EXPECT_EQ(report_row.histogram().count_estimate(), index + 1);
      // We added labels for indices 0, 1 and 5 but not the others.
      switch (index) {
        case 0: {
          EXPECT_EQ("Event A", report_row.histogram().label());
          break;
        }
        case 1: {
          EXPECT_EQ("Event B", report_row.histogram().label());
          break;
        }
        case 5: {
          EXPECT_EQ("Event F", report_row.histogram().label());
          break;
        }
        default: { EXPECT_EQ("", report_row.histogram().label()); }
      }
    }
  }

  void DoMixedEncodingTest() {
    Init(kStringReportConfigId);
    MakeAndProcessForculusObservations();
    MakeAndProcessBasicRapporStringObservations();

    // Perform the analysis.
    std::vector<ReportRow> report_rows;
    EXPECT_EQ(grpc::UNIMPLEMENTED,
              analysis_engine_->PerformAnalysis(&report_rows).error_code());
  }

  ReportId report_id_;
  std::shared_ptr<ProjectContext> project_;
  std::shared_ptr<ReportRegistry> report_registry_;
  std::unique_ptr<HistogramAnalysisEngine> analysis_engine_;
};

TEST_F(HistogramAnalysisEngineTest, Forculus) { DoForculusTest(); }

TEST_F(HistogramAnalysisEngineTest, BasicRapporString) {
  DoBasicRapporStringTest();
}

TEST_F(HistogramAnalysisEngineTest, BasicRapporIndex) {
  DoBasicRapporIndexTest();
}

TEST_F(HistogramAnalysisEngineTest, StringRappor) { DoStringRapporTest(); }

TEST_F(HistogramAnalysisEngineTest, GroupedStringRappor) {
  DoGroupedStringRapporTest();
}

TEST_F(HistogramAnalysisEngineTest, UnencodedStrings) {
  DoUnencodedStringTest();
}

TEST_F(HistogramAnalysisEngineTest, UnencodedIndices) {
  DoUnencodedIndexTest();
}

TEST_F(HistogramAnalysisEngineTest, UnencodedIntBuckets) {
  DoUnencodedIntBucketsTest();
}

TEST_F(HistogramAnalysisEngineTest, MixedEncoding) { DoMixedEncodingTest(); }

}  // namespace analyzer
}  // namespace cobalt

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}

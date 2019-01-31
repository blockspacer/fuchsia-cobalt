// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/raw_dump_reports.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/store/data_store.h"
#include "analyzer/store/memory_store.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/project_context.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {

using config::AnalyzerConfig;
using store::DataStore;
using store::Status;
using store::test::FaultInjectableMemoryStore;

namespace {

const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;
const uint32_t kMetricId = 1;
const uint32_t kMissingPartsMetricID = 2;
const uint32_t kNosuchMetricId = 3;
const uint32_t kDayIndex = 123456;

const char kPart1Name[] = "Part1";
const char kPart2Name[] = "Part2";
const char kPart3Name[] = "Part3";
const char kPart4Name[] = "Part4";
const char* kPartNames[] = {kPart1Name, kPart2Name, kPart3Name, kPart4Name};
std::string PartName(int part_num) {
  CHECK(1 <= part_num && part_num <= 4) << part_num;
  return kPartNames[part_num - 1];
}

const char kCobaltRegistryText[] = R"(
encoding_configs {
  customer_id: 1
  project_id: 1
  id: 1
  no_op_encoding {
  }
}

# Metric 1 is the valid metric we will use.
metric_configs {
  customer_id: 1
  project_id: 1
  id: 1
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
      data_type: STRING
    }
  }
  parts {
    key: "Part2"
    value {
      data_type: INT
    }
  }
  parts {
    key: "Part3"
    value {
      data_type: DOUBLE
    }
  }
  parts {
    key: "Part4"
    value {
      data_type: INDEX
    }
  }
}

# Metric 2 is missing all the parts we will look for.
metric_configs {
  customer_id: 1
  project_id: 1
  id: 2
  time_zone_policy: UTC
  parts {
    key: "Frog"
    value {
      data_type: STRING
    }
  }
}

)";

// Adds an ObservationPart to |observation| with the given |part_name|,
// using the NoOp encoding to directly embed a ValuePart with the given
// |data_type| and a value derived from |index|.
void AddUnencodedValuePart(std::string part_name, size_t index,
                           MetricPart::DataType data_type,
                           Observation* observation) {
  auto value = (*observation->mutable_parts())[part_name]
                   .mutable_unencoded()
                   ->mutable_unencoded_value();
  switch (data_type) {
    case MetricPart::STRING: {
      value->mutable_string_value()->assign(std::to_string(index));
      break;
    }
    case MetricPart::INT: {
      value->set_int_value(index);
      break;
    }
    case MetricPart::DOUBLE: {
      value->set_double_value(index);
      break;
    }
    case MetricPart::INDEX: {
      value->set_index_value(index);
      break;
    }
    default: { CHECK(false); }
  }
}

enum ObservationFault {
  kNoFault = 0,
  kMissingPart2 = 1,
  kWrongTypeForPart2 = 2
};

// Builds an unencoded observation for our test metric. If index is even
// then the value of |fault| is ignored and the Observation will be valid. But
// if index is odd then the Observation will have the fault specified by |fault|
// in its Part2.
Observation MakeObservationWithFault(size_t index, ObservationFault fault) {
  Observation observation;
  AddUnencodedValuePart(kPart1Name, index, MetricPart::STRING, &observation);
  if (index % 2 == 0) {
    fault = kNoFault;
  }
  switch (fault) {
    case kNoFault:
      AddUnencodedValuePart(kPart2Name, index, MetricPart::INT, &observation);
      break;

    case kMissingPart2:
      break;

    case kWrongTypeForPart2:
      AddUnencodedValuePart(kPart2Name, index, MetricPart::STRING,
                            &observation);
      break;
  }
  AddUnencodedValuePart(kPart3Name, index, MetricPart::DOUBLE, &observation);
  AddUnencodedValuePart(kPart4Name, index, MetricPart::INDEX, &observation);
  return observation;
}

// Returns the string representation of |value|, given that it was extracted
// from part |part_num| of one of the valid Observations that we made.
std::string AsString(int part_num, const ValuePart& value) {
  std::ostringstream stream;
  switch (part_num) {
    case 1:
      EXPECT_EQ(ValuePart::kStringValue, value.data_case());
      return value.string_value();
    case 2:
      EXPECT_EQ(ValuePart::kIntValue, value.data_case());
      stream << value.int_value();
      break;
    case 3:
      EXPECT_EQ(ValuePart::kDoubleValue, value.data_case());
      stream << value.double_value();
      break;
    case 4:
      EXPECT_EQ(ValuePart::kIndexValue, value.data_case());
      stream << value.index_value();
      break;
    default:
      CHECK(false) << part_num;
  }
  return stream.str();
}

// Checks a ReportRow returned from the iterator, given that |part_nums|
// was used for the current experiment.
void CheckRow(const ReportRow* row, std::vector<int> part_nums) {
  ASSERT_NE(nullptr, row);
  ASSERT_EQ(ReportRow::kRawDump, row->row_type_case());
  int expected_num_parts = static_cast<int>(part_nums.size());
  ASSERT_EQ(expected_num_parts, row->raw_dump().values_size());
  std::string value_as_string =
      AsString(part_nums[0], row->raw_dump().values(0));
  for (int i = 1; i < expected_num_parts; i++) {
    EXPECT_EQ(value_as_string,
              AsString(part_nums[i], row->raw_dump().values(i)));
  }
}

}  // namespace

// Tests of the RawDumpReportRowIterator.
class RawDumpReportRowIteratorTest : public testing::Test {
 protected:
  void SetUp() {
    data_store_.reset(new FaultInjectableMemoryStore());
    observation_store_.reset(new store::ObservationStore(data_store_));
    ASSERT_EQ(store::kOK, data_store_->DeleteAllRows(DataStore::kObservations));

    analyzer_config_ =
        AnalyzerConfig::CreateFromCobaltRegistryProtoText(kCobaltRegistryText);
  }

  // Initializes |iterator_| using our fixed customer_id, project_id, day
  // indices, and the given Metric ID and Observation parts.
  void NewIterator(uint32_t metric_id, std::vector<int> part_nums) {
    std::vector<std::string> parts(part_nums.size());
    for (size_t i = 0; i < part_nums.size(); i++) {
      parts[i] = PartName(part_nums[i]);
    }
    iterator_.reset(new RawDumpReportRowIterator(
        kCustomerId, kProjectId, metric_id, kDayIndex, kDayIndex, parts, {},
        "report_id_string", observation_store_, analyzer_config_));
  }

  // Adds Observations to the ObservationStore. Every other Observation added
  // will have the part specified by |fault| in its Part2.
  void AddObservationsWithFault(size_t num_observations,
                                ObservationFault fault) {
    std::vector<Observation> observations;
    for (size_t i = 0; i < num_observations; i++) {
      observations.emplace_back(MakeObservationWithFault(i, fault));
    }
    ObservationMetadata metadata;
    metadata.set_customer_id(kCustomerId);
    metadata.set_project_id(kProjectId);
    metadata.set_metric_id(kMetricId);
    metadata.set_day_index(kDayIndex);
    EXPECT_EQ(store::kOK,
              observation_store_->AddObservationBatch(metadata, observations));
  }

  // This method contains our iterator checking logic. We invoke the methods of
  // RawDumpIterator causing it to yield its rows and we check the results.
  //
  // |expect_init_failure| Do we expect that an error occurred when the
  // Iterator was constructed?
  //
  // |num_observations| How many Observations were added to the
  // ObservationStore.
  //
  // |expect_error_after_this_many_rows| After this may rows returned from the
  // ObservationStore, expect the ObservationStore to return an error.
  //
  // |part_nums| The indices of the ObservationParts that were used.
  //
  // |fault| Every other Observation added to the ObservationStore will have
  // this fault in its Part2.
  void IterateOnceAndCheck(bool expect_init_failure, size_t num_observations,
                           size_t expect_error_after_this_many_rows,
                           std::vector<int> part_nums, ObservationFault fault) {
    const ReportRow* next_row;
    size_t row_num = 0;
    bool has_more_rows = false;
    do {
      auto status = iterator_->HasMoreRows(&has_more_rows);
      if (expect_init_failure) {
        ASSERT_FALSE(status.ok());
        return;
      }
      if (row_num > expect_error_after_this_many_rows) {
        ASSERT_FALSE(status.ok()) << "row_num=" << row_num;
        return;
      }
      ASSERT_TRUE(status.ok())
          << status.error_message() << "(" << status.error_code()
          << ") row_num=" << row_num;
      if (has_more_rows) {
        status = iterator_->NextRow(&next_row);
        ASSERT_TRUE(status.ok())
            << status.error_message() << "(" << status.error_code()
            << ") row_num=" << row_num;
        CheckRow(next_row, part_nums);
        row_num++;
      }
    } while (has_more_rows);
    size_t expected_num_rows = num_observations;
    if (fault != kNoFault &&
        std::find(part_nums.begin(), part_nums.end(), 2) != part_nums.end()) {
      expected_num_rows = num_observations / 2;
    }
    EXPECT_EQ(expected_num_rows, row_num);
  }

  // This is our main test method. It adds |num_observations| Observations
  // to the ObservationStore in which every other Observation has the fault
  // specified by |fault| in its Part2. Then it constructs a new
  // RawDumpReportRowIterator, invokes IterateOnceAndCheck(), then
  // invokes Reset() and then invokes IterateOnceAndCheck() a second time.
  void DoIteratorTest(uint32_t metric_id, bool expect_init_failure,
                      size_t num_observations, std::vector<int> part_nums,
                      ObservationFault fault) {
    AddObservationsWithFault(num_observations, fault);
    NewIterator(metric_id, part_nums);
    IterateOnceAndCheck(expect_init_failure, num_observations, -1, part_nums,
                        fault);
    auto status = iterator_->Reset();
    ASSERT_TRUE(status.ok())
        << status.error_message() << "(" << status.error_code() << ")";
    IterateOnceAndCheck(expect_init_failure, num_observations, -1, part_nums,
                        fault);
  }

  // |metric_id| should be something other than kMetricId. This will
  // cause initialization of the iterator to fail.
  void DoIteratorTestWithInitialFailure(uint32_t metric_id) {
    DoIteratorTest(metric_id, true, 10, {3}, kNoFault);
  }

  // Invokes DoIteratorTest() with our standard metric ID so that no
  // initial failure will occur.
  void DoIteratorTestWithFault(size_t num_observations,
                               std::vector<int> part_nums,
                               ObservationFault fault) {
    DoIteratorTest(kMetricId, false, num_observations, part_nums, fault);
  }

  // Invokes DoIteratorTest() with our standard metric ID and fault = kNoFault
  // so that no failures will occur.
  void DoIteratorTestNoFault(size_t num_observations,
                             std::vector<int> part_nums) {
    DoIteratorTestWithFault(num_observations, part_nums, kNoFault);
  }

  std::shared_ptr<FaultInjectableMemoryStore> data_store_;
  std::shared_ptr<store::ObservationStore> observation_store_;
  std::shared_ptr<config::AnalyzerConfig> analyzer_config_;
  std::unique_ptr<RawDumpReportRowIterator> iterator_;
};  // namespace analyzer

// Tests in the case of no errors, zero rows, and parts 3,1.
TEST_F(RawDumpReportRowIteratorTest, NoFault0Rows) {
  DoIteratorTestNoFault(0, {3, 1});
}

// Tests in the case of no errors, 1 row, and parts 3,1.
TEST_F(RawDumpReportRowIteratorTest, NoFault1Row) {
  DoIteratorTestNoFault(1, {3, 1});
}

// Tests in the case of no errors, 999 rows, and parts 3.
TEST_F(RawDumpReportRowIteratorTest, NoFault999Rows) {
  DoIteratorTestNoFault(999, {3});
}

// Tests in the case of no errors, 1000 rows, and parts 2,4.
TEST_F(RawDumpReportRowIteratorTest, NoFault1000Rows) {
  DoIteratorTestNoFault(1000, {2, 4});
}

// Tests in the case of no errors, 1001 rows, and parts 1,4,2,3
TEST_F(RawDumpReportRowIteratorTest, NoFault1001Rows) {
  DoIteratorTestNoFault(1001, {1, 4, 2, 3});
}

// Tests in the case of no errors, 2001 rows, and parts 2,1,3,
TEST_F(RawDumpReportRowIteratorTest, NoFault2001Rows) {
  DoIteratorTestNoFault(2001, {2, 1, 3});
}

// Tests in the case of faulty Observations with a missing part that is not
// used.
TEST_F(RawDumpReportRowIteratorTest, MissingUnusedPart10Rows) {
  DoIteratorTestWithFault(10, {3, 1}, kMissingPart2);
}

// Tests in the case of faulty Observations with a missing part that is used.
TEST_F(RawDumpReportRowIteratorTest, MissingUsedPart10Rows) {
  DoIteratorTestWithFault(10, {2, 1}, kMissingPart2);
}

// Tests in the case of faulty Observations with a corrupt part that is not
// used.
TEST_F(RawDumpReportRowIteratorTest, WrongTypeUnusedPart10Rows) {
  DoIteratorTestWithFault(10, {3, 1}, kWrongTypeForPart2);
}

// Tests in the case of faulty Observations with a corrupt part that is used.
TEST_F(RawDumpReportRowIteratorTest, WrongTypeUsedPart10Rows) {
  DoIteratorTestWithFault(10, {2, 1}, kWrongTypeForPart2);
}

// Tests in the case of faulty config with a missing metric part.
TEST_F(RawDumpReportRowIteratorTest, InitFailurePartNotFoundInConfig) {
  DoIteratorTestWithInitialFailure(kMissingPartsMetricID);
}

// Tests in the case of faulty config with a missing metric.
TEST_F(RawDumpReportRowIteratorTest, InitFailureMetricNotFoundInConfig) {
  DoIteratorTestWithInitialFailure(kNosuchMetricId);
}

// Tests in the case that the ObservationStore returns an error part of
// the way through the query.
TEST_F(RawDumpReportRowIteratorTest, ObservationStoreReturnsError) {
  // We tell the DataStore to return an error after 2 queries. We know
  // that the RawDumpReportRowIterator happens to be coded so that each
  // query returns 1000 rows. So we expect a problem to occur after on
  // the row with index 2001.
  data_store_->reset_num_to_succeed(2);

  // Add more than 2001 valid Observations.
  AddObservationsWithFault(2010, kNoFault);

  // Construct the iterator.
  NewIterator(kMetricId, {3});

  // - We don't expect any initial failure because the config was good.
  // - We added 2010 Observations
  // - We expect to see an error returned *after* the row with the
  //   (zero-based) index of 1999
  // - Expect to see ObservationPart 3.
  // - Expect no faulty Observations.
  IterateOnceAndCheck(false, 2010, 1999, {3}, kNoFault);
}

}  // namespace analyzer
}  // namespace cobalt

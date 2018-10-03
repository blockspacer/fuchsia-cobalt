// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_LOGGER_TEST_UTILS_H_
#define COBALT_LOGGER_LOGGER_TEST_UTILS_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "./gtest.h"
#include "encoder/shipping_manager.h"
#include "logger/encoder.h"
#include "logger/local_aggregation.pb.h"

namespace cobalt {
namespace logger {
namespace testing {

// A mock ObservationStore.
class FakeObservationStore
    : public ::cobalt::encoder::ObservationStoreWriterInterface {
 public:
  StoreStatus AddEncryptedObservation(
      std::unique_ptr<EncryptedMessage> message,
      std::unique_ptr<ObservationMetadata> metadata) override {
    messages_received.emplace_back(std::move(message));
    metadata_received.emplace_back(std::move(metadata));
    return kOk;
  }

  std::vector<std::unique_ptr<EncryptedMessage>> messages_received;
  std::vector<std::unique_ptr<ObservationMetadata>> metadata_received;
};

// A mock ObservationStoreUpdateRecipient.
class TestUpdateRecipient
    : public ::cobalt::encoder::ObservationStoreUpdateRecipient {
 public:
  void NotifyObservationsAdded() override { invocation_count++; }

  int invocation_count = 0;
};

// A container for information about the set of all locally aggregated
// reports in a configuration. This is used by tests to check the output of the
// EventAggregator.
typedef struct ExpectedAggregationParams {
  // The total number of locally aggregated Observations which should be
  // generated each day.
  size_t daily_num_obs;
  // The MetricReportIds of the locally aggregated reports in this
  // configuration.
  std::set<MetricReportId> metric_report_ids;
  // Keys are the MetricReportIds of all locally aggregated reports in a config.
  // The value at a key is the number of Observations which should be generated
  // each day for that report.
  std::map<MetricReportId, size_t> num_obs_per_report;
  // Keys are the MetricReportIds of all locally aggregated reports in a config.
  // The value at a key is the number of event codes for that report's
  // parent MetricDefinition.
  std::map<MetricReportId, size_t> num_event_codes;
  // Keys are the MetricReportIds of all locally aggregated reports in a config.
  // The value at a key is the set of window sizes of that report.
  std::map<MetricReportId, std::set<uint32_t>> window_sizes;
} ExpectedAggregationParams;

// A representation of a set of expected UniqueActivesObservations for a fixed
// day index. Used to check the values of UniqueActivesObservations generated by
// the EventAggregator.
//
// The outer map is keyed by MetricReportId, and the value at an ID is
// a map keyed by window size. The value of the inner map at a window size is a
// vector of size equal to the number of event codes for the parent metric of
// the report, and the i-th element of the vector is |true| if the i-th event
// code occurred on the device during the window of that size, or |false| if
// not.
typedef std::map<MetricReportId, std::map<uint32_t, std::vector<bool>>>
    ExpectedActivity;

// Populates a MetricDefinitions proto message from a serialized representation.
bool PopulateMetricDefinitions(const char metric_string[],
                               MetricDefinitions* metric_definitions);

// Returns the ReportAggregationKey associated to a report, given a
// ProjectContext containing the report and the report's MetricReportId.
ReportAggregationKey MakeAggregationKey(const ProjectContext& project_context,
                                        const MetricReportId& metric_report_id);

// Returns the AggregationConfig associated to a report, given a ProjectContext
// containing the report and the report's MetricReportId.
AggregationConfig MakeAggregationConfig(const ProjectContext& project_context,
                                        const MetricReportId& metric_report_id);

// Given an ExpectedAggregationParams struct populated with information about
// the locally aggregated reports in a config, return an ExpectedActivity map
// initialized with that config's MetricReportIds and window sizes, with all
// activity indicators set to false.
ExpectedActivity MakeNullExpectedActivity(
    const ExpectedAggregationParams& expected_params);

// Populates |observations| with the contents of a FakeObservationStore.
// |observations| should be a vector whose size is equal to the number
// of expected observations. Checks the the ObservationStore contains
// the expected number of Observations and that the report_ids of the
// Observations are equal to |expected_report_ids|. Returns true iff all checks
// pass.
bool FetchObservations(std::vector<Observation2>* observations,
                       const std::vector<uint32_t>& expected_report_ids,
                       FakeObservationStore* observation_store,
                       TestUpdateRecipient* update_recipient);

// Populates |observation| with the contents of a FakeObservationStore,
// which is expected to contain a single Observation with a report_id
// of |expected_report_id|. Returns true iff all checks pass.
bool FetchSingleObservation(Observation2* observation,
                            uint32_t expected_report_id,
                            FakeObservationStore* observation_store,
                            TestUpdateRecipient* update_recipient);

// Given an ExpectedAggregationParams containing information about the set of
// locally aggregated reports in a config, populates a vector |observations|
// with the contents of a FakeObservationStore and checks that the vector
// contains exactly the number of Observations that the EventAggregator should
// generate for a single day index, for each locally aggregated report in that
// config. Does not assume that the contents of the FakeObservationStore have a
// particular order. The size of |observations| is ignored, and can be 0.
bool FetchAggregatedObservations(
    std::vector<Observation2>* observations,
    const ExpectedAggregationParams& expected_params,
    FakeObservationStore* observation_store,
    TestUpdateRecipient* update_recipient);

// Checks that the contents of a FakeObservationStore is a sequence of
// IntegerEventObservations specified by the various parameters. Returns
// true if all checks pass.
bool CheckNumericEventObservations(
    const std::vector<uint32_t>& expected_report_ids,
    uint32_t expected_event_code, const std::string expected_component_name,
    int64_t expected_int_value, FakeObservationStore* observation_store,
    TestUpdateRecipient* update_recipient);

// Checks that the Observations contained in a FakeObservationStore are exactly
// the UniqueActivesObservations that should be generated for a single day index
// given a representation of the expected activity indicators for that day, for
// each UniqueActives report, for each window size and event code, for a config
// whose locally aggregated reports are all of type UNIQUE_N_DAY_ACTIVES.
bool CheckUniqueActivesObservations(
    const ExpectedActivity& expected_activity,
    const ExpectedAggregationParams& expected_params,
    FakeObservationStore* observation_store,
    TestUpdateRecipient* update_recipient);

}  // namespace testing
}  // namespace logger
}  // namespace cobalt

#endif  //  COBALT_LOGGER_LOGGER_TEST_UTILS_H_
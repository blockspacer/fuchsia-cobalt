// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/logger_test_utils.h"

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>
#include <utility>

#include "./observation.pb.h"
#include "./observation2.pb.h"
#include "algorithms/rappor/rappor_config_helper.h"
#include "algorithms/rappor/rappor_encoder.h"
#include "config/encodings.pb.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/send_retryer.h"
#include "encoder/shipping_manager.h"
#include "logger/encoder.h"
#include "util/encrypted_message_util.h"

using ::google::protobuf::util::MessageDifferencer;

namespace cobalt {

using encoder::ClientSecret;
using rappor::BasicRapporEncoder;
using rappor::RapporConfigHelper;
using util::EncryptedMessageMaker;
using util::MessageDecrypter;

namespace logger {
namespace testing {

bool PopulateMetricDefinitions(const char metric_string[],
                               MetricDefinitions* metric_definitions) {
  google::protobuf::TextFormat::Parser parser;
  return parser.ParseFromString(metric_string, metric_definitions);
}

ReportAggregationKey MakeAggregationKey(
    const ProjectContext& project_context,
    const MetricReportId& metric_report_id) {
  ReportAggregationKey key;
  key.set_customer_id(project_context.project().customer_id());
  key.set_project_id(project_context.project().project_id());
  key.set_metric_id(metric_report_id.first);
  key.set_report_id(metric_report_id.second);
  return key;
}

AggregationConfig MakeAggregationConfig(
    const ProjectContext& project_context,
    const MetricReportId& metric_report_id) {
  AggregationConfig config;
  const auto& metric = project_context.GetMetric(metric_report_id.first);
  bool found_report_id;
  for (const auto& report : metric->reports()) {
    if (metric_report_id.second == report.id()) {
      found_report_id = true;
      *config.mutable_project() = project_context.project();
      *config.mutable_metric() = *metric;
      *config.mutable_report() = report;
      break;
    }
  }
  if (!found_report_id) {
    LOG(ERROR) << "Report ID " << metric_report_id.second << " not found.\n";
  }
  return config;
}

ExpectedActivity MakeNullExpectedActivity(
    const ExpectedAggregationParams& expected_params) {
  ExpectedActivity expected_activity;
  for (const auto& report_pair : expected_params.window_sizes) {
    for (const auto& window_size : report_pair.second) {
      for (uint32_t event_code = 0;
           event_code < expected_params.num_event_codes.at(report_pair.first);
           event_code++) {
        expected_activity[report_pair.first][window_size].push_back(false);
      }
    }
  }
  return expected_activity;
}

bool FetchObservations(std::vector<Observation2>* observations,
                       const std::vector<uint32_t>& expected_report_ids,
                       FakeObservationStore* observation_store,
                       TestUpdateRecipient* update_recipient) {
  CHECK(observations);
  size_t expected_num_received = observations->size();
  CHECK(expected_report_ids.size() == expected_num_received);
  auto num_received = observation_store->messages_received.size();
  EXPECT_EQ(num_received, observation_store->metadata_received.size());
  if (num_received != observation_store->metadata_received.size()) {
    return false;
  }
  EXPECT_EQ(num_received, expected_num_received);
  if (num_received != expected_num_received) {
    return false;
  }
  num_received = update_recipient->invocation_count;
  EXPECT_EQ(num_received, expected_num_received);
  if (num_received != expected_num_received) {
    return false;
  }
  MessageDecrypter message_decrypter("");

  for (auto i = 0u; i < expected_num_received; i++) {
    bool isNull = (observation_store->metadata_received[i].get() == nullptr);
    EXPECT_FALSE(isNull);
    if (isNull) {
      return false;
    }
    EXPECT_EQ(observation_store->metadata_received[i]->report_id(),
              expected_report_ids[i])
        << "i=" << i;
    isNull = (observation_store->messages_received[i].get() == nullptr);
    EXPECT_FALSE(isNull);
    if (isNull) {
      return false;
    }
    bool successfullyDeserialized = message_decrypter.DecryptMessage(
        *(observation_store->messages_received[i]), &(observations->at(i)));
    EXPECT_TRUE(successfullyDeserialized);
    if (!successfullyDeserialized) {
      return false;
    }
    bool has_random_id = !(observations->at(i).random_id().empty());
    EXPECT_TRUE(has_random_id);
    if (!successfullyDeserialized) {
      return false;
    }
  }
  return true;
}

bool FetchSingleObservation(Observation2* observation,
                            uint32_t expected_report_id,
                            FakeObservationStore* observation_store,
                            TestUpdateRecipient* update_recipient) {
  std::vector<Observation2> observations(1);
  std::vector<uint32_t> expected_report_ids;
  expected_report_ids.push_back(expected_report_id);
  if (!FetchObservations(&observations, expected_report_ids, observation_store,
                         update_recipient)) {
    return false;
  }
  *observation = observations[0];
  return true;
}

bool FetchAggregatedObservations(
    std::vector<Observation2>* observations,
    const ExpectedAggregationParams& expected_params,
    FakeObservationStore* observation_store,
    TestUpdateRecipient* update_recipient) {
  auto num_received = observation_store->messages_received.size();
  EXPECT_EQ(num_received, observation_store->metadata_received.size());
  if (num_received != observation_store->metadata_received.size()) {
    return false;
  }
  EXPECT_EQ(expected_params.daily_num_obs, num_received);
  if (expected_params.daily_num_obs != num_received) {
    return false;
  }
  num_received = update_recipient->invocation_count;
  EXPECT_EQ(expected_params.daily_num_obs, num_received);
  if (expected_params.daily_num_obs != num_received) {
    return false;
  }
  observations->resize(expected_params.daily_num_obs);
  MessageDecrypter message_decrypter("");
  // Get the expected number of Observations for each report ID.
  // Decrement the expected number as received Observations are counted.
  auto expected_num_obs_by_id = expected_params.num_obs_per_report;
  for (auto i = 0u; i < expected_params.daily_num_obs; i++) {
    bool isNull = (observation_store->metadata_received[i].get() == nullptr);
    EXPECT_FALSE(isNull);
    if (isNull) {
      return false;
    }
    auto metric_report_id =
        MetricReportId(observation_store->metadata_received[i]->metric_id(),
                       observation_store->metadata_received[i]->report_id());
    EXPECT_GE(expected_num_obs_by_id[metric_report_id], 1u) << "i=" << i;
    expected_num_obs_by_id[metric_report_id]--;

    isNull = (observation_store->messages_received[i].get() == nullptr);
    EXPECT_FALSE(isNull);
    if (isNull) {
      return false;
    }
    bool successfullyDeserialized = message_decrypter.DecryptMessage(
        *(observation_store->messages_received[i]), &(observations->at(i)));
    EXPECT_TRUE(successfullyDeserialized);
    if (!successfullyDeserialized) {
      return false;
    }
    bool has_random_id = !(observations->at(i).random_id().empty());
    EXPECT_TRUE(has_random_id);
    if (!successfullyDeserialized) {
      return false;
    }
  }
  // Check that all expected Observations have been found.
  for (auto iter = expected_num_obs_by_id.begin();
       iter != expected_num_obs_by_id.end(); iter++) {
    EXPECT_EQ(0u, iter->second);
  }
  return true;
}

bool CheckNumericEventObservations(
    const std::vector<uint32_t>& expected_report_ids,
    uint32_t expected_event_code, const std::string expected_component_name,
    int64_t expected_int_value, FakeObservationStore* observation_store,
    TestUpdateRecipient* update_recipient) {
  size_t expected_num_observations = expected_report_ids.size();
  std::vector<Observation2> observations(expected_num_observations);
  if (!FetchObservations(&observations, expected_report_ids, observation_store,
                         update_recipient)) {
    return false;
  }
  for (auto i = 0u; i < expected_num_observations; i++) {
    const auto& numeric_event = observations[i].numeric_event();
    EXPECT_EQ(expected_event_code, numeric_event.event_code());
    if (expected_event_code != numeric_event.event_code()) {
      return false;
    }
    if (expected_component_name.empty()) {
      EXPECT_TRUE(numeric_event.component_name_hash().empty());
      if (!numeric_event.component_name_hash().empty()) {
        return false;
      }
    } else {
      EXPECT_EQ(numeric_event.component_name_hash().size(), 32u);
      if (numeric_event.component_name_hash().size() != 32u) {
        return false;
      }
    }
    EXPECT_EQ(expected_int_value, numeric_event.value());
    if (expected_int_value != numeric_event.value()) {
      return false;
    }
  }
  return true;
}

bool CheckUniqueActivesObservations(
    const ExpectedActivity& expected_activity,
    const ExpectedAggregationParams& expected_params,
    FakeObservationStore* observation_store,
    TestUpdateRecipient* update_recipient) {
  // A container for the strings expected to appear in the |data| field of the
  // BasicRapporObservation wrapped by the UniqueActivesObservation for a given
  // MetricReportId, window size, and event code.
  std::map<MetricReportId, std::map<uint32_t, std::map<uint32_t, std::string>>>
      expected_values;
  // Form Basic RAPPOR-encoded bits from the expected activity values and
  // populate |expected_values|.
  BasicRapporConfig basic_rappor_config;
  basic_rappor_config.set_prob_0_becomes_1(0.0);
  basic_rappor_config.set_prob_1_stays_1(1.0);
  basic_rappor_config.mutable_indexed_categories()->set_num_categories(1u);
  std::unique_ptr<BasicRapporEncoder> encoder(new BasicRapporEncoder(
      basic_rappor_config, ClientSecret::GenerateNewSecret()));
  for (const auto& id_pair : expected_activity) {
    expected_values[id_pair.first] = {};
    for (const auto& window_pair : id_pair.second) {
      expected_values[id_pair.first][window_pair.first] = {};
      for (uint32_t event_code = 0; event_code < window_pair.second.size();
           event_code++) {
        BasicRapporObservation basic_rappor_obs;
        if (window_pair.second[event_code]) {
          ValuePart value;
          value.set_index_value(0u);
          encoder->Encode(value, &basic_rappor_obs);
        } else {
          encoder->EncodeNullObservation(&basic_rappor_obs);
        }
        expected_values[id_pair.first][window_pair.first][event_code] =
            basic_rappor_obs.data();
      }
    }
  }
  // Fetch the contents of the ObservationStore and check that each
  // received Observation corresponds to an element of |expected_values|.
  std::vector<Observation2> observations;
  if (!FetchAggregatedObservations(&observations, expected_params,
                                   observation_store, update_recipient)) {
    return false;
  }

  for (size_t i = 0; i < observations.size(); i++) {
    if (!observations.at(i).has_unique_actives()) {
      return false;
    }
    auto obs_id =
        MetricReportId(observation_store->metadata_received[i]->metric_id(),
                       observation_store->metadata_received[i]->report_id());
    if (expected_values.count(obs_id) == 0) {
      return false;
    }
    uint32_t obs_window_size =
        observations.at(i).unique_actives().window_size();
    if (expected_values.at(obs_id).count(obs_window_size) == 0) {
      return false;
    }
    uint32_t obs_event_code = observations.at(i).unique_actives().event_code();
    if (expected_values.at(obs_id).at(obs_window_size).count(obs_event_code) ==
        0) {
      return false;
    }
    std::string obs_data =
        observations.at(i).unique_actives().basic_rappor_obs().data();
    if (expected_values.at(obs_id).at(obs_window_size).at(obs_event_code) !=
        obs_data) {
      return false;
    }
    // Remove the bucket of |expected_values| corresponding to the
    // received Observation.
    expected_values[obs_id][obs_window_size].erase(obs_event_code);
    if (expected_values[obs_id][obs_window_size].empty()) {
      expected_values[obs_id].erase(obs_window_size);
    }
    if (expected_values[obs_id].empty()) {
      expected_values.erase(obs_id);
    }
  }
  // Check that every expected Observation has been received.
  if (!expected_values.empty()) {
    return false;
  }
  return true;
}

}  // namespace testing
}  // namespace logger
}  // namespace cobalt

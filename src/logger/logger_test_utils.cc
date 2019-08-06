// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logger/logger_test_utils.h"

#include <algorithm>
#include <utility>

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>

#include "./logging.h"
#include "./observation.pb.h"
#include "./observation2.pb.h"
#include "src/algorithms/rappor/rappor_config_helper.h"
#include "src/algorithms/rappor/rappor_encoder.h"
#include "src/logger/encoder.h"
#include "src/logger/project_context_factory.h"
#include "src/registry/encodings.pb.h"
#include "src/system_data/client_secret.h"
#include "util/encrypted_message_util.h"

namespace cobalt {

using crypto::byte;
using crypto::hash::DIGEST_SIZE;
using encoder::ClientSecret;
using rappor::BasicRapporEncoder;
using util::MessageDecrypter;

namespace logger {
namespace testing {

namespace {

constexpr uint32_t kComponentNameHashSize = 32;

// Populates |*hash_out| with the SHA256 of |component|, unless |component|
// is empty in which case *hash_out is set to the empty string also. An
// empty string indicates that the component_name feature is not being used.
// We expect this to be a common case and in this case there is no point
// in using 32 bytes to represent the empty string. Returns true on success
// and false on failure (unexpected).
bool HashComponentNameIfNotEmpty(const std::string& component, std::string* hash_out) {
  CHECK(hash_out);
  if (component.empty()) {
    hash_out->resize(0);
    return true;
  }
  hash_out->resize(DIGEST_SIZE);
  return cobalt::crypto::hash::Hash(reinterpret_cast<const byte*>(component.data()),
                                    component.size(), reinterpret_cast<byte*>(&hash_out->front()));
}
}  // namespace

std::unique_ptr<ProjectContext> GetTestProject(const std::string& registry_base64) {
  auto project_context_factory =
      ProjectContextFactory::CreateFromCobaltRegistryBase64(registry_base64);
  EXPECT_NE(nullptr, project_context_factory);
  EXPECT_TRUE(project_context_factory->is_single_project());
  return project_context_factory->TakeSingleProjectContext();
}

ReportAggregationKey MakeAggregationKey(const ProjectContext& project_context,
                                        const MetricReportId& metric_report_id) {
  ReportAggregationKey key;
  key.set_customer_id(project_context.project().customer_id());
  key.set_project_id(project_context.project().project_id());
  key.set_metric_id(metric_report_id.first);
  key.set_report_id(metric_report_id.second);
  return key;
}

AggregationConfig MakeAggregationConfig(const ProjectContext& project_context,
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
      std::vector<uint32_t> window_sizes;
      for (const auto& window_size : report.window_size()) {
        window_sizes.push_back(window_size);
      }
      std::sort(window_sizes.begin(), window_sizes.end());
      for (const auto& window_size : window_sizes) {
        config.add_window_size(window_size);
      }
      break;
    }
  }
  if (!found_report_id) {
    LOG(ERROR) << "Report ID " << metric_report_id.second << " not found.\n";
  }
  return config;
}

ExpectedUniqueActivesObservations MakeNullExpectedUniqueActivesObservations(
    const ExpectedAggregationParams& expected_params, uint32_t day_index) {
  ExpectedUniqueActivesObservations expected_obs;
  for (const auto& report_pair : expected_params.window_sizes) {
    for (const auto& window_size : report_pair.second) {
      for (uint32_t event_code = 0;
           event_code < expected_params.num_event_codes.at(report_pair.first); event_code++) {
        expected_obs[{report_pair.first, day_index}][window_size].push_back(false);
      }
    }
  }
  return expected_obs;
}

ExpectedReportParticipationObservations MakeExpectedReportParticipationObservations(
    const ExpectedAggregationParams& expected_params, uint32_t day_index) {
  ExpectedReportParticipationObservations expected_obs;

  for (const auto& report_pair : expected_params.window_sizes) {
    expected_obs.insert({report_pair.first, day_index});
  }
  return expected_obs;
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
  EXPECT_EQ(num_received, observation_store->num_observations_added());
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
    EXPECT_EQ(observation_store->metadata_received[i]->report_id(), expected_report_ids[i])
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

bool FetchSingleObservation(Observation2* observation, uint32_t expected_report_id,
                            FakeObservationStore* observation_store,
                            TestUpdateRecipient* update_recipient) {
  std::vector<Observation2> observations(1);
  std::vector<uint32_t> expected_report_ids;
  expected_report_ids.push_back(expected_report_id);
  if (!FetchObservations(&observations, expected_report_ids, observation_store, update_recipient)) {
    return false;
  }
  *observation = observations[0];
  return true;
}

bool FetchAggregatedObservations(std::vector<Observation2>* observations,
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
    auto metric_report_id = MetricReportId(observation_store->metadata_received[i]->metric_id(),
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
  for (auto iter = expected_num_obs_by_id.begin(); iter != expected_num_obs_by_id.end(); iter++) {
    EXPECT_EQ(0u, iter->second);
  }
  return true;
}

bool CheckNumericEventObservations(const std::vector<uint32_t>& expected_report_ids,
                                   uint32_t expected_event_code,
                                   const std::string& expected_component_name,
                                   int64_t expected_int_value,
                                   FakeObservationStore* observation_store,
                                   TestUpdateRecipient* update_recipient) {
  size_t expected_num_observations = expected_report_ids.size();
  std::vector<Observation2> observations(expected_num_observations);
  if (!FetchObservations(&observations, expected_report_ids, observation_store, update_recipient)) {
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
      EXPECT_EQ(numeric_event.component_name_hash().size(), kComponentNameHashSize);
      if (numeric_event.component_name_hash().size() != kComponentNameHashSize) {
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

bool CheckUniqueActivesObservations(const ExpectedUniqueActivesObservations& expected_obs,
                                    FakeObservationStore* observation_store,
                                    TestUpdateRecipient* update_recipient) {
  // An ExpectedAggregationParams struct describing the number of Observations
  // for each report ID which are expected to be in the FakeObservationStore
  // when this method is called.
  ExpectedAggregationParams expected_params;
  // A container for the strings expected to appear in the |data| field of the
  // BasicRapporObservation wrapped by the UniqueActivesObservation for a
  // given MetricReportId, day index, window size, and event code.
  std::map<std::pair<MetricReportId, uint32_t>, std::map<uint32_t, std::map<uint32_t, std::string>>>
      expected_values;
  // Form Basic RAPPOR-encoded bits from the expected activity
  // indicators and populate |expected_params| and |expected_values|.
  BasicRapporConfig basic_rappor_config;
  basic_rappor_config.set_prob_0_becomes_1(0.0);
  basic_rappor_config.set_prob_1_stays_1(1.0);
  basic_rappor_config.mutable_indexed_categories()->set_num_categories(1u);
  std::unique_ptr<BasicRapporEncoder> encoder(
      new BasicRapporEncoder(basic_rappor_config, ClientSecret::GenerateNewSecret()));
  for (const auto& id_pair : expected_obs) {
    expected_values[id_pair.first] = {};
    expected_params.metric_report_ids.insert(id_pair.first.first);
    for (const auto& window_pair : id_pair.second) {
      expected_values[id_pair.first][window_pair.first] = {};
      for (uint32_t event_code = 0; event_code < window_pair.second.size(); event_code++) {
        BasicRapporObservation basic_rappor_obs;
        if (window_pair.second[event_code]) {
          ValuePart value;
          value.set_index_value(0u);
          encoder->Encode(value, &basic_rappor_obs);
        } else {
          encoder->EncodeNullObservation(&basic_rappor_obs);
        }
        expected_values[id_pair.first][window_pair.first][event_code] = basic_rappor_obs.data();
        expected_params.num_obs_per_report[id_pair.first.first]++;
        expected_params.daily_num_obs++;
      }
    }
  }
  // Fetch the contents of the ObservationStore and check that each
  // received Observation corresponds to an element of |expected_values|.
  std::vector<Observation2> observations;
  if (!FetchAggregatedObservations(&observations, expected_params, observation_store,
                                   update_recipient)) {
    return false;
  }

  for (size_t i = 0; i < observations.size(); i++) {
    if (!observations.at(i).has_unique_actives()) {
      return false;
    }
    auto obs_key =
        std::make_pair(MetricReportId(observation_store->metadata_received[i]->metric_id(),
                                      observation_store->metadata_received[i]->report_id()),
                       observation_store->metadata_received[i]->day_index());
    if (expected_values.count(obs_key) == 0) {
      return false;
    }
    uint32_t obs_window_size = observations.at(i).unique_actives().window_size();
    if (expected_values.at(obs_key).count(obs_window_size) == 0) {
      return false;
    }
    uint32_t obs_event_code = observations.at(i).unique_actives().event_code();
    if (expected_values.at(obs_key).at(obs_window_size).count(obs_event_code) == 0) {
      return false;
    }
    std::string obs_data = observations.at(i).unique_actives().basic_rappor_obs().data();
    if (expected_values.at(obs_key).at(obs_window_size).at(obs_event_code) != obs_data) {
      return false;
    }
    // Remove the bucket of |expected_values| corresponding to the
    // received Observation.
    expected_values[obs_key][obs_window_size].erase(obs_event_code);
    if (expected_values[obs_key][obs_window_size].empty()) {
      expected_values[obs_key].erase(obs_window_size);
    }
    if (expected_values[obs_key].empty()) {
      expected_values.erase(obs_key);
    }
  }
  // Check that every expected Observation has been received.
  return expected_values.empty();
}

bool CheckPerDeviceNumericObservations(
    ExpectedPerDeviceNumericObservations expected_per_device_numeric_obs,
    ExpectedReportParticipationObservations expected_report_participation_obs,
    FakeObservationStore* observation_store, TestUpdateRecipient* update_recipient) {
  // An ExpectedAggregationParams struct describing the number of
  // Observations for each report ID which are expected to be
  // in the FakeObservationStore when this method is called.
  ExpectedAggregationParams expected_params;
  std::map<std::string, std::string> component_hashes;
  for (const auto& id_pair : expected_report_participation_obs) {
    expected_params.daily_num_obs++;
    expected_params.num_obs_per_report[id_pair.first]++;
    expected_params.metric_report_ids.insert(id_pair.first);
  }
  for (const auto& id_pair : expected_per_device_numeric_obs) {
    for (const auto& window_size_pair : id_pair.second) {
      auto window_size = window_size_pair.first;
      expected_params.window_sizes[id_pair.first.first].insert(window_size);
      for (const auto& expected_obs : window_size_pair.second) {
        expected_params.daily_num_obs++;
        expected_params.num_obs_per_report[id_pair.first.first]++;
        const std::string& component = std::get<0>(expected_obs);
        std::string component_hash;
        HashComponentNameIfNotEmpty(component, &component_hash);
        component_hashes[component_hash] = component;
      }
    }
  }
  // Fetch the contents of the ObservationStore and check that each
  // received Observation corresponds to an element of |expected_values|.
  std::vector<Observation2> observations;
  if (!FetchAggregatedObservations(&observations, expected_params, observation_store,
                                   update_recipient)) {
    return false;
  }
  std::vector<Observation2> report_participation_obs;
  std::vector<ObservationMetadata> report_participation_metadata;
  std::vector<Observation2> per_device_numeric_obs;
  std::vector<ObservationMetadata> per_device_numeric_metadata;
  for (size_t i = 0; i < observations.size(); i++) {
    if (observations.at(i).has_report_participation()) {
      report_participation_obs.push_back(observations.at(i));
      report_participation_metadata.push_back(*observation_store->metadata_received[i]);
    } else if (observations.at(i).has_per_device_numeric()) {
      per_device_numeric_obs.push_back(observations.at(i));
      per_device_numeric_metadata.push_back(*observation_store->metadata_received[i]);
    } else {
      return false;
    }
  }
  // Check the received PerDeviceNumericObservations
  for (size_t i = 0; i < per_device_numeric_obs.size(); i++) {
    auto obs_key = std::make_pair(MetricReportId(per_device_numeric_metadata[i].metric_id(),
                                                 per_device_numeric_metadata[i].report_id()),
                                  per_device_numeric_metadata[i].day_index());
    auto report_iter = expected_per_device_numeric_obs.find(obs_key);
    if (report_iter == expected_per_device_numeric_obs.end()) {
      return false;
    }
    auto obs = per_device_numeric_obs.at(i);
    uint32_t obs_window_size = obs.per_device_numeric().window_size();
    auto window_iter = report_iter->second.find(obs_window_size);
    if (window_iter == report_iter->second.end()) {
      return false;
    }
    std::string obs_component_hash =
        obs.per_device_numeric().integer_event_obs().component_name_hash();
    std::string obs_component;
    auto hash_iter = component_hashes.find(obs_component_hash);
    if (hash_iter == component_hashes.end()) {
      return false;
    }
    obs_component = component_hashes[obs_component_hash];
    auto obs_tuple =
        std::make_tuple(obs_component, obs.per_device_numeric().integer_event_obs().event_code(),
                        obs.per_device_numeric().integer_event_obs().value());
    auto obs_iter = window_iter->second.find(obs_tuple);
    if (obs_iter == window_iter->second.end()) {
      return false;
    }
    expected_per_device_numeric_obs.at(obs_key).at(obs_window_size).erase(obs_tuple);
    if (expected_per_device_numeric_obs.at(obs_key).at(obs_window_size).empty()) {
      expected_per_device_numeric_obs.at(obs_key).erase(obs_window_size);
    }
    if (expected_per_device_numeric_obs.at(obs_key).empty()) {
      expected_per_device_numeric_obs.erase(obs_key);
    }
  }
  if (!expected_per_device_numeric_obs.empty()) {
    return false;
  }

  // Check the received ReportParticipationObservations
  for (size_t i = 0; i < report_participation_obs.size(); i++) {
    auto obs_key = std::make_pair(MetricReportId(report_participation_metadata[i].metric_id(),
                                                 report_participation_metadata[i].report_id()),
                                  report_participation_metadata[i].day_index());
    if (expected_report_participation_obs.count(obs_key) == 0) {
      return false;
    }
    expected_report_participation_obs.erase(obs_key);
  }

  return expected_report_participation_obs.empty();
}

}  // namespace testing
}  // namespace logger
}  // namespace cobalt

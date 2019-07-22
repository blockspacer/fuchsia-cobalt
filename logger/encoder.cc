// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/encoder.h"

#include <memory>
#include <string>

#include "./logging.h"
#include "./observation2.pb.h"
#include "./tracing.h"
#include "algorithms/forculus/forculus_encrypter.h"
#include "algorithms/rappor/rappor_config_helper.h"
#include "algorithms/rappor/rappor_encoder.h"
#include "config/packed_event_codes.h"
#include "logger/project_context.h"

namespace cobalt::logger {

using ::cobalt::crypto::byte;
using ::cobalt::crypto::hash::DIGEST_SIZE;
using ::cobalt::encoder::ClientSecret;
using ::cobalt::encoder::SystemDataInterface;
using ::cobalt::forculus::ForculusEncrypter;
using ::cobalt::rappor::BasicRapporEncoder;
using ::cobalt::rappor::RapporConfigHelper;
using ::cobalt::rappor::RapporEncoder;
using ::google::protobuf::RepeatedField;

namespace {
// Populates |*hash_out| with the SHA256 of |component|, unless |component|
// is empty in which case *hash_out is set to the empty string also. An
// empty string indicates that the component_name feature is not being used.
// We expect this to be a common case and in this case there is no point
// in using 32 bytes to represent the empty string. Returns true on success
// and false on failure (unexpected).
bool HashComponentNameIfNotEmpty(const std::string& component,
                                 std::string* hash_out) {
  CHECK(hash_out);
  if (component.empty()) {
    hash_out->resize(0);
    return true;
  }
  hash_out->resize(DIGEST_SIZE);
  return cobalt::crypto::hash::Hash(
      reinterpret_cast<const byte*>(component.data()), component.size(),
      reinterpret_cast<byte*>(&hash_out->front()));
}

// Translates a rappor::Status |status| into a logger::Status and prints a debug
// message if |status| is not kOK.
Status TranslateBasicRapporEncoderStatus(MetricRef metric,
                                         const ReportDefinition* report,
                                         const rappor::Status& status) {
  switch (status) {
    case rappor::kOK:
      return kOK;
    case rappor::kInvalidConfig:
      LOG(ERROR) << "BasicRapporEncoder returned kInvalidConfig for: Report "
                 << report->report_name() << " for metric "
                 << metric.metric_name() << " in project "
                 << metric.ProjectDebugString() << ".";
      return kInvalidConfig;
    case rappor::kInvalidInput:
      LOG(ERROR) << "BasicRapporEncoder returned kInvalidInput for: Report "
                 << report->report_name() << " for metric "
                 << metric.metric_name() << " in project "
                 << metric.ProjectDebugString() << ".";
      return kInvalidArguments;
  }
}

}  // namespace

Encoder::Encoder(ClientSecret client_secret,
                 const encoder::SystemDataInterface* system_data)
    : client_secret_(std::move(client_secret)), system_data_(system_data) {}

Encoder::Result Encoder::EncodeBasicRapporObservation(
    MetricRef metric, const ReportDefinition* report, uint32_t day_index,
    uint32_t value_index, uint32_t num_categories) const {
  TRACE_DURATION("cobalt_core", "Encoder::EncodeBasicRapporObservation");

  auto result = MakeObservation(metric, report, day_index);
  auto* observation = result.observation.get();
  auto* basic_rappor_observation = observation->mutable_basic_rappor();

  BasicRapporConfig basic_rappor_config;
  basic_rappor_config.set_prob_rr(RapporConfigHelper::kProbRR);
  basic_rappor_config.mutable_indexed_categories()->set_num_categories(
      num_categories);
  float prob_bit_flip =
      RapporConfigHelper::ProbBitFlip(*report, metric.FullyQualifiedName());
  basic_rappor_config.set_prob_0_becomes_1(prob_bit_flip);
  basic_rappor_config.set_prob_1_stays_1(1.0f - prob_bit_flip);

  // TODO(rudominer) Stop copying the client_secret_ on each Encode*()
  // operation.
  BasicRapporEncoder basic_rappor_encoder(basic_rappor_config, client_secret_);
  ValuePart index_value;
  index_value.set_index_value(value_index);
  result.status = TranslateBasicRapporEncoderStatus(
      metric, report,
      basic_rappor_encoder.Encode(index_value, basic_rappor_observation));
  return result;
}

Encoder::Result Encoder::EncodeRapporObservation(MetricRef metric,
                                                 const ReportDefinition* report,
                                                 uint32_t day_index,
                                                 const std::string& str) const {
  auto result = MakeObservation(metric, report, day_index);
  auto* observation = result.observation.get();
  auto* rappor_observation = observation->mutable_string_rappor();

  RapporConfig rappor_config;
  rappor_config.set_num_hashes(RapporConfigHelper::kNumHashes);
  rappor_config.set_num_cohorts(
      RapporConfigHelper::StringRapporNumCohorts(*report));
  rappor_config.set_num_bloom_bits(
      RapporConfigHelper::StringRapporNumBloomBits(*report));
  rappor_config.set_prob_rr(RapporConfigHelper::kProbRR);
  float prob_bit_flip =
      RapporConfigHelper::ProbBitFlip(*report, metric.FullyQualifiedName());
  rappor_config.set_prob_0_becomes_1(prob_bit_flip);
  rappor_config.set_prob_1_stays_1(1.0f - prob_bit_flip);

  RapporEncoder rappor_encoder(rappor_config, client_secret_);
  ValuePart string_value;
  string_value.set_string_value(str);
  switch (rappor_encoder.Encode(string_value, rappor_observation)) {
    case rappor::kOK:
      break;

    case rappor::kInvalidConfig:
      LOG(ERROR) << "RapporEncoder returned kInvalidConfig for: Report "
                 << report->report_name() << " for metric "
                 << metric.metric_name() << " in project "
                 << metric.ProjectDebugString() << ".";
      result.status = kInvalidConfig;
      return result;

    case rappor::kInvalidInput:
      LOG(ERROR) << "RapporEncoder returned kInvalidInput for: Report "
                 << report->report_name() << " for metric "
                 << metric.metric_name() << " in project "
                 << metric.ProjectDebugString() << ".";
      result.status = kInvalidArguments;
      return result;
  }
  return result;
}

Encoder::Result Encoder::EncodeForculusObservation(
    MetricRef metric, const ReportDefinition* report, uint32_t day_index,
    const std::string& str) const {
  auto result = MakeObservation(metric, report, day_index);
  auto* observation = result.observation.get();
  auto* forculus_observation = observation->mutable_forculus();
  ForculusConfig forculus_config;
  if (report->threshold() < 2) {
    LOG(ERROR) << "Invalid Cobalt config: Report " << report->report_name()
               << " for metric " << metric.metric_name() << " in project "
               << metric.ProjectDebugString()
               << " has an invalid value for |threshold|.";
    result.status = kInvalidConfig;
    return result;
  }
  forculus_config.set_threshold(report->threshold());
  forculus_config.set_epoch_type(DAY);
  ValuePart string_value;
  string_value.set_string_value(str);
  ForculusEncrypter forculus_encrypter(
      forculus_config, metric.project().customer_id(),
      metric.project().project_id(), metric.metric_id(), "", client_secret_);

  switch (forculus_encrypter.EncryptValue(string_value, day_index,
                                          forculus_observation)) {
    case ForculusEncrypter::kOK:
      break;

    case ForculusEncrypter::kInvalidConfig:
      LOG(ERROR) << "ForculusEncrypter returned kInvalidConfig for: Report "
                 << report->report_name() << " for metric "
                 << metric.metric_name() << " in project "
                 << metric.ProjectDebugString() << ".";
      result.status = kInvalidConfig;
      return result;

    case ForculusEncrypter::kEncryptionFailed:
      LOG(ERROR) << "ForculusEncrypter returned kEncryptionFailed for: Report "
                 << report->report_name() << " for metric "
                 << metric.metric_name() << " in project "
                 << metric.ProjectDebugString() << ".";
      result.status = kOther;
  }
  return result;
}

Encoder::Result Encoder::EncodeIntegerEventObservation(
    MetricRef metric, const ReportDefinition* report, uint32_t day_index,
    const RepeatedField<uint32_t>& event_codes, const std::string& component,
    int64_t value) const {
  auto result = MakeObservation(metric, report, day_index);
  auto* observation = result.observation.get();
  auto* integer_event_observation = observation->mutable_numeric_event();
  integer_event_observation->set_event_code(
      config::PackEventCodes(event_codes));
  if (!HashComponentNameIfNotEmpty(
          component,
          integer_event_observation->mutable_component_name_hash())) {
    LOG(ERROR) << "Hashing the component name failed for: Report "
               << report->report_name() << " for metric "
               << metric.metric_name() << " in project "
               << metric.ProjectDebugString() << ".";
    result.status = kOther;
  }
  integer_event_observation->set_value(value);
  return result;
}

Encoder::Result Encoder::EncodeHistogramObservation(
    MetricRef metric, const ReportDefinition* report, uint32_t day_index,
    const RepeatedField<uint32_t>& event_codes, const std::string& component,
    HistogramPtr histogram) const {
  auto result = MakeObservation(metric, report, day_index);
  auto* observation = result.observation.get();
  auto* histogram_observation = observation->mutable_histogram();
  histogram_observation->set_event_code(config::PackEventCodes(event_codes));
  if (!HashComponentNameIfNotEmpty(
          component, histogram_observation->mutable_component_name_hash())) {
    LOG(ERROR) << "Hashing the component name failed for: Report "
               << report->report_name() << " for metric "
               << metric.metric_name() << " in project "
               << metric.ProjectDebugString() << ".";
    result.status = kOther;
  }
  histogram_observation->mutable_buckets()->Swap(histogram.get());
  return result;
}

Encoder::Result Encoder::EncodeCustomObservation(
    MetricRef metric, const ReportDefinition* report, uint32_t day_index,
    EventValuesPtr event_values) const {
  auto result = MakeObservation(metric, report, day_index);
  auto* observation = result.observation.get();
  auto* custom_observation = observation->mutable_custom();
  custom_observation->mutable_values()->swap(*event_values);
  return result;
}

Encoder::Result Encoder::EncodeUniqueActivesObservation(
    MetricRef metric, const ReportDefinition* report, uint32_t day_index,
    uint32_t event_code, bool was_active, uint32_t window_size) const {
  auto result = MakeObservation(metric, report, day_index);
  Encoder::Result basic_rappor_result;
  if (was_active) {
    // Encode a single 1 bit
    basic_rappor_result =
        EncodeBasicRapporObservation(metric, report, day_index, 0u, 1u);
  } else {
    // Encode a single 0 bit
    basic_rappor_result =
        EncodeNullBasicRapporObservation(metric, report, day_index, 1u);
  }
  if (basic_rappor_result.status != kOK) {
    result.status = basic_rappor_result.status;
    return result;
  }
  auto* observation = result.observation.get();
  auto* activity_observation = observation->mutable_unique_actives();
  activity_observation->set_window_size(window_size);
  activity_observation->set_event_code(event_code);
  activity_observation->mutable_basic_rappor_obs()->mutable_data()->swap(*(
      basic_rappor_result.observation->mutable_basic_rappor()->mutable_data()));

  return result;
}

Encoder::Result Encoder::EncodePerDeviceNumericObservation(
    MetricRef metric, const ReportDefinition* report, uint32_t day_index,
    const std::string& component, const RepeatedField<uint32_t>& event_codes,
    int64_t count, uint32_t window_size) const {
  auto result = EncodeIntegerEventObservation(metric, report, day_index,
                                              event_codes, component, count);
  auto* integer_event_observation = result.observation->release_numeric_event();
  auto* per_device_observation =
      result.observation->mutable_per_device_numeric();
  per_device_observation->set_allocated_integer_event_obs(
      integer_event_observation);
  per_device_observation->set_window_size(window_size);
  return result;
}

Encoder::Result Encoder::EncodeReportParticipationObservation(
    MetricRef metric, const ReportDefinition* report,
    uint32_t day_index) const {
  auto result = MakeObservation(metric, report, day_index);
  auto* observation = result.observation.get();
  observation->mutable_report_participation();
  return result;
}

Encoder::Result Encoder::EncodeNullBasicRapporObservation(
    MetricRef metric, const ReportDefinition* report, uint32_t day_index,
    uint32_t num_categories) const {
  auto result = MakeObservation(metric, report, day_index);
  auto* observation = result.observation.get();
  auto* basic_rappor_observation = observation->mutable_basic_rappor();

  BasicRapporConfig basic_rappor_config;
  basic_rappor_config.set_prob_rr(RapporConfigHelper::kProbRR);
  basic_rappor_config.mutable_indexed_categories()->set_num_categories(
      num_categories);
  float prob_bit_flip =
      RapporConfigHelper::ProbBitFlip(*report, metric.FullyQualifiedName());
  basic_rappor_config.set_prob_0_becomes_1(prob_bit_flip);
  basic_rappor_config.set_prob_1_stays_1(1.0f - prob_bit_flip);

  // TODO(rudominer) Stop copying the client_secret_ on each Encode*()
  // operation.
  BasicRapporEncoder basic_rappor_encoder(basic_rappor_config, client_secret_);
  result.status = TranslateBasicRapporEncoderStatus(
      metric, report,
      basic_rappor_encoder.EncodeNullObservation(basic_rappor_observation));
  return result;
}

Encoder::Result Encoder::MakeObservation(MetricRef metric,
                                         const ReportDefinition* report,
                                         uint32_t day_index) const {
  Result result;
  result.status = kOK;
  result.observation = std::make_unique<Observation2>();
  auto* observation = result.observation.get();
  result.metadata = std::make_unique<ObservationMetadata>();
  auto* metadata = result.metadata.get();

  // Generate the random_id field. Currently we use 8 bytes but our
  // infrastructure allows us to change that in the future if we wish to. The
  // random_id is used by the Analyzer Service as part of a unique row key
  // for the observation in the Observation Store.
  static const size_t kNumRandomBytes = 8;
  observation->set_allocated_random_id(new std::string(kNumRandomBytes, 0));
  random_.RandomString(observation->mutable_random_id());

  metadata->set_customer_id(metric.project().customer_id());
  metadata->set_project_id(metric.project().project_id());
  metadata->set_metric_id(metric.metric_id());
  metadata->set_report_id(report->id());
  metadata->set_day_index(day_index);

  if (system_data_) {
    const auto& profile = system_data_->system_profile();
    if (report->system_profile_field_size() == 0) {
      metadata->mutable_system_profile()->set_board_name(profile.board_name());
      metadata->mutable_system_profile()->set_product_name(
          profile.product_name());
    } else {
      for (const auto& field : report->system_profile_field()) {
        switch (field) {
          case SystemProfileField::OS:
            metadata->mutable_system_profile()->set_os(profile.os());
            break;
          case SystemProfileField::ARCH:
            metadata->mutable_system_profile()->set_arch(profile.arch());
            break;
          case SystemProfileField::BOARD_NAME:
            metadata->mutable_system_profile()->set_board_name(
                profile.board_name());
            break;
          case SystemProfileField::PRODUCT_NAME:
            metadata->mutable_system_profile()->set_product_name(
                profile.product_name());
            break;
          case SystemProfileField::SYSTEM_VERSION:
            metadata->mutable_system_profile()->set_system_version(
                profile.system_version());
            break;
          case SystemProfileField::CHANNEL:
            metadata->mutable_system_profile()->set_channel(profile.channel());
            break;
        }
      }
    }
  }

  return result;
}

}  // namespace cobalt::logger

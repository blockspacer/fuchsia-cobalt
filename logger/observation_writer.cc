// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/observation_writer.h"

#include <memory>
#include <utility>

#include "./logging.h"

namespace cobalt {
namespace logger {

namespace {
// TODO(azani): To be deleted when we start encrypting everything.
// For projects test_app2 and fuchsia_system_metrics, every report is duplicated
// with the string "_encrypted" appended to the report name. This function
// identifies ObservationMetadata that corresponds to those reports.
// Only the observations corresponding to those reports are encrypted using
// the observation_encrypter_ of the ObservationWriter.
// All other observations are sent unencrypted.
bool ShouldBeEncrypted(const ObservationMetadata &metadata) {
  // Only observations for the Fuchsia customer should be encrypted.
  if (metadata.customer_id() != 1) {
    return false;
  }
  const uint64_t kTestApp2ProjectId = 657579885;
  if (metadata.project_id() == kTestApp2ProjectId) {
    // IDs of test_app2 reports for which observations should be encrypted.
    switch (metadata.report_id()) {
      // error_counts_encrypted
      case 2146363509:
      // file_system_cache_miss_counts_encrypted
      case 2172115661:
      // file_system_cache_miss_counts_from_file_list_encrypted
      case 1368227890:
      // file_system_cache_miss_histograms_encrypted
      case 3399000372:
      // query_response_encrypted
      case 2205100518:
      // features_active_unique_devices_encrypted
      case 7800948:
      // update_duration_timing_histogram_encrypted
      case 1726545936:
      // update_duration_timing_histogram_linear_from_list_encrypted
      case 4115702700:
      // game_frame_rate_histograms_encrypted
      case 2151092773:
      // game_frame_rate_histograms_linear_from_list_encrypted
      case 2409861875:
      // application_memory_histograms_encrypted
      case 1499974448:
      // application_memory_histograms_linear_from_list_encrypted
      case 518775052:
      // power_usage_histograms_encrypted
      case 2347124339:
      // bandwidth_usage_histograms_encrypted
      case 2551346839:
        return true;
    }
  }
  const uint64_t kFuchsiaSystemMetrics = 1334068210;
  if (metadata.project_id() == kFuchsiaSystemMetrics) {
    // IDs of fuchsia_system_metrics reports for which observations should be
    // encrypted.
    switch (metadata.report_id()) {
      // fuchsia_unique_device_up_counts_encrypted
      case 205457729:
      // fuchsia_lifetime_event_counts_encrypted
      case 2146017511:
      // fuchsia_unique_device_lifetime_event_counts_encrypted
      case 3320976728:
        return true;
    }
  }

  return false;
}
}  // namespace

using ::cobalt::encoder::ObservationStoreWriterInterface;

Status ObservationWriter::WriteObservation(
    const Observation2 &observation,
    std::unique_ptr<ObservationMetadata> metadata) const {
  auto encrypted_observation = std::make_unique<EncryptedMessage>();
  auto encrypter = non_encrypter_.get();
  if (ShouldBeEncrypted(*metadata)) {
    encrypter = observation_encrypter_;
  }
  if (!encrypter->Encrypt(observation, encrypted_observation.get())) {
    LOG(ERROR) << "Encryption of an Observation failed.";
    return kOther;
  }
  auto store_status = observation_store_->AddEncryptedObservation(
      std::move(encrypted_observation), std::move(metadata));
  if (store_status != ObservationStoreWriterInterface::kOk) {
    LOG(ERROR)
        << "ObservationStore::AddEncryptedObservation() failed with status "
        << store_status;
    return kOther;
  }
  update_recipient_->NotifyObservationsAdded();
  return kOK;
}

}  // namespace logger
}  // namespace cobalt

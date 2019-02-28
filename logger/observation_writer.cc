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
bool ShouldBeEncrypted(const ObservationMetadata &metadata) {
  const uint64_t kTestApp2ProjectId = 657579885;
  if (metadata.project_id() == kTestApp2ProjectId) {
    switch (metadata.report_id()) {
      // List of _encrypted report ids for test_app2.
      case 2146363509:
      case 2172115661:
      case 1368227890:
      case 2205100518:
      case 7800948:
      case 1726545936:
      case 4115702700:
      case 2151092773:
      case 2409861875:
      case 1499974448:
      case 518775052:
      case 2347124339:
      case 2551346839:
        return true;
    }
  }
  const uint64_t kFuchsiaSystemMetrics = 1334068210;
  if (metadata.project_id() == kFuchsiaSystemMetrics) {
    switch (metadata.report_id()) {
      // List of encrypted report ids for fuchsia_system_metrics.
      case 205457729:
      case 2146017511:
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

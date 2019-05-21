// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/observation_writer.h"

#include <memory>
#include <utility>

#include "./logging.h"
#include "./tracing.h"

namespace cobalt {
namespace logger {

using ::cobalt::encoder::ObservationStoreWriterInterface;

Status ObservationWriter::WriteObservation(
    const Observation2 &observation,
    std::unique_ptr<ObservationMetadata> metadata) const {
  TRACE_DURATION("cobalt_core", "ObservationWriter::WriteObservation");
  auto encrypted_observation = std::make_unique<EncryptedMessage>();
  if (!observation_encrypter_->Encrypt(observation,
                                       encrypted_observation.get())) {
    LOG(ERROR) << "Encryption of an Observation failed.";
    return kOther;
  }
  auto store_status = observation_store_->AddEncryptedObservation(
      std::move(encrypted_observation), std::move(metadata));
  if (store_status != ObservationStoreWriterInterface::kOk) {
    LOG_FIRST_N(ERROR, 10)
        << "ObservationStore::AddEncryptedObservation() failed with status "
        << store_status;
    return kOther;
  }
  update_recipient_->NotifyObservationsAdded();
  return kOK;
}

}  // namespace logger
}  // namespace cobalt

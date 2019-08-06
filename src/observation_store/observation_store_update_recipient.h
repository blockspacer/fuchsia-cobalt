// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_OBSERVATION_STORE_OBSERVATION_STORE_UPDATE_RECIPIENT_H_
#define COBALT_SRC_OBSERVATION_STORE_OBSERVATION_STORE_UPDATE_RECIPIENT_H_

namespace cobalt {
namespace encoder {

// An ObservationStoreUpdateRecipient is any component that wishes to receive
// notifications when an Observation has been added to the ObservationStore.
class ObservationStoreUpdateRecipient {
 public:
  virtual ~ObservationStoreUpdateRecipient() = default;

  // Notifies the Recipient that AddEncryptedObservation() has been invoked on
  // the ObservationStore.
  virtual void NotifyObservationsAdded() = 0;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_SRC_OBSERVATION_STORE_OBSERVATION_STORE_UPDATE_RECIPIENT_H_

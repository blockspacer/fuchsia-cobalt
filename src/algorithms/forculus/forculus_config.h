
// Copyright 2018 The Fuchsia Authors. All rights reserved.  Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE
// file.

#ifndef COBALT_SRC_ALGORITHMS_FORCULUS_FORCULUS_CONFIG_H_
#define COBALT_SRC_ALGORITHMS_FORCULUS_FORCULUS_CONFIG_H_

#include <cstdint>

namespace cobalt::forculus {

// A Cobalt Epoch is a contiguous sequence of days used to aggregate
// observations. Each Report analyzes a set of observations from one epoch.
// Some encodings, such as Forculus, use the current epoch as a parameter.
//
// Each Cobalt |Observation| includes a |day_index| which determines in
// which epoch the Observation falls.
enum EpochType {
  // Each day is a different epoch.
  DAY = 0,
  // A week epoch is a seven day period from Sunday to Saturday
  WEEK = 1,
  // A month epoch is the set of days in a single month of the Gregorian
  // calendar
  MONTH = 2,
};

struct ForculusConfig {
  // Must satisfy 2 <= threshold <= 1,000,000
  uint32_t threshold = 0;

  // Forculus threshold encryption is based on the current epoch. For example
  // if epoch_type = WEEK and threshold = 20 then the criteria for being able
  // to decrypt a given ciphertext is that at least 20 different clients
  // all submit observations of that ciphertext tagged with a day_index in
  // the same week, from Sunday to Saturday.
  EpochType epoch_type = DAY;
};

}  // namespace cobalt::forculus

#endif  // COBALT_SRC_ALGORITHMS_FORCULUS_FORCULUS_CONFIG_H_

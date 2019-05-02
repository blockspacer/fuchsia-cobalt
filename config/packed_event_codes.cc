// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config/packed_event_codes.h"
namespace cobalt {
namespace config {

namespace {

// Used to mask off the version portion of the packed_event_codes.
const uint64_t kVersionHeaderMask = (0b1111ull << 60);

uint32_t ReadVersion(uint64_t packed_event_codes) {
  return (packed_event_codes & kVersionHeaderMask) >> 60;
}

}  // namespace

std::vector<uint32_t> UnpackEventCodes(uint64_t packed_event_codes) {
  std::vector<uint32_t> event_codes;

  switch (ReadVersion(packed_event_codes)) {
    case 0:
      for (int i = 0; i < 5; i++) {
        event_codes.push_back(
            (packed_event_codes & (kEventCodeMask << (10 * i))) >> (10 * i));
      }
      return event_codes;
    case 1:
      for (int i = 0; i < 4; i++) {
        event_codes.push_back(
            (packed_event_codes & (kV1EventCodeMask << (15 * i))) >> (15 * i));
      }
      return event_codes;
    default:
      return ((std::vector<uint32_t>){0, 0, 0, 0, 0});
  }
}

}  // namespace config
}  // namespace cobalt

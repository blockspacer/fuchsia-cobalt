// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/registry/packed_event_codes.h"

namespace cobalt::config {

namespace {

// Used to mask off the version portion of the packed_event_codes.
constexpr uint64_t kVersionHeaderMask = (0b1111ull << kVersionHeaderOffset);

uint32_t ReadVersion(uint64_t packed_event_codes) {
  return (packed_event_codes & kVersionHeaderMask) >> kVersionHeaderOffset;
}

}  // namespace

std::vector<uint32_t> UnpackEventCodes(uint64_t packed_event_codes) {
  uint64_t mask;
  uint64_t num_event_codes;
  uint64_t event_code_size;

  switch (ReadVersion(packed_event_codes)) {
    case 0:
      mask = kEventCodeMask;
      num_event_codes = kV0NumEventCodes;
      event_code_size = kV0EventCodeSize;
      break;
    case 1:
      mask = kV1EventCodeMask;
      num_event_codes = kV1NumEventCodes;
      event_code_size = kV1EventCodeSize;
      break;
    default:
      return ((std::vector<uint32_t>){0, 0, 0, 0, 0});
  }

  std::vector<uint32_t> event_codes(num_event_codes);
  for (uint64_t i = 0; i < num_event_codes; i++) {
    event_codes[i] =
        (packed_event_codes & (mask << (event_code_size * i))) >> (event_code_size * i);
  }
  return event_codes;
}

}  // namespace cobalt::config

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file contains the code to pack/unpack a list of event_codes into a
// single uint64_t. This is necessary for supporting multiple event codes
// because the event_code field in Observation is not repeated.
//
// NOTE: This is the ONLY supported way of packing/unpacking multiplexed
// event_codes into/out of the Observation proto.
//
// The encoding is a simple fixed-length encoding where each of the 5 allowed
// metric_dimensions are placed into their own 10 bit section of the uint64_t.
//
// There are currently 2 encodings defined:
//
// Version 0 supports 5 10 bit dimensions, and the bit layout is as follows:
//
// 0xVVVV__________55555555554444444444333333333322222222221111111111
//
// Version 1 supports 4 15 bit dimensions, and the bit layout is as follows:
//
// 0xVVVV444444444444444333333333333333222222222222222111111111111111
//
// Version 0 should always be preferred unless an event has a dimension value
// that cannot be represented in 10 bits. In which case Version 2 may be used if
// there are fewer than 5 dimensions provided.
//
// Where the first element in the event_codes array is placed in the least
// significant bits, and the last element is placed at the most significant
// bits.
//
// The leading 4 bits (denoted by V) are reserved for a version header, which
// will start at version 0 for the scheme described above.
//
// Bits denoted by _ are reserved and not used by this encoding version, and
// should be set to 0.
//
#ifndef COBALT_CONFIG_PACKED_EVENT_CODES_H_
#define COBALT_CONFIG_PACKED_EVENT_CODES_H_

#include <cstdint>
#include <vector>

namespace cobalt {
namespace config {

// This is the mask for pulling event codes out of packed event codes.
constexpr uint64_t kEventCodeMask = 0b1111111111;
constexpr uint64_t kV1EventCodeMask = 0b111111111111111;

constexpr uint64_t kVersionHeaderOffset = 60;

constexpr uint64_t kV0NumEventCodes = 5;
constexpr uint64_t kV0MaxEventCodeSize = 1024;
constexpr uint64_t kV0EventCodeSize = 10;

constexpr uint64_t kV1NumEventCodes = 4;
constexpr uint64_t kV1EventCodeSize = 15;

// UnpackEventCodes pulls a vector of elements out of the supplied
// |packed_event_codes|.
//
// If the version header is 0, the number of elements unpacked will always be 5.
// If the version header is 1, the number of elements unpacked will always be 4.
//
// If the version header of the packed_event_codes does not match a known
// version, the resulting vector will be filled with 5 zeroes.
std::vector<uint32_t> UnpackEventCodes(uint64_t packed_event_codes);

// PackEventCodes_v1 converts an Iterator of uint32_t (|event_codes|) and packs
// them into a single unit64_t using the v1 scheme described above.
//
// |Iterator| Any generic type over which we can iterate. Each element of the
//            iterator should be a uint32_t.
//
// |event_codes| An Iterator of uint32_t of any length (although any elements
//               past the 4th one will be ignored)
template <class Iterator>
uint64_t PackEventCodes_v1(const Iterator& event_codes) {
  uint64_t i = 0;
  uint64_t packed_event_codes = 1ull << kVersionHeaderOffset;
  for (auto code : event_codes) {
    // If the supplied iterator has more than 4 elements, we ignore them.
    if (i >= kV1NumEventCodes) {
      break;
    }
    packed_event_codes |= ((static_cast<uint64_t>(code) & kV1EventCodeMask)
                           << (kV1EventCodeSize * i));
    i += 1;
  }
  return packed_event_codes;
}

// PackEventCodes converts an Iterator of uint32_t (|event_codes|) and packs
// them into a single uint64_t using the scheme described above.
//
// |Iterator| Any generic type over which we can iterate. Each element of the
//            iterator should be a uint32_t.
//
// |event_codes| An Iterator of uint32_t of any length (although any elements
//               past the 5th one will be ignored).
template <class Iterator>
uint64_t PackEventCodes(const Iterator& event_codes) {
  uint64_t i = 0;
  uint64_t packed_event_codes = 0;
  for (auto code : event_codes) {
    // If the supplied iterator has more than 5 elements, we ignore them.
    if (i >= kV0NumEventCodes) {
      break;
    }
    if (code >= kV0MaxEventCodeSize) {
      // We need to fall back to a v1 encoder
      return PackEventCodes_v1(event_codes);
    }
    packed_event_codes |= ((static_cast<uint64_t>(code) & kEventCodeMask)
                           << (kV0EventCodeSize * i));
    i += 1;
  }
  return packed_event_codes;
}

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_PACKED_EVENT_CODES_H_

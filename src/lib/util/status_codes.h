// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_UTIL_STATUS_CODES_H_
#define COBALT_SRC_LIB_UTIL_STATUS_CODES_H_

#include <errno.h>

#include <optional>

namespace cobalt::util {

enum StatusCode {
  OK = 0,
  CANCELLED = 1,
  UNKNOWN = 2,
  INVALID_ARGUMENT = 3,
  DEADLINE_EXCEEDED = 4,
  NOT_FOUND = 5,
  ALREADY_EXISTS = 6,
  PERMISSION_DENIED = 7,
  RESOURCE_EXHAUSTED = 8,
  FAILED_PRECONDITION = 9,
  ABORTED = 10,
  OUT_OF_RANGE = 11,
  UNIMPLEMENTED = 12,
  INTERNAL = 13,
  UNAVAILABLE = 14,
  DATA_LOSS = 15,
  UNAUTHENTICATED = 16,

  DO_NOT_USE = -1,
};

// Tries to map a unix errno to a StatusCode.
//
// If the errno is 0, this will return StatusCode::OK. If there is no known mapping for the provided
// errno, std::nullopt will be returned.
std::optional<StatusCode> ErrnoToStatusCode(int error_number);

}  // namespace cobalt::util

#endif  // COBALT_SRC_LIB_UTIL_STATUS_CODES_H_

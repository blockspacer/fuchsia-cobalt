// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_STATUS_H_
#define COBALT_LOGGER_STATUS_H_

namespace cobalt {
namespace logger {

// Status codes returned by various methods in the logger package.
enum Status {
  // Success.
  kOK = 0,

  // Invalid arguments.
  kInvalidArguments = 1,

  // The Cobalt config is invalid.
  kInvalidConfig = 2,

  // The size of the proviced data is too large.
  kTooBig = 3,

  // The repository being written to is full.
  kFull = 4,

  // Other error.
  kOther = 5
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_STATUS_H_

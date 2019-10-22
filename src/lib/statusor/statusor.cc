// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/statusor/statusor.h"

#include "glog/logging.h"
#include "src/lib/util/status.h"

namespace cobalt::lib::statusor::internal_statusor {

void Helper::HandleInvalidStatusCtorArg(Status* status) {
  const char* kMessage = "An OK status is not a valid constructor argument to StatusOr<T>";
  LOG(ERROR) << kMessage;
  // Fall back to cobalt::util::StatusCode::INTERNAL.
  *status = cobalt::util::Status(cobalt::util::StatusCode::INTERNAL, kMessage);
}

void Helper::Crash(const Status& status) {
  LOG(FATAL) << "Attempting to fetch value instead of handling error " << status.error_message();
}

}  // namespace cobalt::lib::statusor::internal_statusor

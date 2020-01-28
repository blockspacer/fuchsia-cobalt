// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/util/status_codes.h"

namespace cobalt::util {

std::optional<StatusCode> ErrnoToStatusCode(int error_number) {
  switch (error_number) {
    case 0:
      return OK;

    case EINVAL:
      return INVALID_ARGUMENT;

    case ETIMEDOUT:
    case ETIME:
      return DEADLINE_EXCEEDED;

    case ENODEV:
    case ENOENT:
    case ENXIO:
    case ESRCH:
      return NOT_FOUND;

    case EEXIST:
    case EADDRNOTAVAIL:
    case EALREADY:
      return ALREADY_EXISTS;

    case EPERM:
    case EACCES:
    case EROFS:
      return PERMISSION_DENIED;

    case EMFILE:
    case ENFILE:
    case EMLINK:
      return RESOURCE_EXHAUSTED;

    case EFBIG:
    case EOVERFLOW:
    case ERANGE:
      return OUT_OF_RANGE;

    case ENOSYS:
    case ENOTSUP:
      return UNIMPLEMENTED;

    case EAGAIN:
      return UNAVAILABLE;

    case ECONNABORTED:
      return ABORTED;

    case ECANCELED:
      return CANCELLED;

    default:
      return std::nullopt;
  }
}

}  // namespace cobalt::util

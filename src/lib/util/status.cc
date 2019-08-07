// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/util/status.h"

namespace cobalt {
namespace util {

const Status& Status::OK = Status();
const Status& Status::CANCELLED = Status(StatusCode::CANCELLED, "");

}  // namespace util
}  // namespace cobalt

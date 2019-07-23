// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef COBALT_UTIL_FILE_UTIL_H_
#define COBALT_UTIL_FILE_UTIL_H_

#include <string>

#include "third_party/statusor/statusor.h"

namespace cobalt {
namespace util {

statusor::StatusOr<std::string> ReadTextFile(const std::string& file_path);

statusor::StatusOr<std::string> ReadNonEmptyTextFile(const std::string& file_path);

}  // namespace util
}  // namespace cobalt
#endif  // COBALT_UTIL_FILE_UTIL_H_

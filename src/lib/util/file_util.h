// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef COBALT_SRC_LIB_UTIL_FILE_UTIL_H_
#define COBALT_SRC_LIB_UTIL_FILE_UTIL_H_

#include <string>

#include "src/lib/statusor/statusor.h"

namespace cobalt {
namespace util {

lib::statusor::StatusOr<std::string> ReadTextFile(const std::string& file_path);

lib::statusor::StatusOr<std::string> ReadNonEmptyTextFile(const std::string& file_path);

lib::statusor::StatusOr<std::string> ReadHexFile(const std::string& file_path);

std::string ReadHexFileOrDefault(const std::string& file_path, const std::string& default_string);

}  // namespace util
}  // namespace cobalt
#endif  // COBALT_SRC_LIB_UTIL_FILE_UTIL_H_

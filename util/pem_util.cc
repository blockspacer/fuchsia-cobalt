// Copyright 2017 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "util/pem_util.h"

#include "./logging.h"
#include "util/file_util.h"

namespace cobalt::util {

bool PemUtil::ReadTextFile(const std::string& file_path,
                           std::string* file_contents) {
  if (!file_contents) {
    return false;
  }

  auto result = ::cobalt::util::ReadNonEmptyTextFile(file_path);
  if (!result.ok()) {
    // NOTE(rudominer) We use VLOG instead of LOG(ERROR) for any error messages
    // in Cobalt that may occur on the client side.
    VLOG(1) << result.status().error_message();
    return false;
  }

  *file_contents = result.ValueOrDie();
  return true;
}

}  // namespace cobalt::util

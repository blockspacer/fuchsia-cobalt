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

#ifndef COBALT_UTIL_PEM_UTIL_H_
#define COBALT_UTIL_PEM_UTIL_H_

#include <string>

namespace cobalt {
namespace util {

// PemUtil provides utilities for reading and writing PEM files.
class PemUtil {
 public:
  static const int kMaxFileSize = 100000;

  // Reads the text file at the specified path and writes the contents into
  // |*file_contents|. The file must contain at most |kMaxFileSize| bytes.
  // Returns true for success or false for failure.
  static bool ReadTextFile(const std::string& file_path, std::string* file_contents);
};

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_UTIL_PEM_UTIL_H_

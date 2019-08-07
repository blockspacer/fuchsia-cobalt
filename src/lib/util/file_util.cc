// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/lib/util/file_util.h"

#include <fstream>
#include <streambuf>
#include <string>

#include "./logging.h"
#include "src/lib/util/status.h"
#include "src/lib/util/status_codes.h"

namespace cobalt {
namespace util {

namespace {
constexpr int kMaxFileSize = 100000;
}

statusor::StatusOr<std::string> ReadTextFile(const std::string& file_path) {
  std::string file_contents;
  if (file_path.empty()) {
    return Status(INVALID_ARGUMENT, "ReadTextFile: file_path is empty.");
  }
  std::ifstream stream(file_path, std::ifstream::in);
  if (!stream.good()) {
    return Status(NOT_FOUND, "Unable to open file at " + file_path);
  }
  stream.seekg(0, std::ios::end);
  if (!stream.good()) {
    return Status(INTERNAL, "Error reading file at " + file_path);
  }
  auto file_size = stream.tellg();
  if (file_size == 0) {
    return std::string("");
  }
  // Don't try to read a file that's too big.
  if (!stream.good() || file_size < 0 || file_size > kMaxFileSize) {
    return Status(FAILED_PRECONDITION, "Invalid file length for " + file_path);
  }
  file_contents.reserve(file_size);

  stream.seekg(0, std::ios::beg);
  if (!stream.good()) {
    return Status(NOT_FOUND, "Error reading file at " + file_path);
  }
  file_contents.assign((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if (!stream.good()) {
    return Status(NOT_FOUND, "Error reading file at " + file_path);
  }
  VLOG(3) << "Successfully read file at " << file_path;
  return file_contents;
}

statusor::StatusOr<std::string> ReadNonEmptyTextFile(const std::string& file_path) {
  auto read_file_result = ReadTextFile(file_path);
  if (!read_file_result.ok()) {
    return read_file_result;
  }

  if (read_file_result.ValueOrDie().empty()) {
    return Status(FAILED_PRECONDITION, "File was empty: " + file_path);
  }

  return read_file_result;
}

}  // namespace util
}  // namespace cobalt

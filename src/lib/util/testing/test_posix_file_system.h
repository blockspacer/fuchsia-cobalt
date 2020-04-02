// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_UTIL_TESTING_TEST_POSIX_FILE_SYSTEM_H_
#define COBALT_SRC_LIB_UTIL_TESTING_TEST_POSIX_FILE_SYSTEM_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/lib/util/posix_file_system.h"

namespace cobalt::util::testing {

// TestPosixFileSystem wraps PosixFileSystem with additional bookkeeping to track files
// written/read.
class TestPosixFileSystem : public PosixFileSystem {
 public:
  // We will treat a rename as a "read" from the |from| filename, and a "write" to the |to|
  // filename.
  bool Rename(const std::string &from, const std::string &to) override {
    files_read_[from] += 1;
    files_written_[to] += 1;
    return PosixFileSystem::Rename(from, to);
  }

  using FileSystem::NewProtoInputStream;
  lib::statusor::StatusOr<ProtoInputStreamPtr> NewProtoInputStream(const std::string &file,
                                                                   int block_size) override {
    files_read_[file] += 1;
    return PosixFileSystem::NewProtoInputStream(file, block_size);
  }

  using FileSystem::NewProtoOutputStream;
  lib::statusor::StatusOr<ProtoOutputStreamPtr> NewProtoOutputStream(const std::string &file,
                                                                     bool append,
                                                                     int block_size) override {
    files_written_[file] += 1;
    return PosixFileSystem::NewProtoOutputStream(file, append, block_size);
  }

  // Returns the number of times a file has been written.
  //
  // |file| The absolute path of the file in question.
  uint32_t TimesWritten(const std::string &file) {
    if (files_written_.find(file) == files_written_.end()) {
      return 0;
    }

    return files_written_[file];
  }

  // Returns the number of times a file has been read.
  //
  // |file| The absolute path of the file in question.
  uint32_t TimesRead(const std::string &file) {
    if (files_read_.find(file) == files_read_.end()) {
      return 0;
    }

    return files_read_[file];
  }

 private:
  std::map<std::string, uint32_t> files_read_;
  std::map<std::string, uint32_t> files_written_;
};

}  // namespace cobalt::util::testing

#endif  // COBALT_SRC_LIB_UTIL_TESTING_TEST_POSIX_FILE_SYSTEM_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_UTIL_POSIX_FILE_SYSTEM_H_
#define COBALT_SRC_LIB_UTIL_POSIX_FILE_SYSTEM_H_

#include <memory>
#include <string>
#include <vector>

#include "src/lib/util/file_system.h"

namespace cobalt::util {

// PosixFileSystem implements FileSystem for posix compliant systems.
class PosixFileSystem : public FileSystem {
 public:
  bool MakeDirectory(const std::string &directory) override;
  statusor::StatusOr<std::vector<std::string>> ListFiles(const std::string &directory) override;
  bool Delete(const std::string &file) override;
  statusor::StatusOr<size_t> FileSize(const std::string &file) override;
  bool FileExists(const std::string &file) override;
  bool Rename(const std::string &from, const std::string &to) override;

  using FileSystem::NewProtoInputStream;
  statusor::StatusOr<ProtoInputStreamPtr> NewProtoInputStream(const std::string &file,
                                                              int block_size) override;

  using FileSystem::NewProtoOutputStream;
  statusor::StatusOr<ProtoOutputStreamPtr> NewProtoOutputStream(const std::string &file,
                                                                bool append,
                                                                int block_size) override;
};

}  // namespace cobalt::util

#endif  // COBALT_SRC_LIB_UTIL_POSIX_FILE_SYSTEM_H_

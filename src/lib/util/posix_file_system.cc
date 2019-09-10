// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/util/posix_file_system.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <map>
#include <memory>
#include <sstream>

#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "src/lib/util/file_system.h"

namespace cobalt::util {

using statusor::StatusOr;
using util::Status;
using util::StatusCode;

bool PosixFileSystem::MakeDirectory(const std::string &directory) {
  return mkdir(directory.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) == 0;
}

class CloseDir {
 public:
  void operator()(DIR *dir) { closedir(dir); }
};

StatusOr<std::vector<std::string>> PosixFileSystem::ListFiles(const std::string &directory) {
  std::vector<std::string> files;
  std::unique_ptr<DIR, CloseDir> dir(opendir(directory.c_str()), CloseDir());
  struct dirent *ent;
  if (dir != nullptr) {
    while ((ent = readdir(dir.get())) != nullptr) {
      if (ent->d_name[0] == '.' && (ent->d_name[1] == '.' || ent->d_name[1] == '\0')) {
        continue;
      }
      files.emplace_back(ent->d_name);
    }
  } else {
    std::stringstream ss;
    ss << "Unable to open directory [" << directory << "]: " << errno;
    return Status(StatusCode::INTERNAL, ss.str());
  }

  return files;
}

bool PosixFileSystem::Delete(const std::string &file) { return std::remove(file.c_str()) == 0; }

StatusOr<size_t> PosixFileSystem::FileSize(const std::string &file) {
  struct stat st;
  if (stat(file.c_str(), &st) != 0) {
    std::stringstream ss;
    ss << "Unable to stat file [" << file << "]: " << std::strerror(errno) << "[" << errno << "]";
    return Status(StatusCode::INTERNAL, ss.str());
  }
  return st.st_size;
}

bool PosixFileSystem::FileExists(const std::string &file) {
  struct stat buffer;
  return (stat(file.c_str(), &buffer) == 0);
}

bool PosixFileSystem::Rename(const std::string &from, const std::string &to) {
  return std::rename(from.c_str(), to.c_str()) == 0;
}

statusor::StatusOr<FileSystem::ProtoInputStreamPtr> PosixFileSystem::NewProtoInputStream(
    const std::string &file, int block_size) {
  auto fs = open(file.c_str(), O_RDWR);
  if (fs == -1) {
    std::stringstream ss;
    ss << "Unable to open file [" << file << "] in read mode.";
    return Status(StatusCode::DATA_LOSS, ss.str(), std::strerror(errno));
  }
  auto stream = std::make_unique<google::protobuf::io::FileInputStream>(fs, block_size);
  stream->SetCloseOnDelete(true);
  return StatusOr(std::move(stream));
}

statusor::StatusOr<FileSystem::ProtoOutputStreamPtr> PosixFileSystem::NewProtoOutputStream(
    const std::string &file, bool append, int block_size) {
  auto flags = O_WRONLY | O_CREAT;
  if (append) {
    flags |= O_APPEND;
  }
  auto fs = open(file.c_str(), flags, S_IRUSR | S_IWUSR);
  if (fs == -1) {
    std::stringstream ss;
    ss << "Unable to open file [" << file << "] in write mode.";
    return Status(StatusCode::DATA_LOSS, ss.str(), std::strerror(errno));
  }
  auto stream = std::make_unique<google::protobuf::io::FileOutputStream>(fs, block_size);
  stream->SetCloseOnDelete(true);
  return StatusOr(std::move(stream));
}

}  // namespace cobalt::util

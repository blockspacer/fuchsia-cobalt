// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_UTIL_FILE_SYSTEM_H_
#define COBALT_UTIL_FILE_SYSTEM_H_

#include <string>
#include <vector>

#include "third_party/statusor/statusor.h"

namespace cobalt {
namespace util {

// FileSystem is an abstract class used for interacting with the file system
// in a platform independent way.
class FileSystem {
 public:
  // MakeDirectory creates a directory on the file system.
  //
  // |directory|. An absolute path to the directory to be created.
  //
  // Returns: True if the directory was created successfully.
  virtual bool MakeDirectory(const std::string &directory) = 0;

  // ListFiles lists the files in a given directory.
  //
  // |directory|. An absolute path to the directory to list.
  //
  // Returns: A StatusOr with a vector of filenames. An Ok status indicates
  // that the list operation succeeded, even if the vector is empty.
  //
  // Note: On unix like systems, the directories "." and ".." should not be
  // returned.
  virtual statusor::StatusOr<std::vector<std::string>> ListFiles(const std::string &directory) = 0;

  // Delete deletes a file or an empty directory.
  //
  // |file|. An absolute path to the file or directory to be deleted.
  //
  // Returns: True if the file was successfully deleted.
  virtual bool Delete(const std::string &file) = 0;

  // FileSize returns the size of the |file| on disk.
  //
  // |file|. An absolute path to the file whose size is needed.
  //
  // Returns: A StatusOr containing the size of the file in bytes. An OK
  // status indicates that the FileSize operation succeeded, even if the
  // size_t is 0.
  virtual statusor::StatusOr<size_t> FileSize(const std::string &file) = 0;

  // FileExists returns true if the |file| exists.
  virtual bool FileExists(const std::string &file) = 0;

  // Rename renames a file.
  //
  // |from|. An absolute path to the file that is to be renamed.
  // |to|. An absolute path to the new name for the file.
  //
  // Returns: True if the file was renamed successfully.
  virtual bool Rename(const std::string &from, const std::string &to) = 0;

  virtual ~FileSystem() = default;
};

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_UTIL_FILE_SYSTEM_H_

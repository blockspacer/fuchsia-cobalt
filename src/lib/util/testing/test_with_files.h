// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_UTIL_TESTING_TEST_WITH_FILES_H_
#define COBALT_SRC_LIB_UTIL_TESTING_TEST_WITH_FILES_H_

#include "src/lib/util/testing/test_posix_file_system.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::util::testing {

// Filenames for constructors of ConsistentProtoStores
constexpr char kAggregateStoreFilename[] = "local_aggregate_store_backup";
constexpr char kObsHistoryFilename[] = "obs_history_backup";

// TestWithFiles is a test fixture to make testing using the filesystem easier.
//
// The class handles creating a temporary folder into which files can be written, and cleaning that
// folder up at the end of the test.h
//
// By default, SetUp calls MakeTestFolder, if SetUp is overridden, MakeTestFolder must be called
// before any calls to test_folder().
//
// On destruction, the temporary folder is deleted.
class TestWithFiles : public ::testing::Test {
 public:
  // Creates a new test folder, deleting the old one if present.
  void MakeTestFolder() {
    if (folder_made_) {
      DeleteTestFolder();
    }

    std::stringstream fname;
    fname << "/tmp/with_files_"
          << std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();

    test_folder_ = fname.str();

    fs_.MakeDirectory(test_folder_);

    folder_made_ = true;
  }

  ~TestWithFiles() override { DeleteTestFolder(); }

  void SetUp() override { MakeTestFolder(); }

  // Deletes the contents of the test folder, then the folder itself.
  void DeleteTestFolder() {
    if (!folder_made_) {
      return;
    }

    folder_made_ = false;
    for (const auto& file : fs_.ListFiles(test_folder_).ConsumeValueOr({})) {
      fs_.Delete(file);
    }
    fs_.Delete(test_folder_);
  }

  // Returns the instance of TestPosixFileSystem to be used by tests.
  TestPosixFileSystem* fs() { return &fs_; }

  // Returns an absolute path to the current test_folder.
  //
  // If MakeTestFolder has not yet been called, this method will fail.
  const std::string& test_folder() {
    EXPECT_TRUE(folder_made_) << "Test folder should be created before trying to access it.";
    return test_folder_;
  }

  // Helper methods for commonly used paths relative to test_folder().
  std::string aggregate_store_path() { return test_folder() + "/" + kAggregateStoreFilename; }
  std::string obs_history_path() { return test_folder() + "/" + kObsHistoryFilename; }

 private:
  bool folder_made_ = false;
  std::string test_folder_;
  TestPosixFileSystem fs_;
};

}  // namespace cobalt::util::testing

#endif  // COBALT_SRC_LIB_UTIL_TESTING_TEST_WITH_FILES_H_

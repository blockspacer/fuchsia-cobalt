// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/util/file_util.h"

#include "glog/logging.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace {

// Directory in which the test files can be found.
const char* kTestFilesDir;

// Test file names.
const char* kDeadbeefFile = "deadbeef";
const char* kEmptyFile = "empty";
const char* kNonExistentFile = "non_existent";
const char* kHexTooShortFile = "hex_too_short";
const char* kHexWrongCharFile = "hex_wrong_char";

const char* kDeadbeef = "\xde\xad\xbe\xef";

TEST(ReadTextFile, ReadFile) {
  std::string path = absl::StrCat(kTestFilesDir, kDeadbeefFile);
  auto read_result = cobalt::util::ReadTextFile(path);
  ASSERT_TRUE(read_result.ok());
  EXPECT_EQ(read_result.ValueOrDie(), "deadbeef\n");
}

TEST(ReadTextFile, ReadEmptyFile) {
  std::string path = absl::StrCat(kTestFilesDir, kEmptyFile);
  auto read_result = cobalt::util::ReadTextFile(path);
  ASSERT_TRUE(read_result.ok());
  EXPECT_EQ(read_result.ValueOrDie(), "");
}

TEST(ReadTextFile, ReadNonExistentFile) {
  std::string path = absl::StrCat(kTestFilesDir, kNonExistentFile);
  auto read_result = cobalt::util::ReadTextFile(path);
  ASSERT_FALSE(read_result.ok());
}

TEST(ReadNonEmptyTextFile, ReadFile) {
  std::string path = absl::StrCat(kTestFilesDir, kDeadbeefFile);
  auto read_result = cobalt::util::ReadNonEmptyTextFile(path);
  ASSERT_TRUE(read_result.ok());
  EXPECT_EQ(read_result.ValueOrDie(), "deadbeef\n");
}

TEST(ReadNonEmptyTextFile, ReadEmptyFile) {
  std::string path = absl::StrCat(kTestFilesDir, kEmptyFile);
  auto read_result = cobalt::util::ReadNonEmptyTextFile(path);
  EXPECT_FALSE(read_result.ok());
}

TEST(ReadNonEmtpyTextFile, ReadNonExistentFile) {
  std::string path = absl::StrCat(kTestFilesDir, kNonExistentFile);
  auto read_result = cobalt::util::ReadNonEmptyTextFile(path);
  EXPECT_FALSE(read_result.ok());
}

TEST(ReadHexFile, ReadHexFile) {
  std::string path = absl::StrCat(kTestFilesDir, kDeadbeefFile);
  auto read_result = cobalt::util::ReadHexFile(path);
  ASSERT_TRUE(read_result.ok());
  EXPECT_EQ(read_result.ValueOrDie(), kDeadbeef);
}

TEST(ReadHexFile, ReadEmptyFile) {
  std::string path = absl::StrCat(kTestFilesDir, kEmptyFile);
  auto read_result = cobalt::util::ReadHexFile(path);
  EXPECT_FALSE(read_result.ok());
}

TEST(ReadHexFile, ReadNonExistentFile) {
  std::string path = absl::StrCat(kTestFilesDir, kNonExistentFile);
  auto read_result = cobalt::util::ReadHexFile(path);
  EXPECT_FALSE(read_result.ok());
}

TEST(ReadHexFile, ReadHexTooShortFile) {
  std::string path = absl::StrCat(kTestFilesDir, kHexTooShortFile);
  auto read_result = cobalt::util::ReadHexFile(path);
  EXPECT_FALSE(read_result.ok());
}

TEST(ReadHexFile, ReadHexWrongCharFile) {
  std::string path = absl::StrCat(kTestFilesDir, kHexWrongCharFile);
  auto read_result = cobalt::util::ReadHexFile(path);
  EXPECT_FALSE(read_result.ok());
}

TEST(ReadHexFileOrDefault, ReadHexFile) {
  const std::string kDefault = "default";
  std::string path = absl::StrCat(kTestFilesDir, kDeadbeefFile);
  EXPECT_EQ(cobalt::util::ReadHexFileOrDefault(path, kDefault), kDeadbeef);
}

TEST(ReadHexFileOrDefault, ReadEmptyFile) {
  const std::string kDefault = "default";
  std::string path = absl::StrCat(kTestFilesDir, kEmptyFile);
  EXPECT_EQ(cobalt::util::ReadHexFileOrDefault(path, kDefault), kDefault);
}

TEST(ReadHexFileOrDefault, ReadNonExistentFile) {
  const std::string kDefault = "default";
  std::string path = absl::StrCat(kTestFilesDir, kNonExistentFile);
  EXPECT_EQ(cobalt::util::ReadHexFileOrDefault(path, kDefault), kDefault);
}

TEST(ReadHexFileOrDefault, ReadHexTooShortFile) {
  const std::string kDefault = "default";
  std::string path = absl::StrCat(kTestFilesDir, kHexTooShortFile);
  EXPECT_EQ(cobalt::util::ReadHexFileOrDefault(path, kDefault), kDefault);
}

TEST(ReadHexFileOrDefault, ReadHexWrongCharFile) {
  const std::string kDefault = "default";
  std::string path = absl::StrCat(kTestFilesDir, kHexWrongCharFile);
  EXPECT_EQ(cobalt::util::ReadHexFileOrDefault(path, kDefault), kDefault);
}

}  // namespace

int main(int argc, char* argv[]) {
  // Compute the path where keys are stored in the output directory.
  std::string path(argv[0]);
  auto slash = path.find_last_of('/');
  CHECK(slash != std::string::npos);
  path = path.substr(0, slash) + "/file_util_test_files/";
  kTestFilesDir = path.c_str();

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

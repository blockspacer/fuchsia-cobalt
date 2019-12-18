// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/util/consistent_proto_store.h"

#include <fstream>

#include "src/lib/util/consistent_proto_store_test.pb.h"
#include "src/lib/util/posix_file_system.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::util {

int path_suffix = 0;
constexpr char test_dir_base[] = "/tmp/cps_test";

const Status fake_fail(StatusCode::INTERNAL, "BAD", "BAD");

std::string GetTestDirName(const std::string &base) {
  std::stringstream fname;
  fname << base << "_"
        << std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
               .count()
        << "_" << path_suffix++;
  return fname.str();
}

class TestConsistentProtoStore : public ConsistentProtoStore {
 public:
  explicit TestConsistentProtoStore(const std::string &filename)
      : ConsistentProtoStore(filename, &fs_),
        fail_write_tmp_(false),
        fail_move_tmp_(false),
        fail_delete_primary_(false),
        fail_move_override_to_primary_(false) {}

  ~TestConsistentProtoStore() override = default;

  void FailNextWriteToTmp() { fail_write_tmp_ = true; }
  void FailNextMoveTmpToOverride() { fail_move_tmp_ = true; }
  void FailNextDeletePrimary() { fail_delete_primary_ = true; }
  void FailNextMoveOverrideToPrimary() { fail_move_override_to_primary_ = true; }

 private:
  Status WriteToTmp(const google::protobuf::MessageLite &proto) override {
    if (fail_write_tmp_) {
      fail_write_tmp_ = false;
      return fake_fail;
    }
    return ConsistentProtoStore::WriteToTmp(proto);
  }

  Status MoveTmpToOverride() override {
    if (fail_move_tmp_) {
      fail_move_tmp_ = false;
      return fake_fail;
    }
    return ConsistentProtoStore::MoveTmpToOverride();
  }

  Status DeletePrimary() override {
    if (fail_delete_primary_) {
      fail_delete_primary_ = false;
      return fake_fail;
    }
    return ConsistentProtoStore::DeletePrimary();
  }

  Status MoveOverrideToPrimary() override {
    if (fail_move_override_to_primary_) {
      fail_move_override_to_primary_ = false;
      return fake_fail;
    }
    return ConsistentProtoStore::MoveOverrideToPrimary();
  }

  PosixFileSystem fs_;
  bool fail_write_tmp_;
  bool fail_move_tmp_;
  bool fail_delete_primary_;
  bool fail_move_override_to_primary_;
};

class ConsistentProtoStoreTest : public ::testing::Test {
 public:
  ConsistentProtoStoreTest()
      : directory_(GetTestDirName(test_dir_base)), store_(directory_ + "/Proto") {}

  void Mkdir() {
    PosixFileSystem fs;
    fs.MakeDirectory(directory_);
  }

  void TearDown() override {
    // Clean up the directory
    PosixFileSystem fs;
    auto files = fs.ListFiles(directory_).ConsumeValueOr({});
    for (const auto &file : files) {
      fs.Delete(directory_ + "/" + file);
    }
    fs.Delete(directory_);
  }

 protected:
  std::string directory_;
  TestConsistentProtoStore store_;
};

TEST_F(ConsistentProtoStoreTest, DirectoryMissing) {
  TestProto p;
  p.set_b(true);
  auto stat = store_.Write(p);

  EXPECT_FALSE(stat.ok());
  EXPECT_EQ(stat.error_details(), "No such file or directory");

  stat = store_.Read(&p);
  EXPECT_FALSE(stat.ok());
  EXPECT_EQ(stat.error_details(), "No such file or directory");
}

TEST_F(ConsistentProtoStoreTest, NoFileToRead) {
  Mkdir();
  TestProto p;
  auto stat = store_.Read(&p);

  EXPECT_FALSE(stat.ok());
  EXPECT_EQ(stat.error_details(), "No such file or directory");
}

TEST_F(ConsistentProtoStoreTest, Normal) {
  Mkdir();
  TestProto pin, pout;
  pin.set_b(true);
  pin.set_i(42);
  pin.set_s("Data!");
  auto stat = store_.Write(pin);
  EXPECT_TRUE(stat.ok());
  EXPECT_EQ(stat.error_message(), "");
  EXPECT_EQ(stat.error_details(), "");

  stat = store_.Read(&pout);
  EXPECT_TRUE(stat.ok());
  EXPECT_EQ(stat.error_message(), "");
  EXPECT_EQ(stat.error_details(), "");

  EXPECT_EQ(pout.b(), true);
  EXPECT_EQ(pout.i(), 42);
  EXPECT_EQ(pout.s(), "Data!");
}

TEST_F(ConsistentProtoStoreTest, ReadCompatible) {
  Mkdir();
  TestProto pin;
  CompatibleProto pout;
  pin.set_b(true);
  pin.set_i(44);
  auto stat = store_.Write(pin);
  EXPECT_TRUE(stat.ok());
  EXPECT_EQ(stat.error_message(), "");
  EXPECT_EQ(stat.error_details(), "");

  stat = store_.Read(&pout);
  EXPECT_TRUE(stat.ok());
  EXPECT_EQ(stat.error_message(), "");
  EXPECT_EQ(stat.error_details(), "");

  EXPECT_EQ(pout.a_boolean(), true);
}

TEST_F(ConsistentProtoStoreTest, ReadIncompatible) {
  Mkdir();
  TestProto pin;
  IncompatibleProto pout;
  pin.set_s("strang");
  auto stat = store_.Write(pin);
  EXPECT_TRUE(stat.ok());
  EXPECT_EQ(stat.error_message(), "");
  EXPECT_EQ(stat.error_details(), "");

  // Reading an incompatible proto works, but the data is wrong.
  stat = store_.Read(&pout);
  EXPECT_TRUE(stat.ok());
  EXPECT_EQ(stat.error_message(), "");
  EXPECT_EQ(stat.error_details(), "");

  EXPECT_NE(pout.s(), "strang");
}

TEST_F(ConsistentProtoStoreTest, ReadCorrupt) {
  Mkdir();
  {
    std::ofstream f(directory_ + "/Proto");
    f << "Invalid data and stuff";
  }

  TestProto p;
  auto stat = store_.Read(&p);
  EXPECT_FALSE(stat.ok());
  EXPECT_EQ(stat.error_message(), "Unable to parse the protobuf from the store. Data is corrupt.");
  EXPECT_EQ(stat.error_details(), "");
}

TEST_F(ConsistentProtoStoreTest, TestFailures) {
  Mkdir();

  TestProto p, pread;
  p.set_b(true);
  p.set_i(10000);
  p.set_s("testing123");

  store_.FailNextWriteToTmp();
  p.set_i(p.i() + 1);
  EXPECT_FALSE(store_.Write(p).ok());
  EXPECT_FALSE(store_.Read(&pread).ok());

  store_.FailNextMoveTmpToOverride();
  p.set_i(p.i() + 1);
  EXPECT_FALSE(store_.Write(p).ok());
  EXPECT_FALSE(store_.Read(&pread).ok());

  store_.FailNextDeletePrimary();
  p.set_i(p.i() + 1);
  EXPECT_FALSE(store_.Write(p).ok());
  // The read should succeed since the tmp file was renamed to new file.
  EXPECT_TRUE(store_.Read(&pread).ok());
  EXPECT_EQ(pread.i(), p.i());

  store_.FailNextDeletePrimary();
  p.set_i(p.i() + 1);
  // This write should succeed, since the delete will fail during recovery,
  // which is ignored.
  EXPECT_TRUE(store_.Write(p).ok());
  EXPECT_TRUE(store_.Read(&pread).ok());
  EXPECT_EQ(pread.i(), p.i());

  store_.FailNextMoveOverrideToPrimary();
  p.set_i(p.i() + 1);
  EXPECT_FALSE(store_.Write(p).ok());
  // A failed finalize should still update the value in the store.
  EXPECT_TRUE(store_.Read(&pread).ok());
  EXPECT_EQ(pread.i(), p.i());

  store_.FailNextMoveTmpToOverride();
  p.set_i(p.i() + 1);
  EXPECT_FALSE(store_.Write(p).ok());
  // If we fail te rename tmp, the value in the store shouldn't change, but it
  // should still be valid.
  EXPECT_TRUE(store_.Read(&pread).ok());
  EXPECT_EQ(pread.i(), p.i() - 1);
}

}  // namespace cobalt::util

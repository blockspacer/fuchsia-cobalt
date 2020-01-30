// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "glog/logging.h"
#include "proto/tink.pb.h"
#include "src/lib/util/encrypted_message_util.h"
#include "src/lib/util/file_util.h"
#include "src/pb/encrypted_message.pb.h"
#include "src/pb/observation2.pb.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

// Does some sanity-checking on the tink keys.

namespace {

// Directory in which the keys can be found.
const char* kKeysDir;

// Read the key from disk.
void ReadKey(const std::string& filename, std::string* key_bytes) {
  std::string path = kKeysDir + filename;
  auto key_bytes_result = cobalt::util::ReadNonEmptyTextFile(path);
  ASSERT_TRUE(key_bytes_result.ok());

  *key_bytes = key_bytes_result.ValueOrDie();
}

// Encrypt some non-empty proto using the supplied EncryptedMessageMaker.
void EncryptSomething(cobalt::util::EncryptedMessageMaker* maker,
                      cobalt::EncryptedMessage* encrypted_message) {
  cobalt::Observation2 observation;
  observation.set_random_id("random id");

  ASSERT_TRUE(maker->Encrypt(observation, encrypted_message));
}

TEST(KeysTests, TestShufflerCobaltEncryptionDevKey) {
  std::string key_bytes;
  ReadKey("shuffler_public.cobalt_key", &key_bytes);

  auto maker_result = cobalt::util::EncryptedMessageMaker::MakeForEnvelopes(key_bytes);
  ASSERT_TRUE(maker_result.ok());

  cobalt::EncryptedMessage encrypted_message;
  EncryptSomething(maker_result.ValueOrDie().get(), &encrypted_message);

  EXPECT_EQ(cobalt::EncryptedMessage::NONE, encrypted_message.scheme());
  EXPECT_EQ(encrypted_message.key_index(), 1u);
}

TEST(KeysTests, TestAnalyzerCobaltEncryptionDevKey) {
  std::string key_bytes;
  ReadKey("analyzer_public.cobalt_key", &key_bytes);

  auto maker_result = cobalt::util::EncryptedMessageMaker::MakeForObservations(key_bytes);
  ASSERT_TRUE(maker_result.ok());

  cobalt::EncryptedMessage encrypted_message;
  EncryptSomething(maker_result.ValueOrDie().get(), &encrypted_message);

  EXPECT_EQ(cobalt::EncryptedMessage::NONE, encrypted_message.scheme());
  EXPECT_EQ(encrypted_message.key_index(), 2u);
}
TEST(KeysTests, TestShufflerCobaltEncryptionProdKey) {
  std::string key_bytes;
  ReadKey("shuffler_prod_public.cobalt_key", &key_bytes);

  auto maker_result = cobalt::util::EncryptedMessageMaker::MakeForEnvelopes(key_bytes);
  ASSERT_TRUE(maker_result.ok());

  cobalt::EncryptedMessage encrypted_message;
  EncryptSomething(maker_result.ValueOrDie().get(), &encrypted_message);

  EXPECT_EQ(cobalt::EncryptedMessage::NONE, encrypted_message.scheme());
  EXPECT_EQ(encrypted_message.key_index(), 3u);
}

TEST(KeysTests, TestAnalyzerCobaltEncryptionProdKey) {
  std::string key_bytes;
  ReadKey("analyzer_prod_public.cobalt_key", &key_bytes);

  auto maker_result = cobalt::util::EncryptedMessageMaker::MakeForObservations(key_bytes);
  ASSERT_TRUE(maker_result.ok());

  cobalt::EncryptedMessage encrypted_message;
  EncryptSomething(maker_result.ValueOrDie().get(), &encrypted_message);

  EXPECT_EQ(cobalt::EncryptedMessage::NONE, encrypted_message.scheme());
  EXPECT_EQ(encrypted_message.key_index(), 4u);
}

}  // namespace

int main(int argc, char* argv[]) {
  // Compute the path where keys are stored in the output directory.
  std::string path(argv[0]);
  auto slash = path.find_last_of('/');
  CHECK(slash != std::string::npos);
  path = path.substr(0, slash) + "/keys/";
  kKeysDir = path.c_str();

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "./encrypted_message.pb.h"
#include "./observation.pb.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "third_party/tink/include/proto/tink.pb.h"
#include "util/crypto_util/base64.h"
#include "util/encrypted_message_util.h"
#include "util/file_util.h"

// Does some sanity-checking on the tink keys.

namespace {

const char* kKeysDir;

class KeyTests : public ::testing::TestWithParam<std::string> {};

// Check that the keys can be used to encrypt an observation.
TEST_P(KeyTests, TestEncryption) {
  std::string path = kKeysDir + GetParam() + "_public_key.tink";
  auto keyset_result = cobalt::util::ReadNonEmptyTextFile(path);
  ASSERT_TRUE(keyset_result.ok());
  auto keyset = keyset_result.ValueOrDie();

  auto maker_result =
      cobalt::util::EncryptedMessageMaker::MakeHybridTink(keyset);
  ASSERT_TRUE(maker_result.ok());
  auto maker = std::move(maker_result.ValueOrDie());

  cobalt::Observation observation;
  (*observation.mutable_parts())["some_part"] = cobalt::ObservationPart();

  cobalt::EncryptedMessage encrypted_message;
  ASSERT_TRUE(maker->Encrypt(observation, &encrypted_message));

  EXPECT_EQ(cobalt::EncryptedMessage::HYBRID_TINK, encrypted_message.scheme());
  EXPECT_FALSE(encrypted_message.ciphertext().empty());
}

std::string GetTestParam(::testing::TestParamInfo<std::string> param_info) {
  return param_info.param + "_key";
}

INSTANTIATE_TEST_CASE_P(KeysTests, KeyTests,
                        ::testing::Values("analyzer", "shuffler"),
                        GetTestParam);

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

// Copyright 2017 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "encoder/shuffler_client.h"

#include <memory>
#include <string>
#include <utility>

#include "./encrypted_message.pb.h"
#include "./gtest.h"
#include "./logging.h"
#include "third_party/gflags/include/gflags/gflags.h"

namespace cobalt {
namespace encoder {

class ShufflerClientTest : public ::testing::Test {
 public:
  ShufflerClientTest() {}

 protected:
};

// This is just a smoke test of ShufflerClient. There is not very much we
// can test in the unit test environment--since ShufflerClient is a thin wrapper
// around gRPC there is no sensible place to insert a mock. ShufflerClient is
// thouroughly tested by our integration tests.
TEST_F(ShufflerClientTest, SmokeTest) {
  static const std::string kUri = "www.not.really.a.uri";
  static const EncryptedMessage encrypted_message;

  bool use_tls = false;
  std::unique_ptr<ShufflerClient> shuffler_client(
      new ShufflerClient(kUri, use_tls));

  // Since this is a unit test and we are not mocking the gRPC connection
  // and there is no actual Shuffler service to connect to we expect
  // SendToShuffler() to fail. Here we are only testing that it fails in
  // the expected way. We set the gRPC timeout to the current time and
  // expect an error. Note that if we don't set a timeout then the call hangs
  // forever.
  std::unique_ptr<grpc::ClientContext> context(new grpc::ClientContext());
  context->set_deadline(std::chrono::system_clock::now());
  auto status =
      shuffler_client->SendToShuffler(encrypted_message, context.get());
  EXPECT_NE(grpc::OK, status.error_code());

  // Try again with use_tls = true.
  use_tls = true;
  // A context can only be used once.
  context.reset(new grpc::ClientContext());
  context->set_deadline(std::chrono::system_clock::now());
  status =
      shuffler_client->SendToShuffler(encrypted_message, context.get());
  EXPECT_NE(grpc::OK, status.error_code());
}

}  // namespace encoder
}  // namespace cobalt

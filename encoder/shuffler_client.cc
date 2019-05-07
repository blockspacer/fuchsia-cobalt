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

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "encoder/shuffler_client.h"

namespace cobalt {
namespace encoder {

using shuffler::Shuffler;

namespace {

std::shared_ptr<grpc::ChannelCredentials> CreateChannelCredentials(
    bool use_tls, const char* pem_root_certs) {
  if (use_tls) {
    auto opts = grpc::SslCredentialsOptions();
    if (pem_root_certs) {
      opts.pem_root_certs = pem_root_certs;
    }
    return grpc::SslCredentials(opts);
  } else {
    return grpc::InsecureChannelCredentials();
  }
}

}  // namespace

ShufflerClient::ShufflerClient(const std::string& uri, bool use_tls,
                               const char* pem_root_certs)
    : shuffler_stub_(Shuffler::NewStub(grpc::CreateChannel(
          uri, CreateChannelCredentials(use_tls, pem_root_certs)))) {}

grpc::Status ShufflerClient::SendToShuffler(
    const EncryptedMessage& encrypted_message, grpc::ClientContext* context) {
  // Cobalt v0.1 is diabled.
  return grpc::Status::OK;
}

}  // namespace encoder
}  // namespace cobalt

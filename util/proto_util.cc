// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/proto_util.h"

using ::google::protobuf::MessageLite;

namespace cobalt {

using crypto::Base64Encode;

namespace util {

bool SerializeToBase64(const MessageLite& message, std::string* encoded_message) {
  std::string serialized_message;
  if (!message.SerializeToString(&serialized_message)) {
    LOG(ERROR) << "Failed to serialize proto message.";
    return false;
  }
  if (!Base64Encode(serialized_message, encoded_message)) {
    LOG(ERROR) << "Failed to base64-encode serialized message.";
    return false;
  }
  return true;
}

}  // namespace util
}  // namespace cobalt

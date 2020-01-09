// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/util/proto_util.h"

#include "third_party/abseil-cpp/absl/strings/escaping.h"

using ::google::protobuf::MessageLite;

namespace cobalt::util {

bool SerializeToBase64(const MessageLite& message, std::string* encoded_message) {
  std::string serialized_message;
  if (!message.SerializeToString(&serialized_message)) {
    LOG(ERROR) << "Failed to serialize proto message.";
    return false;
  }

  absl::Base64Escape(serialized_message, encoded_message);
  return true;
}

}  // namespace cobalt::util

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_CLEARCUT_LOGGER_H_
#define THIRD_PARTY_CLEARCUT_LOGGER_H_

#include <chrono>
#include <memory>
#include <vector>

#include "src/lib/util/status.h"
#include "third_party/abseil-cpp/absl/strings/escaping.h"
#include "third_party/clearcut/clearcut.pb.h"
#include "third_party/clearcut/http_client.h"
#include "third_party/statusor/statusor.h"

namespace clearcut {

static const int32_t kClearcutDemoSource = 177;
static const int32_t kFuchsiaCobaltShufflerInputDevel = 844;

static const int32_t kFuchsiaClientType = 17;
static const int32_t kMaxRetries = 5;

// A ClearcutUploader sends events to clearcut using the given HTTPClient.
//
// Note: This class is not threadsafe.
class ClearcutUploader {
 public:
  ClearcutUploader(const std::string &url, std::unique_ptr<HTTPClient> client,
                   int64_t upload_timeout = 0, int64_t initial_backoff_millis = 250);

  // Uploads the |log_request|  with retries.
  Status UploadEvents(LogRequest *log_request, int32_t max_retries = kMaxRetries);

 private:
  // Tries once to upload |log_request|.
  Status TryUploadEvents(LogRequest *log_request, std::chrono::steady_clock::time_point deadline);

  const std::string url_;
  const std::unique_ptr<HTTPClient> client_;
  const int64_t upload_timeout_;
  const int64_t initial_backoff_millis_;

 protected:
  friend class UploaderTest;

  // When we get a next_request_wait_millis from the clearcut server, we set
  // this value to now() + next_request_wait_millis.
  std::chrono::steady_clock::time_point pause_uploads_until_;
};

}  // namespace clearcut

#endif  // THIRD_PARTY_CLEARCUT_LOGGER_H_

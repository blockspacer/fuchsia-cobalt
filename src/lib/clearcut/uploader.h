// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_CLEARCUT_UPLOADER_H_
#define COBALT_SRC_LIB_CLEARCUT_UPLOADER_H_

#include <chrono>
#include <memory>
#include <vector>

#include "src/lib/clearcut/clearcut.pb.h"
#include "src/lib/clearcut/http_client.h"
#include "src/lib/util/clock.h"
#include "src/lib/util/sleeper.h"
#include "src/lib/util/status.h"
#include "third_party/abseil-cpp/absl/strings/escaping.h"
#include "third_party/statusor/statusor.h"

namespace cobalt::lib::clearcut {

static const int32_t kFuchsiaClientType = 17;
static const int32_t kMaxRetries = 8;  // Wait between tries: 0.25s 0.5.s 1s 2s 4s 8s 16s
static const int64_t kInitialBackoffMillis = 250;

// A ClearcutUploader sends events to clearcut using the given HTTPClient.
//
// Note: This class is not threadsafe.
class ClearcutUploader {
 public:
  // url: The URL of the Clearcut server to which POST requests should be sent.
  //
  // client: An implementation of HTTPClient to use.
  //
  // upload_timeout_millis: If a positive value is specified then this number of milliseconds
  //  will be used as the HTTP request timeout. Otherwise no timeout will be used.
  //
  // initial_backoff_millis: The initial value to use for exponential backoff, in milliseconds.
  //   This is used when the ClearcutUploader retries on failures. Optionally override the default.
  //   Mostly useful for tests.
  //
  // steady_clock: Optionally replace the system steady clock. Mostly useful for tests.
  //
  // sleeper: Optionally replace the system sleep. Mostly useful for tests.
  ClearcutUploader(std::string url, std::unique_ptr<HTTPClient> client,
                   int64_t upload_timeout_millis = 0,
                   int64_t initial_backoff_millis = kInitialBackoffMillis,
                   std::unique_ptr<cobalt::util::SteadyClockInterface> steady_clock =
                       std::make_unique<cobalt::util::SteadyClock>(),
                   std::unique_ptr<cobalt::util::SleeperInterface> sleeper =
                       std::make_unique<cobalt::util::Sleeper>());

  // Uploads the |log_request|  with retries.
  Status UploadEvents(LogRequest *log_request, int32_t max_retries = kMaxRetries);

 private:
  // Tries once to upload |log_request|.
  Status TryUploadEvents(LogRequest *log_request, std::chrono::steady_clock::time_point deadline);

  const std::string url_;
  const std::unique_ptr<HTTPClient> client_;
  const std::chrono::milliseconds upload_timeout_;
  const std::chrono::milliseconds initial_backoff_;

  const std::unique_ptr<cobalt::util::SteadyClockInterface> steady_clock_;
  const std::unique_ptr<cobalt::util::SleeperInterface> sleeper_;

  friend class UploaderTest;

  // When we get a next_request_wait_millis from the clearcut server, we set
  // this value to now() + next_request_wait_millis.
  std::chrono::steady_clock::time_point pause_uploads_until_;
};

}  // namespace cobalt::lib::clearcut

#endif  // COBALT_SRC_LIB_CLEARCUT_UPLOADER_H_

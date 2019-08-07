// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/clearcut/uploader.h"

#include <chrono>
#include <thread>

#include "src/gtest.h"
#include "src/logging.h"
#include "third_party/clearcut/clearcut.pb.h"
#include "third_party/gflags/include/gflags/gflags.h"

namespace clearcut {

using cobalt::util::StatusCode;

namespace {

static const int64_t kInitialBackoffMillis = 10;

static std::set<uint32_t> seen_event_codes;
static int next_request_wait_millis;
static bool fail_next_request;

class TestHTTPClient : public HTTPClient {
 public:
  std::future<StatusOr<HTTPResponse>> Post(HTTPRequest request,
                                           std::chrono::steady_clock::time_point _ignored) {
    if (fail_next_request) {
      fail_next_request = false;
      std::promise<StatusOr<HTTPResponse>> response_promise;
      response_promise.set_value(Status(StatusCode::DEADLINE_EXCEEDED, "Artificial post failure"));
      return response_promise.get_future();
    }

    LogRequest req;
    req.ParseFromString(request.body);
    for (auto event : req.log_event()) {
      seen_event_codes.insert(event.event_code());
    }

    HTTPResponse response;
    response.http_code = 200;
    LogResponse resp;
    if (next_request_wait_millis != -1) {
      resp.set_next_request_wait_millis(next_request_wait_millis);
    }
    resp.SerializeToString(&response.response);

    std::promise<StatusOr<HTTPResponse>> response_promise;
    response_promise.set_value(std::move(response));

    return response_promise.get_future();
  }
};

}  // namespace

class UploaderTest : public ::testing::Test {
  void SetUp() {
    seen_event_codes = {};
    next_request_wait_millis = -1;
    fail_next_request = false;
    auto unique_client = std::make_unique<TestHTTPClient>();
    client = unique_client.get();
    uploader = std::make_unique<ClearcutUploader>("http://test.com", std::move(unique_client), 0,
                                                  kInitialBackoffMillis);
  }

 public:
  Status UploadClearcutDemoEvent(uint32_t event_code, int32_t max_retries = 1) {
    LogRequest request;
    request.set_log_source(kClearcutDemoSource);
    request.add_log_event()->set_event_code(event_code);
    return uploader->UploadEvents(&request, max_retries);
  }

  std::chrono::steady_clock::time_point get_pause_uploads_until() {
    return uploader->pause_uploads_until_;
  }

  bool SawEventCode(uint32_t event_code) {
    return seen_event_codes.find(event_code) != seen_event_codes.end();
  }

  std::unique_ptr<ClearcutUploader> uploader;
  TestHTTPClient *client;
};

TEST_F(UploaderTest, BasicClearcutDemoUpload) {
  ASSERT_TRUE(UploadClearcutDemoEvent(1).ok());
  ASSERT_TRUE(UploadClearcutDemoEvent(2).ok());
  ASSERT_TRUE(UploadClearcutDemoEvent(3).ok());
  ASSERT_TRUE(UploadClearcutDemoEvent(4).ok());

  ASSERT_TRUE(SawEventCode(1));
  ASSERT_TRUE(SawEventCode(2));
  ASSERT_TRUE(SawEventCode(3));
  ASSERT_TRUE(SawEventCode(4));
}

TEST_F(UploaderTest, RateLimitingWorks) {
  next_request_wait_millis = 10;
  ASSERT_TRUE(UploadClearcutDemoEvent(100).ok());
  ASSERT_TRUE(SawEventCode(100));

  int code = 150;
  // Repeatedly try to upload until we pass the uploader's
  // "pause_uploads_until_" field.
  while (std::chrono::steady_clock::now() < get_pause_uploads_until()) {
    ASSERT_FALSE(UploadClearcutDemoEvent(code).ok());

    // We haven't waited long enough yet.
    ASSERT_FALSE(SawEventCode(code));

    code++;

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Assert that we've made at least 1 rate limit verification pass.
  ASSERT_GT(code, 150);

  // Now that the pause has expired, we should be able to upload.
  ASSERT_TRUE(UploadClearcutDemoEvent(code).ok());

  // Now it should work.
  ASSERT_TRUE(SawEventCode(code));
}

TEST_F(UploaderTest, ShouldRetryOnFailedUpload) {
  fail_next_request = true;
  ASSERT_TRUE(UploadClearcutDemoEvent(1, 2).ok());
  ASSERT_TRUE(SawEventCode(1));

  fail_next_request = true;
  ASSERT_FALSE(UploadClearcutDemoEvent(2).ok());
  ASSERT_TRUE(UploadClearcutDemoEvent(3).ok());
  ASSERT_TRUE(SawEventCode(3));
}

}  // namespace clearcut

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  INIT_LOGGING(argv[0]);
  return RUN_ALL_TESTS();
}

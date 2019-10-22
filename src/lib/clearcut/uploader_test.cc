// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/clearcut/uploader.h"

#include <chrono>
#include <thread>

#include "src/lib/clearcut/clearcut.pb.h"
#include "src/lib/util/clock.h"
#include "src/lib/util/sleeper.h"
#include "src/logging.h"
#include "third_party/gflags/include/gflags/gflags.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::lib::clearcut {

using cobalt::util::FakeSleeper;
using cobalt::util::IncrementingSteadyClock;
using cobalt::util::StatusCode;
using std::chrono::steady_clock;

namespace {

constexpr char kFakeClearcutURL[] = "http://test.com";
constexpr int64_t kUploadTimeoutMillis = 31 * 1000;
constexpr int64_t kInitialBackoffMillisForTest = 250;
constexpr std::chrono::milliseconds kFakeClockIncrement(1);

class FakeHttpClient : public HTTPClient {
 public:
  std::future<StatusOr<HTTPResponse>> Post(
      HTTPRequest request, std::chrono::steady_clock::time_point deadline) override {
    EXPECT_EQ(request.url, kFakeClearcutURL);
    last_deadline_seen = deadline;
    EXPECT_EQ(0, request.headers.size());
    if (!error_statuses_to_return.empty()) {
      auto next_status = error_statuses_to_return.front();
      error_statuses_to_return.pop_front();
      std::promise<StatusOr<HTTPResponse>> response_promise;
      response_promise.set_value(Status(next_status, "Artificial post failure"));
      return response_promise.get_future();
    }

    LogRequest req;
    req.ParseFromString(request.body);
    for (auto &event : req.log_event()) {
      seen_event_codes.insert(event.event_code());
    }

    HTTPResponse response;
    int http_response_code = 200;
    if (!http_response_codes_to_return.empty()) {
      http_response_code = http_response_codes_to_return.front();
      http_response_codes_to_return.pop_front();
    }
    response.http_code = http_response_code;
    response.headers["Size"] = "LARGE";
    response.headers["Color"] = "PURPLE";
    LogResponse resp;
    if (next_request_wait_millis != -1) {
      resp.set_next_request_wait_millis(next_request_wait_millis);
    }
    resp.SerializeToString(&response.response);

    std::promise<StatusOr<HTTPResponse>> response_promise;
    response_promise.set_value(std::move(response));

    return response_promise.get_future();
  }

  std::set<uint32_t> seen_event_codes = {};
  int next_request_wait_millis = -1;
  std::chrono::steady_clock::time_point last_deadline_seen;
  std::deque<StatusCode> error_statuses_to_return;
  std::deque<int> http_response_codes_to_return;
};

}  // namespace

class UploaderTest : public ::testing::Test {
 protected:
  void DoSetUp(int64_t upload_timeout_millis) {
    auto unique_client = std::make_unique<FakeHttpClient>();
    client = unique_client.get();
    auto unique_clock = std::make_unique<IncrementingSteadyClock>(kFakeClockIncrement);
    fake_clock = unique_clock.get();
    auto unique_sleeper = std::make_unique<FakeSleeper>();
    fake_sleeper = unique_sleeper.get();
    fake_sleeper->set_incrementing_steady_clock(fake_clock);
    uploader = std::make_unique<ClearcutUploader>(
        kFakeClearcutURL, std::move(unique_client), upload_timeout_millis,
        kInitialBackoffMillisForTest, std::move(unique_clock), std::move(unique_sleeper));
  }

  void SetUp() override { DoSetUp(kUploadTimeoutMillis); }

 public:
  Status UploadClearcutDemoEvent(uint32_t event_code, int32_t max_retries = 1) {
    LogRequest request;
    constexpr int32_t kClearcutDemoSource = 12345;
    request.set_log_source(kClearcutDemoSource);
    request.add_log_event()->set_event_code(event_code);
    auto status = uploader->UploadEvents(&request, max_retries);
    return status;
  }

  std::chrono::steady_clock::time_point get_pause_uploads_until() {
    return uploader->pause_uploads_until_;
  }

  bool SawEventCode(uint32_t event_code) {
    return client->seen_event_codes.find(event_code) != client->seen_event_codes.end();
  }

  std::unique_ptr<ClearcutUploader> uploader;
  FakeHttpClient *client;
  IncrementingSteadyClock *fake_clock;
  FakeSleeper *fake_sleeper;
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

// Tests that the correct value for the timeout is passed to the HttpClient.
TEST_F(UploaderTest, CorrectDeadlineUsed) {
  auto expected_deadline =
      fake_clock->peek_later() + std::chrono::milliseconds(kUploadTimeoutMillis);
  ASSERT_TRUE(UploadClearcutDemoEvent(1).ok());
  EXPECT_EQ(client->last_deadline_seen, expected_deadline);

  // Advance time by 10 minutes.
  fake_clock->increment_by(std::chrono::minutes(10));
  expected_deadline = fake_clock->peek_later() + std::chrono::milliseconds(kUploadTimeoutMillis);
  ASSERT_TRUE(UploadClearcutDemoEvent(2).ok());
  EXPECT_EQ(client->last_deadline_seen, expected_deadline);
}

// Tests the functionality of obeying a request from the server to rate-limit.
TEST_F(UploaderTest, RateLimitingWorks) {
  // Have the fake HTTP client return a response to the Uploader telling it that the server
  // is asking it to wait 10ms.
  client->next_request_wait_millis = 10;
  fake_clock->set_time(std::chrono::steady_clock::time_point(std::chrono::milliseconds(0)));
  ASSERT_TRUE(UploadClearcutDemoEvent(100).ok());
  ASSERT_TRUE(SawEventCode(100));

  // Haven't waited long enough. Expect the Uploader to return an error.
  fake_clock->set_time(std::chrono::steady_clock::time_point(std::chrono::milliseconds(8)));
  ASSERT_EQ(UploadClearcutDemoEvent(150).error_code(), StatusCode::RESOURCE_EXHAUSTED);
  ASSERT_FALSE(SawEventCode(150));

  // Haven't waited long enough. Expect the Uploader to return an error.
  fake_clock->set_time(std::chrono::steady_clock::time_point(std::chrono::milliseconds(9)));
  ASSERT_EQ(UploadClearcutDemoEvent(150).error_code(), StatusCode::RESOURCE_EXHAUSTED);
  ASSERT_FALSE(SawEventCode(150));

  // Now we have waited long enough.
  fake_clock->set_time(std::chrono::steady_clock::time_point(std::chrono::milliseconds(12)));
  ASSERT_TRUE(UploadClearcutDemoEvent(150).ok());
  ASSERT_TRUE(SawEventCode(150));
}

// Tests the functionality of retrying multiple times with exponential backoff.
TEST_F(UploaderTest, ShouldRetryOnFailedUpload) {
  // Arrange for the upload to fail three times.
  client->error_statuses_to_return = {StatusCode::DEADLINE_EXCEEDED, StatusCode::RESOURCE_EXHAUSTED,
                                      StatusCode::DEADLINE_EXCEEDED};
  // Try to upload three times. Expect this to fail and return the third status code.
  fake_sleeper->Reset();
  EXPECT_EQ(UploadClearcutDemoEvent(101, 3).error_code(), StatusCode::DEADLINE_EXCEEDED);
  EXPECT_FALSE(SawEventCode(101));
  // There should have been two sleeps.
  EXPECT_EQ(fake_sleeper->last_sleep_duration().count(), kInitialBackoffMillisForTest * 2);

  // Arrange for the upload to fail three times.
  client->error_statuses_to_return = {StatusCode::DEADLINE_EXCEEDED, StatusCode::RESOURCE_EXHAUSTED,
                                      StatusCode::DEADLINE_EXCEEDED};
  // Try to upload four times. It should succeed the fourth time.
  fake_sleeper->Reset();
  ASSERT_TRUE(UploadClearcutDemoEvent(102, 4).ok());
  ASSERT_TRUE(SawEventCode(102));
  // There should have been three sleeps.
  EXPECT_EQ(fake_sleeper->last_sleep_duration().count(), kInitialBackoffMillisForTest * 4);

  // Arrange for the upload to fail four times.
  client->error_statuses_to_return = {StatusCode::DEADLINE_EXCEEDED, StatusCode::RESOURCE_EXHAUSTED,
                                      StatusCode::DEADLINE_EXCEEDED,
                                      StatusCode::RESOURCE_EXHAUSTED};
  // Try to upload four times. Expect this to fail and return the fourth status code.
  fake_sleeper->Reset();
  EXPECT_EQ(UploadClearcutDemoEvent(103, 4).error_code(), StatusCode::RESOURCE_EXHAUSTED);
  ASSERT_FALSE(SawEventCode(103));
  // There should have be three sleeps.
  EXPECT_EQ(fake_sleeper->last_sleep_duration().count(), kInitialBackoffMillisForTest * 4);

  // Arrange for the upload to fail four times.
  client->error_statuses_to_return = {StatusCode::DEADLINE_EXCEEDED, StatusCode::RESOURCE_EXHAUSTED,
                                      StatusCode::DEADLINE_EXCEEDED,
                                      StatusCode::RESOURCE_EXHAUSTED};
  // Try to upload five times. Expect this to succeed the fifth time.
  fake_sleeper->Reset();
  EXPECT_TRUE(UploadClearcutDemoEvent(104, 5).ok());
  ASSERT_TRUE(SawEventCode(104));
  // There should have be 4 sleeps.
  EXPECT_EQ(fake_sleeper->last_sleep_duration().count(), kInitialBackoffMillisForTest * 8);

  // Arrange for the upload to fail three times but return a non-retryable error the first time.
  client->error_statuses_to_return = {StatusCode::INVALID_ARGUMENT, StatusCode::NOT_FOUND,
                                      StatusCode::PERMISSION_DENIED};
  // Set up to try four times but we will in fact only try once because the first time
  // we return a non-retryable error.
  fake_sleeper->Reset();
  ASSERT_EQ(UploadClearcutDemoEvent(105, 4).error_code(), StatusCode::INVALID_ARGUMENT);
  ASSERT_FALSE(SawEventCode(105));
  EXPECT_EQ(fake_sleeper->last_sleep_duration().count(), 0);
}

// Tests that we will stop retrying when the timeout is exceeded.
TEST_F(UploaderTest, ExceedsTimeout) {
  // Do the setup again with a short timeout of 2s.
  DoSetUp(2000);
  // Arrange for the upload to fail seven times.
  client->error_statuses_to_return = {
      StatusCode::RESOURCE_EXHAUSTED, StatusCode::RESOURCE_EXHAUSTED,
      StatusCode::RESOURCE_EXHAUSTED, StatusCode::RESOURCE_EXHAUSTED,
      StatusCode::RESOURCE_EXHAUSTED, StatusCode::RESOURCE_EXHAUSTED,
      StatusCode::RESOURCE_EXHAUSTED};
  fake_sleeper->Reset();
  // Arrange to try to upload ten times. This will fail after only five attempts because
  // the two-second deadline will have been exceeded. The returned error will be
  // DEADLINE_EXCEEDED.
  EXPECT_EQ(UploadClearcutDemoEvent(101, 10).error_code(), StatusCode::DEADLINE_EXCEEDED);
  EXPECT_FALSE(SawEventCode(101));
  // There should have be only 4 sleeps because after five upload attempts the deadline
  // will have been exceeded.
  EXPECT_EQ(fake_sleeper->last_sleep_duration().count(), kInitialBackoffMillisForTest * 8);
}

// Tests the logic for handling non-200 HTTP response codes.
TEST_F(UploaderTest, HttpResponseCodes) {
  std::vector<std::pair<int, StatusCode>> codes = {
      {400, StatusCode::INVALID_ARGUMENT},   {401, StatusCode::PERMISSION_DENIED},
      {403, StatusCode::PERMISSION_DENIED},  {404, StatusCode::NOT_FOUND},
      {503, StatusCode::RESOURCE_EXHAUSTED}, {500, StatusCode::UNKNOWN}};
  for (const auto &code : codes) {
    client->http_response_codes_to_return = {code.first};
    EXPECT_EQ(UploadClearcutDemoEvent(1, 1).error_code(), code.second);
  }
  // Arrange for the upload to fail three times.
  client->http_response_codes_to_return = {500, 500, 503};
  // Try to upload three times. Expect this to fail and return the third status code.
  fake_sleeper->Reset();
  EXPECT_EQ(UploadClearcutDemoEvent(101, 3).error_code(), StatusCode::RESOURCE_EXHAUSTED);
  // The server returned an error but still saw the request.
  EXPECT_TRUE(SawEventCode(101));
  // There should have been two sleeps.
  EXPECT_EQ(fake_sleeper->last_sleep_duration().count(), kInitialBackoffMillisForTest * 2);

  // Arrange for the upload to fail three times.
  client->http_response_codes_to_return = {503, 503, 503};
  // Try to upload four times. It should succeed the fourth time.
  fake_sleeper->Reset();
  ASSERT_TRUE(UploadClearcutDemoEvent(102, 4).ok());
  ASSERT_TRUE(SawEventCode(102));
  // There should have been three sleeps.
  EXPECT_EQ(fake_sleeper->last_sleep_duration().count(), kInitialBackoffMillisForTest * 4);
}

}  // namespace cobalt::lib::clearcut

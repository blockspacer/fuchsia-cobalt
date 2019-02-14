// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoder/shipping_manager.h"

#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "./clearcut_extensions.pb.h"
#include "./gtest.h"
#include "./logging.h"
#include "config/client_config.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/fake_system_data.h"
#include "encoder/memory_observation_store.h"
#include "encoder/observation_store.h"
#include "encoder/project_context.h"
// Generated from shipping_manager_test_config.yaml
#include "encoder/shipping_manager_test_config.h"
#include "third_party/clearcut/clearcut.pb.h"
#include "third_party/gflags/include/gflags/gflags.h"

namespace cobalt {
namespace encoder {

using cobalt::clearcut_extensions::LogEventExtension;
using config::ClientConfig;
using send_retryer::CancelHandle;
using send_retryer::SendRetryer;
using statusor::StatusOr;
using util::EncryptedMessageMaker;

namespace {

// These values must match the values specified in the invocation of
// generate_test_config_h() in CMakeLists.txt. and in the invocation of
// cobalt_config_header("generate_shipping_manager_test_config") in BUILD.gn.
const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;

const size_t kNoOpEncodingByteOverhead = 30;
const size_t kMaxBytesPerObservation = 50;
const size_t kMaxBytesPerEnvelope = 200;
const size_t kMaxBytesTotal = 1000;

const std::chrono::seconds kInitialRpcDeadline(10);
const std::chrono::seconds kDeadlinePerSendAttempt(60);
const std::chrono::seconds kMaxSeconds = UploadScheduler::kMaxSeconds;

// Returns a ProjectContext obtained by parsing the configuration specified
// in shipping_manager_test_config.yaml
std::shared_ptr<ProjectContext> GetTestProject() {
  // Parse the base64-encoded, serialized CobaltRegistry in
  // shipping_manager_test_config.h. This is generated from
  // shipping_manager_test_config.yaml. Edit that yaml file to make changes. The
  // variable name below, |kCobaltRegistryBase64|, must match what is
  // specified in the build files.
  std::unique_ptr<ClientConfig> client_config =
      ClientConfig::CreateFromCobaltRegistryBase64(kCobaltRegistryBase64);
  EXPECT_NE(nullptr, client_config);

  return std::shared_ptr<ProjectContext>(new ProjectContext(
      kCustomerId, kProjectId,
      std::shared_ptr<ClientConfig>(client_config.release())));
}

struct FakeSendRetryer : public SendRetryerInterface {
  explicit FakeSendRetryer(uint32_t metric_id = kDefaultMetricId)
      : SendRetryerInterface(), metric_id(metric_id) {}

  grpc::Status SendToShuffler(
      std::chrono::seconds initial_rpc_deadline,
      std::chrono::seconds overerall_deadline,
      send_retryer::CancelHandle* cancel_handle,
      const EncryptedMessage& encrypted_message) override {
    // Decrypt encrypted_message. (No actual decryption is involved since
    // we used the NONE encryption scheme.)
    util::MessageDecrypter decrypter("");
    Envelope recovered_envelope;
    EXPECT_TRUE(
        decrypter.DecryptMessage(encrypted_message, &recovered_envelope));
    EXPECT_EQ(1, recovered_envelope.batch_size());
    EXPECT_EQ(metric_id, recovered_envelope.batch(0).meta_data().metric_id());

    std::unique_lock<std::mutex> lock(mutex);
    send_call_count++;
    observation_count +=
        recovered_envelope.batch(0).encrypted_observation_size();
    // We grab the return value before we block. This allows the test
    // thread to wait for us to block, then change the value of
    // status_to_return for the *next* send without changing it for
    // the currently blocking send.
    grpc::Status status = status_to_return;
    if (should_block) {
      is_blocking = true;
      send_is_blocking_notifier.notify_all();
      send_can_exit_notifier.wait(lock, [this] { return !should_block; });
      is_blocking = false;
    }
    return status;
  }

  std::mutex mutex;
  bool should_block = false;
  std::condition_variable send_can_exit_notifier;
  bool is_blocking = false;
  std::condition_variable send_is_blocking_notifier;
  grpc::Status status_to_return = grpc::Status::OK;
  int send_call_count = 0;
  int observation_count = 0;
  uint32_t metric_id;
};

class FakeHTTPClient : public clearcut::HTTPClient {
 public:
  std::future<StatusOr<clearcut::HTTPResponse>> Post(
      clearcut::HTTPRequest request,
      std::chrono::steady_clock::time_point _ignored) {
    util::MessageDecrypter decrypter("");

    clearcut::LogRequest req;
    req.ParseFromString(request.body);
    EXPECT_GT(req.log_event_size(), 0);
    for (auto event : req.log_event()) {
      EXPECT_TRUE(event.HasExtension(LogEventExtension::ext));
      auto log_event = event.GetExtension(LogEventExtension::ext);
      Envelope recovered_envelope;
      EXPECT_TRUE(decrypter.DecryptMessage(
          log_event.cobalt_encrypted_envelope(), &recovered_envelope));
      EXPECT_EQ(1, recovered_envelope.batch_size());
      EXPECT_EQ(kClearcutMetricId,
                recovered_envelope.batch(0).meta_data().metric_id());
      observation_count +=
          recovered_envelope.batch(0).encrypted_observation_size();
    }
    send_call_count++;

    clearcut::HTTPResponse response;
    response.http_code = 200;
    clearcut::LogResponse resp;
    resp.SerializeToString(&response.response);

    std::promise<StatusOr<clearcut::HTTPResponse>> response_promise;
    response_promise.set_value(std::move(response));

    return response_promise.get_future();
  }

  int send_call_count = 0;
  int observation_count = 0;
};
}  // namespace

class ShippingManagerTest : public ::testing::Test {
 public:
  ShippingManagerTest()
      : encrypt_to_shuffler_(EncryptedMessageMaker::MakeUnencrypted()),
        encrypt_to_analyzer_(EncryptedMessageMaker::MakeUnencrypted()),
        observation_store_(kMaxBytesPerObservation, kMaxBytesPerEnvelope,
                           kMaxBytesTotal),
        project_(GetTestProject()),
        encoder_(project_, ClientSecret::GenerateNewSecret(), &system_data_) {}

 protected:
  void Init(std::chrono::seconds schedule_interval,
            std::chrono::seconds min_interval,
            uint32_t metric_id = kDefaultMetricId) {
    send_retryer_.reset(new FakeSendRetryer(metric_id));
    UploadScheduler upload_scheduler(schedule_interval, min_interval);
    LegacyShippingManager::SendRetryerParams send_retryer_params(
        kInitialRpcDeadline, kDeadlinePerSendAttempt);
    if (metric_id == kDefaultMetricId) {
      shipping_manager_.reset(new LegacyShippingManager(
          upload_scheduler, &observation_store_, encrypt_to_shuffler_.get(),
          send_retryer_params, send_retryer_.get()));
    } else {
      auto http_client = std::make_unique<FakeHTTPClient>();
      http_client_ = http_client.get();
      shipping_manager_.reset(new ClearcutV1ShippingManager(
          upload_scheduler, &observation_store_, encrypt_to_shuffler_.get(),
          std::make_unique<clearcut::ClearcutUploader>(
              "https://test.com", std::move(http_client))));
    }
    shipping_manager_->Start();
  }

  ObservationStore::StoreStatus AddObservation(
      size_t num_bytes, uint32_t metric_id = kDefaultMetricId) {
    CHECK(num_bytes > kNoOpEncodingByteOverhead) << " num_bytes=" << num_bytes;
    Encoder::Result result = encoder_.EncodeString(
        metric_id, kNoOpEncodingId,
        std::string(num_bytes - kNoOpEncodingByteOverhead, 'x'));
    auto message = std::make_unique<EncryptedMessage>();
    EXPECT_TRUE(
        encrypt_to_analyzer_->Encrypt(*result.observation, message.get()));
    auto retval = observation_store_.AddEncryptedObservation(
        std::move(message), std::move(result.metadata));
    shipping_manager_->NotifyObservationsAdded();
    return retval;
  }

  void CheckCallCount(int expected_call_count, int expected_observation_count) {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    EXPECT_EQ(expected_call_count, send_retryer_->send_call_count);
    EXPECT_EQ(expected_observation_count, send_retryer_->observation_count);
  }

  void CheckHTTPCallCount(int expected_call_count,
                          int expected_observation_count) {
    ASSERT_NE(nullptr, http_client_);
    EXPECT_EQ(expected_call_count, http_client_->send_call_count);
    EXPECT_EQ(expected_observation_count, http_client_->observation_count);
  }

  std::unique_ptr<EncryptedMessageMaker> encrypt_to_shuffler_;
  std::unique_ptr<EncryptedMessageMaker> encrypt_to_analyzer_;
  MemoryObservationStore observation_store_;
  FakeSystemData system_data_;
  std::unique_ptr<FakeSendRetryer> send_retryer_;
  std::unique_ptr<ShippingManager> shipping_manager_;
  std::shared_ptr<ProjectContext> project_;
  FakeHTTPClient* http_client_ = nullptr;
  Encoder encoder_;
};

// We construct a ShippingManager and destruct it without calling any methods.
// This tests that the destructor requests that the worker thread terminate
// and then waits for it to terminate.
TEST_F(ShippingManagerTest, ConstructAndDestruct) {
  Init(kMaxSeconds, kMaxSeconds);
}

// We construct a ShippingManager and add one small Observation to it.
// Before the ShippingManager has a chance to send the Observation we
// destruct it. We test that the Add() returns OK and the destructor
// succeeds.
TEST_F(ShippingManagerTest, AddOneObservationAndDestruct) {
  Init(kMaxSeconds, kMaxSeconds);
  EXPECT_EQ(ObservationStore::kOk, AddObservation(40));
}

// We add one Observation, confirm that it is not immediately sent,
// invoke RequestSendSoon, wait for the Observation to be sent, confirm
// that it was sent.
TEST_F(ShippingManagerTest, SendOne) {
  // Init with a very long time for the regular schedule interval but
  // zero for the minimum interval so the test doesn't have to wait.
  Init(kMaxSeconds, std::chrono::seconds::zero());
  // Add one Observation().
  EXPECT_EQ(ObservationStore::kOk, AddObservation(40));

  // Confirm it has not been sent yet.
  CheckCallCount(0, 0);

  // Invoke RequestSendSoon.
  shipping_manager_->RequestSendSoon();

  // Wait for it to be sent.
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // Confirm it has been sent.
  EXPECT_EQ(1u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(0u, shipping_manager_->num_failed_attempts());
  EXPECT_EQ(grpc::OK, shipping_manager_->last_send_status().error_code());
  CheckCallCount(1, 1);
}

// We add two Observations, confirm that they are not immediately sent,
// invoke RequestSendSoon, wait for the Observations to be sent, confirm
// that they were sent together in a single Envelope.
TEST_F(ShippingManagerTest, SendTwo) {
  // Init with a very long time for the regular schedule interval but
  // zero for the minimum interval so the test doesn't have to wait.
  Init(kMaxSeconds, std::chrono::seconds::zero());

  // Add two observations.
  EXPECT_EQ(ObservationStore::kOk, AddObservation(40));
  EXPECT_EQ(ObservationStore::kOk, AddObservation(40));

  // Confirm they have not been sent.
  CheckCallCount(0, 0);

  // Request send soon.
  shipping_manager_->RequestSendSoon();

  // Wait for both Observations to be sent.
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // Confirm the two Observations were sent together in a single Envelope.
  EXPECT_EQ(1u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(0u, shipping_manager_->num_failed_attempts());
  EXPECT_EQ(grpc::OK, shipping_manager_->last_send_status().error_code());
  CheckCallCount(1, 2);
}

// Trys to add an Observation that is too big. Tests that kObservationTooBig
// is returned.
TEST_F(ShippingManagerTest, ObservationTooBig) {
  // Init with a very long time for the regular schedule interval but
  // zero for the minimum interval so the test doesn't have to wait.
  Init(kMaxSeconds, std::chrono::seconds::zero());

  // Add one observation that is too big.
  EXPECT_EQ(ObservationStore::kObservationTooBig, AddObservation(60));
}

// Add multiple Observations and allow them to be sent on the regular
// schedule.
TEST_F(ShippingManagerTest, ScheduledSend) {
  // We set both schedule_interval_ and min_interval_ to zero so the test
  // does not have to wait.
  Init(std::chrono::seconds::zero(), std::chrono::seconds::zero());

  // Add two Observations but do not invoke RequestSendSoon() and do
  // not add enough Observations to exceed envelope_send_threshold_size_.
  for (int i = 0; i < 2; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(40));
  }
  // Wait for the scheduled send
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // We do not check the number of sends because that depends on the
  // timing interaction of the test thread and the worker thread and so it
  // would be flaky. Just check that all 3 Observations were sent.
  std::unique_lock<std::mutex> lock(send_retryer_->mutex);
  EXPECT_EQ(2, send_retryer_->observation_count);
  EXPECT_EQ(grpc::OK, shipping_manager_->last_send_status().error_code());
}

// Tests the ShippingManager in the case that the ObservationStore returns
// kStoreFull.
//
// kMaxBytesTotal = 1000 and we are using Observations of size 40 bytes.
// 40 * 25 = 1000. The MemoryObservationStore will allow the 26th Observation
// to be added and then reject the 27th Observation.
TEST_F(ShippingManagerTest, ExceedMaxBytesTotal) {
  // Init with a very long time for the regular schedule interval but
  // zero for the minimum interval so the test doesn't have to wait.
  Init(kMaxSeconds, std::chrono::seconds::zero());

  // Configure the FakeSendRetryer to fail every time.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::CANCELLED;
  }

  // We can add 15 observations without the ObservationStore reporting that
  // it is almost full.
  for (int i = 0; i < 15; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(40));
    shipping_manager_->WaitUntilWorkerWaiting(kMaxSeconds);
  }

  // The ObservationStore was never almost full so the ShippingManager
  // should not have attempted to do a send.
  EXPECT_EQ(0u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(0u, shipping_manager_->num_failed_attempts());

  // The sixteenth Observation causes the ObservationStore to become almost
  // full and that causes the ShippingManager to attempt a send.
  EXPECT_EQ(ObservationStore::kOk, AddObservation(40));
  shipping_manager_->WaitUntilWorkerWaiting(kMaxSeconds);
  auto num_send_attempts = shipping_manager_->num_send_attempts();
  EXPECT_GT(num_send_attempts, 0u);
  EXPECT_EQ(num_send_attempts, shipping_manager_->num_failed_attempts());
  EXPECT_EQ(grpc::CANCELLED,
            shipping_manager_->last_send_status().error_code());

  // We can add 10 more Observations bringing the total to 26,
  // without the ObservationStore being full.
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(40));
  }
  shipping_manager_->WaitUntilWorkerWaiting(kMaxSeconds);

  // Because the ObservationStore is almost full the ShippingManager has
  // been attempting additional sends.
  EXPECT_GT(shipping_manager_->num_send_attempts(), num_send_attempts);
  num_send_attempts = shipping_manager_->num_send_attempts();
  EXPECT_EQ(num_send_attempts, shipping_manager_->num_failed_attempts());
  EXPECT_EQ(grpc::CANCELLED,
            shipping_manager_->last_send_status().error_code());

  // The 27th Observation will be rejected with kStoreFull.
  EXPECT_EQ(ObservationStore::kStoreFull, AddObservation(40));

  // Now configure the FakeSendRetryer to start succeeding,
  // and reset the counts.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::OK;
    send_retryer_->send_call_count = 0;
    send_retryer_->observation_count = 0;
  }

  // Send all 26 of the accumulated Observations.
  shipping_manager_->RequestSendSoon();
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // All 26 successfully-added Observations should have been sent in six
  // envelopes
  CheckCallCount(6, 26);
  EXPECT_EQ(grpc::OK, shipping_manager_->last_send_status().error_code());
  EXPECT_GT(shipping_manager_->num_send_attempts(), num_send_attempts);
  num_send_attempts = shipping_manager_->num_send_attempts();
  EXPECT_GT(num_send_attempts, shipping_manager_->num_failed_attempts());
  EXPECT_EQ(grpc::OK, shipping_manager_->last_send_status().error_code());

  // Now we can add a 27th Observation and send it.
  EXPECT_EQ(ObservationStore::kOk, AddObservation(40));
  shipping_manager_->RequestSendSoon();
  shipping_manager_->WaitUntilIdle(kMaxSeconds);
  CheckCallCount(7, 27);
  EXPECT_EQ(grpc::OK, shipping_manager_->last_send_status().error_code());
}

// Tests that when the total amount of accumulated Observation data exceeds
// total_bytes_send_threshold_  then RequestSendSoon() will be invoked.
TEST_F(ShippingManagerTest, TotalBytesSendThreshold) {
  // Init with a very long time for the regular schedule interval but
  // zero for the minimum interval so the test doesn't have to wait.
  Init(kMaxSeconds, std::chrono::seconds::zero());

  // Configure the FakeSendRetryer to fail every time so that we can
  // accumulate Observation data in memory.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::CANCELLED;
  }

  // total_bytes_send_threshold_ = 0.6 * max_bytes_total_.
  // kMaxBytesTotal = 1000 so total_bytes_send_threshold_ = 600.
  // We are using Observations of size 40 and 40 * 15 = 600 so the first
  // Observation that causes us to exceed total_bytes_send_threshold_ is #16.
  //
  // Add 15 Observations. We want to do this in such a way that we don't
  // exceed max_bytes_per_envelope_.  Each time we will invoke
  // RequestSendSoon() and then WaitUntilWorkerWaiting() so that we know that
  // between invocations of AddObservtion() the worker thread will complete
  // one execution of SendAllEnvelopes().
  for (int i = 0; i < 15; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(40));
    if (i < 15) {
      // After having added 15 observations we have exceeded the
      // ObservationStore's almost_full_threshold_ and this means that each
      // invocation of AddEncryptedObservation() followed by a
      // NotifyObservationsAdded() automatically invokes RequestSendSoon() and
      // so we don't want to invoke it again here.
      shipping_manager_->RequestSendSoon();
    }
    shipping_manager_->WaitUntilWorkerWaiting(kMaxSeconds);
  }

  // We expect there to have been 45 calls to SendRetryer::SendToShuffler() in
  // which the Envelopes sent contained a total of 195 Observations. Here we
  // explain how this was calculated. See the comments at the top of the file on
  // kMinEnvelopeSendSize. There it is explained that the ObservationStore will
  // attempt to bundle together up to 5 observations into a sinle envelope
  // before sending. None of the sends succeed so the ObservationStore keeps
  // accumulating more Envelopes containing 5 Observations that failed to send.
  // Below is the complete pattern of send attempts. Each set in braces
  // represents one execution of SendAllEnvelopes(). The numbers in each set
  // represent the invocations of SendEnvelopeToBackend() with an Envelope that
  // contains that many observations.
  //
  // {1, 1, 1}, {2, 2, 2}, {3, 3, 3}, {4, 4, 4}, {5, 5, 5}, {5, 5, 5}, ...
  //
  // Thus the total number of send attempts is the total number of numbers:
  // 3 * 15 = 45
  //
  // And the total number of Observations is the sum of all the numbers:
  // (1 + 2 + 3 + 4 + 5) * 3 + (5*3*(15-5)) = 195
  CheckCallCount(45, 195);

  // Now configure the FakeSendRetryer to start succeeding,
  // and reset the counts.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::OK;
    send_retryer_->send_call_count = 0;
    send_retryer_->observation_count = 0;
  }

  // Now we send the 16th Observattion. But notice that we do *not* invoke
  // RequestSendSoon() this time. So the reason the Observations get sent
  // now is because we are exceeding total_bytes_send_threshold_.
  EXPECT_EQ(ObservationStore::kOk, AddObservation(40));

  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // All 16 Observations should have been sent in 4 envelopes as {5, 5, 5, 1}.
  CheckCallCount(4, 16);
}

// Test the version of the method RequestSendSoon() that takes a callback.
// We test that the callback is invoked with success = true when the send
// succeeds and with success = false when the send fails.
TEST_F(ShippingManagerTest, RequestSendSoonWithCallback) {
  Init(kMaxSeconds, std::chrono::seconds::zero());

  // Invoke RequestSendSoon() with a callback before any Observations are
  // added.
  bool captured_success_arg = false;
  shipping_manager_->RequestSendSoon([&captured_success_arg](bool success) {
    captured_success_arg = success;
  });
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // Check that the callback was invoked synchronously with success = true.
  CheckCallCount(0, 0);
  EXPECT_EQ(0u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(0u, shipping_manager_->num_failed_attempts());
  EXPECT_TRUE(captured_success_arg);

  // Arrange for the first send to fail.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::CANCELLED;
  }

  // Add an Observation, invoke RequestSendSoon() with a callback.
  shipping_manager_->WaitUntilIdle(kMaxSeconds);
  EXPECT_EQ(ObservationStore::kOk,
            AddObservation(kNoOpEncodingByteOverhead + 1));
  shipping_manager_->RequestSendSoon([&captured_success_arg](bool success) {
    captured_success_arg = success;
  });
  shipping_manager_->WaitUntilWorkerWaiting(kMaxSeconds);

  // Check that the callback was invoked with success = false.
  CheckCallCount(3, 3);
  EXPECT_EQ(3u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(3u, shipping_manager_->num_failed_attempts());
  EXPECT_FALSE(captured_success_arg);

  // Arrange for the next send to succeed.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::OK;
  }

  // Don't add another Observation but invoke RequestSendSoon() with a
  // callback.
  shipping_manager_->RequestSendSoon([&captured_success_arg](bool success) {
    captured_success_arg = success;
  });
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // Check that the callback was invoked with success = true.
  CheckCallCount(4, 4);
  EXPECT_EQ(4u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(3u, shipping_manager_->num_failed_attempts());
  EXPECT_TRUE(captured_success_arg);

  // Arrange for the next send to fail.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::CANCELLED;
  }

  // Invoke RequestSendSoon without a callback just so that there is an
  // Observation cached in the inner EnvelopeMaker.
  EXPECT_EQ(ObservationStore::kOk,
            AddObservation(kNoOpEncodingByteOverhead + 1));
  shipping_manager_->RequestSendSoon();
  shipping_manager_->WaitUntilWorkerWaiting(kMaxSeconds);
  CheckCallCount(7, 7);
  EXPECT_EQ(7u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(6u, shipping_manager_->num_failed_attempts());

  // Arrange for the next send to succeed.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::OK;
  }

  // Add an Observation, invoke RequestSendSoon() with a callback.
  EXPECT_EQ(ObservationStore::kOk,
            AddObservation(kNoOpEncodingByteOverhead + 1));
  shipping_manager_->RequestSendSoon([&captured_success_arg](bool success) {
    captured_success_arg = success;
  });
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // Check that the callback was invoked with success = true.
  CheckCallCount(8, 9);
  EXPECT_EQ(8u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(6u, shipping_manager_->num_failed_attempts());
  EXPECT_TRUE(captured_success_arg);
}

TEST_F(ShippingManagerTest, SendObservationToClearcut) {
  // Init with a very long time for the regular schedule interval but
  // zero for the minimum interval so the test doesn't have to wait.
  Init(kMaxSeconds, std::chrono::seconds::zero(), kClearcutMetricId);

  // Add some observations for clearcut
  EXPECT_EQ(ObservationStore::kOk, AddObservation(40, kClearcutMetricId));
  EXPECT_EQ(ObservationStore::kOk, AddObservation(41, kClearcutMetricId));

  // Request send soon.
  shipping_manager_->RequestSendSoon();

  // Wait for both Observations to be sent.
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // Ensure we sent stuff to clearcut.
  CheckHTTPCallCount(1, 2);

  // Ensure nothing was sent to legacy.
  CheckCallCount(0, 0);
}

}  // namespace encoder
}  // namespace cobalt

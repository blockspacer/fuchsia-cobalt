// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/uploader/shipping_manager.h"

#include <mutex>
#include <utility>

#include "src/logger/logger_interface.h"
#include "src/logging.h"
#include "src/pb/clearcut_extensions.pb.h"

namespace cobalt {
namespace encoder {

using observation_store::ObservationStore;
using EnvelopeHolder = ObservationStore::EnvelopeHolder;
using cobalt::clearcut_extensions::LogEventExtension;

namespace {

// The number of upload failures after which ShippingManager will bail out of an
// invocation of SendAllEnvelopes().
constexpr size_t kMaxFailuresWithoutSuccess = 3;

std::string ToString(const std::chrono::system_clock::time_point& t) {
  std::time_t time_struct = std::chrono::system_clock::to_time_t(t);
  return std::ctime(&time_struct);
}

grpc::Status CobaltStatusToGrpcStatus(const util::Status& status) {
  return grpc::Status(static_cast<grpc::StatusCode>(status.error_code()), status.error_message(),
                      status.error_details());
}

}  // namespace

ShippingManager::ShippingManager(const UploadScheduler& upload_scheduler,
                                 ObservationStore* observation_store,
                                 util::EncryptedMessageMaker* encrypt_to_shuffler)
    : upload_scheduler_(upload_scheduler),
      next_scheduled_send_time_(std::chrono::system_clock::now() + upload_scheduler_.Interval()),
      encrypt_to_shuffler_(encrypt_to_shuffler) {
  CHECK(observation_store);
  _mutex_protected_fields_do_not_access_directly_.observation_store = observation_store;
}

ShippingManager::~ShippingManager() {
  if (!worker_thread_.joinable()) {
    return;
  }
  ShutDown();
  VLOG(4) << "ShippingManager destructor: waiting for worker thread to exit...";
  worker_thread_.join();
}

void ShippingManager::Start() {
  {
    // We set idle and waiting_for_schedule to false since we are about to
    // start the worker thread. The worker thread will set these variables
    // to true at the appropriate times.
    auto locked = lock();
    locked->fields->idle = false;
    locked->fields->waiting_for_schedule = false;
  }

  std::thread t([this] { this->Run(); });
  worker_thread_ = std::move(t);
}

void ShippingManager::NotifyObservationsAdded() {
  auto locked = lock();

  if (locked->fields->observation_store->IsAlmostFull()) {
    VLOG(4) << name()
            << ": NotifyObservationsAdded(): observation_store "
               "IsAlmostFull.";
    RequestSendSoonLockHeld(locked->fields);
  }

  if (!locked->fields->observation_store->Empty()) {
    // Set idle false because any thread that invokes WaitUntilIdle() after this
    // should wait until the Observation just added has been sent.
    locked->fields->idle = false;
    locked->fields->add_observation_notifier.notify_all();
  }
}

// The caller must hold a lock on mutex_.
void ShippingManager::RequestSendSoonLockHeld(MutexProtectedFields* fields) {
  VLOG(5) << name() << ":RequestSendSoonLockHeld()";
  fields->expedited_send_requested = true;
  fields->expedited_send_notifier.notify_all();
  // We set waiting_for_schedule_ false here so that if the calling thread
  // invokes WaitUntilWorkerWaiting() after this then it will be waiting
  // for a *subsequent* time that the worker thread enters the
  // waiting-for-schedule state.
  fields->waiting_for_schedule = false;
}

void ShippingManager::RequestSendSoon() { RequestSendSoon(SendCallback()); }

void ShippingManager::RequestSendSoon(const SendCallback& send_callback) {
  VLOG(4) << name() << ": Expedited send requested.";
  auto locked = lock();
  RequestSendSoonLockHeld(locked->fields);

  // If we were given a SendCallback then do one of two things...
  if (send_callback) {
    if (locked->fields->observation_store->Empty() && locked->fields->idle) {
      // If the ObservationStore is empty and the ShippingManager is idle. Then
      // we can safely invoke the SendCallback immediately.
      locked->fields->expedited_send_requested = false;
      VLOG(5) << name()
              << "::RequestSendSoon. Not waiting because there are no "
                 "observations. Invoking callback(true) now.";
      send_callback(true);
    } else {
      // Otherwise, we should put the callback into the send callback queue.
      locked->fields->send_callback_queue.push_back(send_callback);
    }
  }
}

bool ShippingManager::shut_down() { return lock()->fields->shut_down; }

void ShippingManager::ShutDown() {
  {
    auto locked = lock();
    locked->fields->shut_down = true;
    locked->fields->shutdown_notifier.notify_all();
    locked->fields->add_observation_notifier.notify_all();
    locked->fields->expedited_send_notifier.notify_all();
    locked->fields->idle_notifier.notify_all();
    locked->fields->waiting_for_schedule_notifier.notify_all();
  }
  VLOG(4) << name() << ": shut-down requested.";
}

size_t ShippingManager::num_send_attempts() {
  auto locked = lock();
  return locked->fields->num_send_attempts;
}

size_t ShippingManager::num_failed_attempts() {
  auto locked = lock();
  return locked->fields->num_failed_attempts;
}

grpc::Status ShippingManager::last_send_status() {
  auto locked = lock();
  return locked->fields->last_send_status;
}

void ShippingManager::Run() {
  while (true) {
    auto locked = lock();
    if (locked->fields->shut_down) {
      return;
    }

    // We start each iteration of the loop with a sleep of
    // upload_scheduler_.MinInterval().
    // This ensures that we never send twice within one
    // upload_scheduler_.MinInterval() period.

    // Sleep for upload_scheduler_.MinInterval() or until shut_down_.
    VLOG(4) << name() << " worker: sleeping for " << upload_scheduler_.MinInterval().count()
            << " seconds.";
    locked->fields->shutdown_notifier.wait_for(locked->lock, upload_scheduler_.MinInterval(),
                                               [&locked] { return (locked->fields->shut_down); });
    VLOG(4) << name() << " worker: waking up from sleep. shut_down_=" << locked->fields->shut_down;
    if (locked->fields->shut_down) {
      return;
    }

    if (locked->fields->observation_store->Empty()) {
      // There are no Observations at all in the observation_store_. Wait
      // forever until notified that one arrived or shut down.
      VLOG(5) << name()
              << " worker: waiting for an Observation to "
                 "be added.";
      // If we are about to leave idle, we should make sure that we invoke all
      // of the SendCallbacks so they don't have to wait until the next time
      // observations are added.
      InvokeSendCallbacksLockHeld(locked->fields, true);
      locked->fields->idle = true;
      locked->fields->idle_notifier.notify_all();
      locked->fields->add_observation_notifier.wait(locked->lock, [&locked] {
        return (locked->fields->shut_down || !locked->fields->observation_store->Empty());
      });
      VLOG(5) << name()
              << " worker: Waking up because an Observation was "
                 "added.";
      locked->fields->idle = false;
    } else {
      auto now = std::chrono::system_clock::now();
      VLOG(4) << name() << ": now: " << ToString(now)
              << " next_scheduled_send_time_: " << ToString(next_scheduled_send_time_);
      if (next_scheduled_send_time_ <= now || locked->fields->expedited_send_requested) {
        VLOG(4) << name() << " worker: time to send now.";
        locked->fields->expedited_send_requested = false;
        locked->lock.unlock();
        SendAllEnvelopes();
        next_scheduled_send_time_ = std::chrono::system_clock::now() + upload_scheduler_.Interval();
        locked->lock.lock();
      } else {
        // Wait until the next scheduled send time or until notified of
        // a new request for an expedited send or we are shut down.
        auto time = std::chrono::system_clock::to_time_t(next_scheduled_send_time_);
        VLOG(4) << name() << " worker: waiting until " << std::ctime(&time)
                << " for next scheduled send.";
        locked->fields->waiting_for_schedule = true;
        locked->fields->waiting_for_schedule_notifier.notify_all();
        locked->fields->expedited_send_notifier.wait_until(
            locked->lock, next_scheduled_send_time_, [&locked] {
              return (locked->fields->shut_down || locked->fields->expedited_send_requested);
            });
        locked->fields->waiting_for_schedule = false;
      }
    }
  }
}

void ShippingManager::SendAllEnvelopes() {
  VLOG(5) << name() << ": SendAllEnvelopes().";
  bool success = true;
  size_t failures_without_success = 0;
  // Loop through all envelopes in the ObservationStore.
  while (true) {
    auto holder = lock()->fields->observation_store->TakeNextEnvelopeHolder();
    if (holder == nullptr) {
      // No more envelopes in the store, we can exit the loop.
      break;
    }
    auto failed_holder = SendEnvelopeToBackend(std::move(holder));
    if (failed_holder == nullptr) {
      // The send succeeded.
      failures_without_success = 0;
    } else {
      // The send failed. Increment failures_without_success and return the
      // failed EnvelopeHolder to the store.
      success = false;
      failures_without_success++;
      lock()->fields->observation_store->ReturnEnvelopeHolder(std::move(failed_holder));
    }

    if (failures_without_success >= kMaxFailuresWithoutSuccess) {
      VLOG(4) << name() << "::SendAllEnvelopes(): failed too many times ("
              << failures_without_success << "). Stopping uploads.";
      break;
    }
  }

  {
    auto locked = lock();
    InvokeSendCallbacksLockHeld(locked->fields, success);
  }
}

void ShippingManager::InvokeSendCallbacksLockHeld(MutexProtectedFields* fields, bool success) {
  fields->expedited_send_requested = false;
  std::vector<SendCallback> callbacks_to_invoke;
  callbacks_to_invoke.swap(fields->send_callback_queue);
  for (SendCallback& callback : callbacks_to_invoke) {
    VLOG(5) << name() << ": Invoking send callback(" << success << ") now.";
    callback(success);
  }
}

ClearcutV1ShippingManager::ClearcutV1ShippingManager(
    const UploadScheduler& upload_scheduler, ObservationStore* observation_store,
    util::EncryptedMessageMaker* encrypt_to_shuffler,
    std::unique_ptr<clearcut::ClearcutUploader> clearcut, logger::LoggerInterface* internal_logger,
    size_t max_attempts_per_upload)
    : ShippingManager(upload_scheduler, observation_store, encrypt_to_shuffler),
      max_attempts_per_upload_(max_attempts_per_upload),
      clearcut_(std::move(clearcut)),
      internal_metrics_(logger::InternalMetrics::NewWithLogger(internal_logger)) {}

std::unique_ptr<EnvelopeHolder> ClearcutV1ShippingManager::SendEnvelopeToBackend(
    std::unique_ptr<EnvelopeHolder> envelope_to_send) {
  auto log_extension = std::make_unique<LogEventExtension>();

  if (!encrypt_to_shuffler_->Encrypt(envelope_to_send->GetEnvelope(),
                                     log_extension->mutable_cobalt_encrypted_envelope())) {
    // TODO(rudominer) log
    // Drop on floor.
    return nullptr;
  }

  VLOG(5) << name() << " worker: Sending Envelope of size " << envelope_to_send->Size()
          << " bytes to clearcut.";

  internal_metrics_->BytesUploaded(logger::PerDeviceBytesUploadedMetricDimensionStatus::Attempted,
                                   envelope_to_send->Size());

  clearcut::LogRequest request;
  request.set_log_source(clearcut::kFuchsiaCobaltShufflerInputDevel);
  request.add_log_event()->SetAllocatedExtension(LogEventExtension::ext, log_extension.release());

  util::Status status;
  {
    std::lock_guard<std::mutex> lock(clearcut_mutex_);
    status = clearcut_->UploadEvents(&request, max_attempts_per_upload_);
  }
  {
    auto locked = lock();
    locked->fields->num_send_attempts++;
    if (!status.ok()) {
      locked->fields->num_failed_attempts++;
    }
    locked->fields->last_send_status = CobaltStatusToGrpcStatus(status);
  }
  if (status.ok()) {
    VLOG(4) << name() << "::SendEnvelopeToBackend: OK";

    internal_metrics_->BytesUploaded(logger::PerDeviceBytesUploadedMetricDimensionStatus::Succeeded,
                                     envelope_to_send->Size());

    return nullptr;
  }

  VLOG(4) << name() << ": Cobalt send to Shuffler failed: (" << status.error_code() << ") "
          << status.error_message() << ". Observations have been re-enqueued for later.";
  return envelope_to_send;
}

void ShippingManager::WaitUntilIdle(std::chrono::seconds max_wait) {
  auto locked = lock();
  if (locked->fields->shut_down || locked->fields->idle) {
    return;
  }
  locked->fields->idle_notifier.wait_for(locked->lock, max_wait, [&locked] {
    return (locked->fields->shut_down || locked->fields->idle);
  });
}

void ShippingManager::WaitUntilWorkerWaiting(std::chrono::seconds max_wait) {
  auto locked = lock();
  if (locked->fields->shut_down || locked->fields->waiting_for_schedule) {
    return;
  }
  locked->fields->waiting_for_schedule_notifier.wait_for(locked->lock, max_wait, [&locked] {
    return (locked->fields->shut_down || locked->fields->waiting_for_schedule);
  });
}

}  // namespace encoder
}  // namespace cobalt

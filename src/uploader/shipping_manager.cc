// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/uploader/shipping_manager.h"

#include <mutex>
#include <utility>

#include "src/lib/util/protected_fields.h"
#include "src/logger/logger_interface.h"
#include "src/logging.h"
#include "src/pb/clearcut_extensions.pb.h"
#include "third_party/protobuf/src/google/protobuf/util/delimited_message_util.h"

namespace cobalt::encoder {

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
                                 util::EncryptedMessageMaker* encrypt_to_analyzer)
    : encrypt_to_analyzer_(encrypt_to_analyzer),
      upload_scheduler_(upload_scheduler),
      next_scheduled_send_time_(std::chrono::system_clock::now() + upload_scheduler_.Interval()),
      protected_fields_(observation_store) {}

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
    auto locked = protected_fields_.lock();
    locked->idle = false;
    locked->waiting_for_schedule = false;
  }

  std::thread t([this] { this->Run(); });
  worker_thread_ = std::move(t);
}

void ShippingManager::NotifyObservationsAdded() {
  auto locked = protected_fields_.lock();

  if (locked->observation_store->IsAlmostFull()) {
    VLOG(4) << name()
            << ": NotifyObservationsAdded(): observation_store "
               "IsAlmostFull.";
    RequestSendSoonLockHeld(&locked);
  }

  if (!locked->observation_store->Empty()) {
    // Set idle false because any thread that invokes WaitUntilIdle() after this
    // should wait until the Observation just added has been sent.
    locked->idle = false;
    locked->add_observation_notifier.notify_all();
  }
}

// The caller must hold a lock on mutex_.
void ShippingManager::RequestSendSoonLockHeld(ShippingManager::Fields::LockedFieldsPtr* fields) {
  auto& f = *fields;

  VLOG(5) << name() << ":RequestSendSoonLockHeld()";
  f->expedited_send_requested = true;
  f->expedited_send_notifier.notify_all();
  // We set waiting_for_schedule_ false here so that if the calling thread
  // invokes WaitUntilWorkerWaiting() after this then it will be waiting
  // for a *subsequent* time that the worker thread enters the
  // waiting-for-schedule state.
  f->waiting_for_schedule = false;
}

void ShippingManager::RequestSendSoon() { RequestSendSoon(SendCallback()); }

void ShippingManager::RequestSendSoon(const SendCallback& send_callback) {
  VLOG(4) << name() << ": Expedited send requested.";
  auto locked = protected_fields_.lock();
  RequestSendSoonLockHeld(&locked);

  // If we were given a SendCallback then do one of two things...
  if (send_callback) {
    if (locked->observation_store->Empty() && locked->idle) {
      // If the ObservationStore is empty and the ShippingManager is idle. Then
      // we can safely invoke the SendCallback immediately.
      locked->expedited_send_requested = false;
      VLOG(5) << name()
              << "::RequestSendSoon. Not waiting because there are no "
                 "observations. Invoking callback(true) now.";
      send_callback(true);
    } else {
      // Otherwise, we should put the callback into the send callback queue.
      locked->send_callback_queue.push_back(send_callback);
    }
  }
}

bool ShippingManager::shut_down() const { return protected_fields_.const_lock()->shut_down; }

void ShippingManager::ShutDown() {
  {
    auto locked = protected_fields_.lock();
    locked->shut_down = true;
    locked->shutdown_notifier.notify_all();
    locked->add_observation_notifier.notify_all();
    locked->expedited_send_notifier.notify_all();
    locked->idle_notifier.notify_all();
    locked->waiting_for_schedule_notifier.notify_all();
  }
  VLOG(4) << name() << ": shut-down requested.";
}

size_t ShippingManager::num_send_attempts() const {
  return protected_fields_.const_lock()->num_send_attempts;
}

size_t ShippingManager::num_failed_attempts() const {
  return protected_fields_.const_lock()->num_failed_attempts;
}

grpc::Status ShippingManager::last_send_status() const {
  return protected_fields_.const_lock()->last_send_status;
}

void ShippingManager::Run() {
  while (true) {
    auto locked = protected_fields_.lock();
    if (locked->shut_down) {
      return;
    }

    // We start each iteration of the loop with a sleep of
    // upload_scheduler_.MinInterval().
    // This ensures that we never send twice within one
    // upload_scheduler_.MinInterval() period.

    // Sleep for upload_scheduler_.MinInterval() or until shut_down_.
    VLOG(4) << name() << " worker: sleeping for " << upload_scheduler_.MinInterval().count()
            << " seconds.";
    locked->shutdown_notifier.wait_for(locked, upload_scheduler_.MinInterval(),
                                       [&locked] { return (locked->shut_down); });

    VLOG(4) << name() << " worker: waking up from sleep. shut_down_=" << locked->shut_down;
    if (locked->shut_down) {
      return;
    }

    if (locked->observation_store->Empty()) {
      // There are no Observations at all in the observation_store_. Wait
      // forever until notified that one arrived or shut down.
      VLOG(5) << name()
              << " worker: waiting for an Observation to "
                 "be added.";
      // If we are about to leave idle, we should make sure that we invoke all
      // of the SendCallbacks so they don't have to wait until the next time
      // observations are added.
      InvokeSendCallbacksLockHeld(&locked, true);
      locked->idle = true;
      locked->idle_notifier.notify_all();
      locked->add_observation_notifier.wait(
          locked, [&locked] { return (locked->shut_down || !locked->observation_store->Empty()); });
      VLOG(5) << name()
              << " worker: Waking up because an Observation was "
                 "added.";
      locked->idle = false;
    } else {
      auto now = std::chrono::system_clock::now();
      VLOG(4) << name() << ": now: " << ToString(now)
              << " next_scheduled_send_time_: " << ToString(next_scheduled_send_time_);
      if (next_scheduled_send_time_ <= now || locked->expedited_send_requested) {
        VLOG(4) << name() << " worker: time to send now.";
        locked->expedited_send_requested = false;
        locked.unlock();
        SendAllEnvelopes();
        next_scheduled_send_time_ = std::chrono::system_clock::now() + upload_scheduler_.Interval();
        locked.lock();
      } else {
        // Wait until the next scheduled send time or until notified of
        // a new request for an expedited send or we are shut down.
        auto time = std::chrono::system_clock::to_time_t(next_scheduled_send_time_);
        VLOG(4) << name() << " worker: waiting until " << std::ctime(&time)
                << " for next scheduled send.";
        locked->waiting_for_schedule = true;
        locked->waiting_for_schedule_notifier.notify_all();
        locked->expedited_send_notifier.wait_until(locked, next_scheduled_send_time_, [&locked] {
          return (locked->shut_down || locked->expedited_send_requested);
        });
        locked->waiting_for_schedule = false;
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
    auto holder = protected_fields_.lock()->observation_store->TakeNextEnvelopeHolder();
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
      protected_fields_.lock()->observation_store->ReturnEnvelopeHolder(std::move(failed_holder));
    }

    if (failures_without_success >= kMaxFailuresWithoutSuccess) {
      VLOG(4) << name() << "::SendAllEnvelopes(): failed too many times ("
              << failures_without_success << "). Stopping uploads.";
      break;
    }
  }

  {
    auto locked = protected_fields_.lock();
    InvokeSendCallbacksLockHeld(&locked, success);
  }
}

void ShippingManager::InvokeSendCallbacksLockHeld(ShippingManager::Fields::LockedFieldsPtr* fields,
                                                  bool success) {
  auto& f = *fields;

  f->expedited_send_requested = false;
  std::vector<SendCallback> callbacks_to_invoke;
  callbacks_to_invoke.swap(f->send_callback_queue);
  for (SendCallback& callback : callbacks_to_invoke) {
    VLOG(5) << name() << ": Invoking send callback(" << success << ") now.";
    callback(success);
  }
}

ClearcutV1ShippingManager::ClearcutV1ShippingManager(
    const UploadScheduler& upload_scheduler, ObservationStore* observation_store,
    util::EncryptedMessageMaker* encrypt_to_shuffler,
    std::unique_ptr<lib::clearcut::ClearcutUploader> clearcut, int32_t log_source_id,
    logger::LoggerInterface* internal_logger, size_t max_attempts_per_upload, std::string api_key)
    : ClearcutV1ShippingManager(upload_scheduler, observation_store, encrypt_to_shuffler, nullptr,
                                std::move(clearcut), log_source_id, internal_logger,
                                max_attempts_per_upload, std::move(api_key)) {}

ClearcutV1ShippingManager::ClearcutV1ShippingManager(
    const UploadScheduler& upload_scheduler, ObservationStore* observation_store,
    util::EncryptedMessageMaker* encrypt_to_shuffler,
    util::EncryptedMessageMaker* encrypt_to_analyzer,
    std::unique_ptr<lib::clearcut::ClearcutUploader> clearcut, int32_t log_source_id,
    logger::LoggerInterface* internal_logger, size_t max_attempts_per_upload, std::string api_key)
    : ClearcutV1ShippingManager(upload_scheduler, observation_store, encrypt_to_analyzer,
                                std::move(clearcut), internal_logger, max_attempts_per_upload,
                                std::move(api_key)) {
  AddClearcutDestination(encrypt_to_shuffler, log_source_id);
}

ClearcutV1ShippingManager::ClearcutV1ShippingManager(
    const UploadScheduler& upload_scheduler, ObservationStore* observation_store,
    util::EncryptedMessageMaker* encrypt_to_analyzer,
    std::unique_ptr<lib::clearcut::ClearcutUploader> clearcut,
    logger::LoggerInterface* internal_logger, size_t max_attempts_per_upload, std::string api_key)
    : ShippingManager(upload_scheduler, observation_store, encrypt_to_analyzer),
      max_attempts_per_upload_(max_attempts_per_upload),
      clearcut_(std::move(clearcut)),
      internal_metrics_(logger::InternalMetrics::NewWithLogger(internal_logger)),
      api_key_(std::move(api_key)) {}

ClearcutV1ShippingManager::ClearcutV1ShippingManager(
    const UploadScheduler& upload_scheduler, ObservationStore* observation_store,
    std::unique_ptr<lib::clearcut::ClearcutUploader> clearcut,
    logger::LoggerInterface* internal_logger, size_t max_attempts_per_upload, std::string api_key)
    : ShippingManager(upload_scheduler, observation_store, nullptr),
      max_attempts_per_upload_(max_attempts_per_upload),
      clearcut_(std::move(clearcut)),
      internal_metrics_(logger::InternalMetrics::NewWithLogger(internal_logger)),
      api_key_(std::move(api_key)) {}

void ClearcutV1ShippingManager::AddClearcutDestination(
    util::EncryptedMessageMaker* encrypt_to_shuffler, int32_t log_source_id) {
  clearcut_destinations_.emplace_back(ClearcutDestination({encrypt_to_shuffler, log_source_id}));
}

void ClearcutV1ShippingManager::ResetInternalMetrics(logger::LoggerInterface* internal_logger) {
  internal_metrics_ = logger::InternalMetrics::NewWithLogger(internal_logger);
  clearcut_->ResetInternalMetrics(internal_logger);
}

std::unique_ptr<EnvelopeHolder> ClearcutV1ShippingManager::SendEnvelopeToBackend(
    std::unique_ptr<EnvelopeHolder> envelope_to_send) {
  auto envelope = envelope_to_send->GetEnvelope(encrypt_to_analyzer_);
  envelope.set_api_key(api_key_);

  bool error_occurred = false;
  for (const auto& clearcut_destination : clearcut_destinations_) {
    util::Status status =
        SendEnvelopeToClearcutDestination(envelope, envelope_to_send->Size(), clearcut_destination);
    if (!status.ok()) {
      error_occurred = true;
      VLOG(4) << name() << ": Cobalt send to Shuffler failed: (" << status.error_code() << ") "
              << status.error_message() << ". Observations have been re-enqueued for later.";
    }
  }
  if (error_occurred) {
    return envelope_to_send;
  }
  return nullptr;
}

util::Status ClearcutV1ShippingManager::SendEnvelopeToClearcutDestination(
    const Envelope& envelope, size_t envelope_size,
    const ClearcutDestination& clearcut_destination) {
  auto log_extension = std::make_unique<LogEventExtension>();

  if (!clearcut_destination.encrypt_to_shuffler_->Encrypt(
          envelope, log_extension->mutable_cobalt_encrypted_envelope())) {
    // TODO(rudominer) log
    // Drop on floor.
    return util::Status::OK;
  }

  for (const auto& observation_batch : envelope.batch()) {
    const auto& metadata = observation_batch.meta_data();
    internal_metrics_->BytesUploaded(
        logger::PerProjectBytesUploadedMetricDimensionStatus::Attempted,
        observation_batch.ByteSizeLong(), metadata.customer_id(), metadata.project_id());
  }

  VLOG(5) << name() << " worker: Sending Envelope of size " << envelope_size
          << " bytes to clearcut.";

  lib::clearcut::LogRequest request;
  request.set_log_source(clearcut_destination.log_source_id_);
  request.add_log_event()->SetAllocatedExtension(LogEventExtension::ext, log_extension.release());

  util::Status status;
  {
    std::lock_guard<std::mutex> lock(clearcut_mutex_);
    status = clearcut_->UploadEvents(&request, max_attempts_per_upload_);
  }
  {
    auto locked = protected_fields_.lock();
    locked->num_send_attempts++;
    if (!status.ok()) {
      locked->num_failed_attempts++;
    }
    locked->last_send_status = CobaltStatusToGrpcStatus(status);
  }
  if (status.ok()) {
    VLOG(4) << name() << "::SendEnvelopeToBackend: OK";

    for (const auto& observation_batch : envelope.batch()) {
      const auto& metadata = observation_batch.meta_data();
      internal_metrics_->BytesUploaded(
          logger::PerProjectBytesUploadedMetricDimensionStatus::Succeeded,
          observation_batch.GetCachedSize(), metadata.customer_id(), metadata.project_id());
    }
  }
  return status;
}

void ShippingManager::WaitUntilIdle(std::chrono::seconds max_wait) {
  auto locked = protected_fields_.lock();
  if (locked->shut_down || locked->idle) {
    return;
  }
  locked->idle_notifier.wait_for(locked, max_wait,
                                 [&locked] { return (locked->shut_down || locked->idle); });
}

void ShippingManager::WaitUntilWorkerWaiting(std::chrono::seconds max_wait) {
  auto locked = protected_fields_.lock();
  if (locked->shut_down || locked->waiting_for_schedule) {
    return;
  }
  locked->waiting_for_schedule_notifier.wait_for(
      locked, max_wait, [&locked] { return (locked->shut_down || locked->waiting_for_schedule); });
}

LocalShippingManager::LocalShippingManager(observation_store::ObservationStore* observation_store,
                                           std::string output_file_path,
                                           std::unique_ptr<util::FileSystem> fs)
    : LocalShippingManager(observation_store, nullptr, std::move(output_file_path), std::move(fs)) {
}

LocalShippingManager::LocalShippingManager(observation_store::ObservationStore* observation_store,
                                           util::EncryptedMessageMaker* encrypt_to_analyzer,
                                           std::string output_file_path,
                                           std::unique_ptr<util::FileSystem> fs)
    : ShippingManager({std::chrono::seconds::zero(), std::chrono::seconds::zero()},
                      observation_store, encrypt_to_analyzer),
      output_file_path_(std::move(output_file_path)),
      fs_(std::move(fs)) {
  CHECK(fs_);
}

std::unique_ptr<EnvelopeHolder> LocalShippingManager::SendEnvelopeToBackend(
    std::unique_ptr<EnvelopeHolder> envelope_to_send) {
  const Envelope& envelope = envelope_to_send->GetEnvelope(encrypt_to_analyzer_);

  VLOG(5) << name() << " worker: Saving Envelope of size " << envelope_to_send->Size()
          << " bytes to local file.";

  util::Status status = util::Status::OK;

  auto ofs = fs_->NewProtoOutputStream(output_file_path_, /*append=*/true);
  if (ofs.ok()) {
    auto proto_output_stream = std::move(ofs.ValueOrDie());
    if (!google::protobuf::util::SerializeDelimitedToZeroCopyStream(envelope,
                                                                    proto_output_stream.get())) {
      VLOG(4) << name() << ": Unable to write Envelope to local file: " << output_file_path_;
    }
  }

  {
    auto locked = protected_fields_.lock();
    locked->num_send_attempts++;
    if (!status.ok()) {
      locked->num_failed_attempts++;
    }
    locked->last_send_status = CobaltStatusToGrpcStatus(status);
  }
  if (status.ok()) {
    VLOG(4) << name() << "::SendEnvelopeToBackend: OK";
    return nullptr;
  }

  VLOG(4) << name() << ": Cobalt save to local file failed: (" << status.error_code() << ") "
          << status.error_message() << ". Observations have been re-enqueued for later.";
  return envelope_to_send;
}

}  // namespace cobalt::encoder

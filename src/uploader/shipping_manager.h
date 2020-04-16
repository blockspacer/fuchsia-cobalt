// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_UPLOADER_SHIPPING_MANAGER_H_
#define COBALT_SRC_UPLOADER_SHIPPING_MANAGER_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "src/lib/clearcut/uploader.h"
#include "src/lib/util/encrypted_message_util.h"
#include "src/lib/util/file_system.h"
#include "src/lib/util/protected_fields.h"
#include "src/logger/internal_metrics.h"
#include "src/logging.h"
#include "src/observation_store/observation_store.h"
#include "src/observation_store/observation_store_update_recipient.h"
#include "src/system_data/configuration_data.h"
#include "src/uploader/upload_scheduler.h"

namespace cobalt::uploader {

// ShippingManager is the client-side component of Cobalt responsible for
// periodically sending Envelopes, encrypted to the Shuffler, from the device
// to the server. In Cobalt, Observations are accumulated in the Observation
// Store. The ShippingManager maintains a background thread that periodically
// reads Envelopes from the Observation Store, encrypts the Envelopes, and
// sends them to Cobalt's backend server. ShippingManager also performs
// expedited off-schedule sends when too much unsent Observation data has
// accumulated. A client may also explicitly request an expedited send.
//
// ShippingManager is an abstract class. Most of the logic is contained in
// ShippingManager itself but a subclass must be defined for each type of
// backend server to which we need to ship. This allows us to switch to
// a different type of backend server without changing this class.
//
// Usage: Construct an instance of a concrete subclass of ShippingManager,
// invoke Start() once. Whenever an observation is added to the
// ObservationStore, call NotifyObservationsAdded() which allows ShippingManager
// to check if it needs to send early. Optionally invoke RequestSendSoon() to
// expedite a send operation.
//
// Usually a single ShippingManager will be constructed for a device.
class ShippingManager : public observation_store::ObservationStoreUpdateRecipient {
 public:
  // Constructor
  //
  // upload_scheduler: This controls the ShippingManager's behavior with
  // respect to scheduling sends.
  //
  // observation_store: The ObservationStore from which Envelopes will be
  // retrieved.
  //
  // encrypt_to_analyzer: An EncryptedMessageMaker that will be used to encrypt the observations in
  // an envelope before sending. TODO(zmbush): add nullptr check once storing unencrypted
  // observations is enabled.
  ShippingManager(const UploadScheduler& upload_scheduler,
                  observation_store::ObservationStore* observation_store,
                  util::EncryptedMessageMaker* encrypt_to_analyzer);

  // The destructor will stop the worker thread and wait for it to stop
  // before exiting.
  ~ShippingManager() override;

  // Starts the worker thread. Destruct this instance to stop the worker thread.
  // This method must be invoked exactly once.
  void Start();

  // Invoke this each time an Observation is added to the ObservationStore.
  void NotifyObservationsAdded() override;

  // Register a request with the ShippingManager for an expedited send. The
  // ShippingManager's worker thread will try to send all of the accumulated,
  // unsent Observations as soon as possible but not sooner than |min_interval|
  // seconds after the previous send operation has completed.
  void RequestSendSoon();

  using SendCallback = std::function<void(bool)>;

  // A version of RequestSendSoon() that provides feedback about the send.
  // |send_callback| will be invoked with the result of the requested send
  // attempt. More precisely, send_callback will be invoked after the
  // ShippingManager has attempted to send all of the Observations that were
  // added to the ObservationStore. It will be invoked with true if all such
  // Observations were succesfully sent. It will be invoked with false if some
  // Observations were not able to be sent, but the status of any particular
  // Observation may not be determined. This is useful mainly in tests.
  void RequestSendSoon(const SendCallback& send_callback);

  // Blocks for |max_wait| seconds or until the worker thread has successfully
  // sent all previously added Observations and is idle, waiting for more
  // Observations to be added. This method is most useful if it can be arranged
  // that there are no concurrent invocations of NotifyObservationsAdded() (for
  // example in a test) because such concurrent invocations may cause the idle
  // state to never be entered.
  void WaitUntilIdle(std::chrono::seconds max_wait);

  // Blocks for |max_wait| seconds or until the worker thread is in the state
  // where there are Observations to be sent but it is waiting for the
  // next scheduled send time. This method is most useful if it can be
  // arranged that there are no concurrent invocations of RequestSendSoon()
  // (for example in a test) because such concurrent invocations might cause
  // that state to never be entered.
  void WaitUntilWorkerWaiting(std::chrono::seconds max_wait);

  // These diagnostic stats are mostly useful in a testing environment but
  // may possibly prove useful in production also.
  size_t num_send_attempts() const;
  size_t num_failed_attempts() const;
  cobalt::util::Status last_send_status() const;

  // Disable allows enabling/disabling the ShippingManager. When the ShippingManager is disabled,
  // all calls to SendAllEnvelopes will return immediately without uploading any data.
  void Disable(bool is_disabled);

  // Resets the internal metrics for the ShippingManager and the ClearcutUploader to use the
  // provided logger.
  virtual void ResetInternalMetrics(logger::LoggerInterface* internal_logger) = 0;

 private:
  friend class ClearcutV1ShippingManager;
  friend class LocalShippingManager;

  util::EncryptedMessageMaker* encrypt_to_analyzer_;

  // Has the ShippingManager been shut down?
  bool shut_down() const;

  // Causes the ShippingManager to shut down. Any active sends will be canceled.
  // All condition variables will be notified in order to wake up any waiting
  // threads. The worker thread will exit as soon as it can.
  void ShutDown();

  // The main method run by the worker thread. Executes a loop that
  // exits when ShutDown() is invoked.
  void Run();

  // Helper method used by Run(). Does not assume mutex_ lock is held.
  //
  // N.B. If the ShippingManager has been disabled (protected_fields_.is_disabled == true), this
  // method will do nothing, and will return immediately.
  void SendAllEnvelopes();

  // Invoked by SendAllEnvelopes() to actually perform the send..
  virtual std::unique_ptr<observation_store::ObservationStore::EnvelopeHolder>
  SendEnvelopeToBackend(
      std::unique_ptr<observation_store::ObservationStore::EnvelopeHolder> envelope_to_send) = 0;

  // Returns a name for this ShippingManager. Useful for log messages in cases
  // where the system is working with more than one.
  //
  // Technical note: We don't want this method to be pure virtual because
  // it may be invoked via a (virtual) destructor and this can lead to a
  // C++ runtime error indicated by a crash printing the message
  // "Pure virtual function called!"
  [[nodiscard]] virtual std::string name() const { return "ShippingManager"; }

  UploadScheduler upload_scheduler_;

  // Variables accessed only by the worker thread. These are not
  // protected by a mutex.
  std::chrono::system_clock::time_point next_scheduled_send_time_;

 private:
  // The background worker thread that runs the method "Run()."
  std::thread worker_thread_;

  // A struct that contains all the fields we want protected by a mutex.
  struct UnprotectedFields {
    explicit UnprotectedFields(observation_store::ObservationStore* store)
        : observation_store(store) {
      CHECK(observation_store);
    }

    bool expedited_send_requested = false;

    // The queue of callbacks that will be invoked when the next send
    // attempt completes.
    std::vector<SendCallback> send_callback_queue;

    // Set shut_down to true in order to stop "Run()".
    bool shut_down = false;

    // We initialize idle and waiting_for_schedule to true because initially the worker thread isn't
    // even started so WaitUntilIdle() and WaitUntilWorkerWaiting() should return immediately if
    // invoked. We will set them to false in Start().
    bool idle = true;
    bool waiting_for_schedule = true;

    // These diagnostic stats are mostly useful in a testing environment but
    // may possibly prove useful in production also.
    size_t num_send_attempts = 0;
    size_t num_failed_attempts = 0;
    cobalt::util::Status last_send_status;

    bool is_disabled = false;
    observation_store::ObservationStore* observation_store;

    std::condition_variable_any add_observation_notifier;
    std::condition_variable_any expedited_send_notifier;
    std::condition_variable_any shutdown_notifier;
    std::condition_variable_any idle_notifier;
    std::condition_variable_any waiting_for_schedule_notifier;

    [[nodiscard]] bool ObservationsAvailable() const {
      return !observation_store->Empty() && !is_disabled;
    }
  };
  using Fields = util::ProtectedFields<UnprotectedFields>;

  Fields protected_fields_;

 private:
  // Does the work of RequestSendSoon() and assumes that the fields->mutex lock
  // is held.
  void RequestSendSoonLockHeld(Fields::LockedFieldsPtr* fields);

  // InvokeSendCallbacksLockHeld invokes all SendCallbacks in
  // send_callback_queue, and also clears the send_callback_queue list.
  void InvokeSendCallbacksLockHeld(Fields::LockedFieldsPtr* fields, bool success);
};

// A concrete subclass of ShippingManager for sending data to Clearcut, which
// is the backend used by Cobalt 1.0.
class ClearcutV1ShippingManager : public ShippingManager {
 public:
  ClearcutV1ShippingManager(
      const UploadScheduler& upload_scheduler,
      observation_store::ObservationStore* observation_store,
      util::EncryptedMessageMaker* encrypt_to_shuffler,
      util::EncryptedMessageMaker* encrypt_to_analyzer,
      std::unique_ptr<lib::clearcut::ClearcutUploader> clearcut,
      int32_t log_source_id = system_data::defaultConfigurationData.GetLogSourceId(),
      logger::LoggerInterface* internal_logger = nullptr,
      size_t max_attempts_per_upload = lib::clearcut::kMaxRetries,
      std::string api_key = "cobalt-default-api-key");

  // The destructor will stop the worker thread and wait for it to stop
  // before exiting.
  ~ClearcutV1ShippingManager() override = default;

  // Resets the internal metrics for the ShippingManager and the ClearcutUploader to use the
  // provided logger.
  void ResetInternalMetrics(logger::LoggerInterface* internal_logger) override;

 private:
  std::unique_ptr<observation_store::ObservationStore::EnvelopeHolder> SendEnvelopeToBackend(
      std::unique_ptr<observation_store::ObservationStore::EnvelopeHolder> envelope_to_send)
      override;

  util::Status SendEnvelopeToClearcutDestination(const Envelope& envelope, size_t envelope_size);

  [[nodiscard]] std::string name() const override { return "ClearcutV1ShippingManager"; }

  const size_t max_attempts_per_upload_;

  std::mutex clearcut_mutex_;
  std::unique_ptr<lib::clearcut::ClearcutUploader> clearcut_;
  std::unique_ptr<logger::InternalMetrics> internal_metrics_;
  const std::string api_key_;
  util::EncryptedMessageMaker* encrypt_to_shuffler_;
  const int32_t log_source_id_;
};

// A concrete subclass of ShippingManager for capturing data locally to a file.
class LocalShippingManager : public ShippingManager {
 public:
  explicit LocalShippingManager(observation_store::ObservationStore* observation_store,
                                std::string output_file_path, util::FileSystem* fs);

  // DEPRECATED: Use non-owned FileSystem
  explicit LocalShippingManager(observation_store::ObservationStore* observation_store,
                                std::string output_file_path,
                                std::unique_ptr<util::FileSystem> owned_fs)
      : LocalShippingManager(observation_store, std::move(output_file_path), owned_fs.get()) {
    owned_fs_ = std::move(owned_fs);
  }

  explicit LocalShippingManager(observation_store::ObservationStore* observation_store,
                                util::EncryptedMessageMaker* encrypt_to_analyzer,
                                std::string output_file_path, util::FileSystem* fs);

  // DEPRECATED: Use non-owned FileSystem
  explicit LocalShippingManager(observation_store::ObservationStore* observation_store,
                                util::EncryptedMessageMaker* encrypt_to_analyzer,
                                std::string output_file_path,
                                std::unique_ptr<util::FileSystem> owned_fs)
      : LocalShippingManager(observation_store, encrypt_to_analyzer, std::move(output_file_path),
                             owned_fs.get()) {
    owned_fs_ = std::move(owned_fs);
  }

  // The destructor will stop the worker thread and wait for it to stop
  // before exiting.
  ~LocalShippingManager() override = default;

  // We don't want to track internal metrics for LocalShippingManager
  void ResetInternalMetrics(logger::LoggerInterface* internal_logger) override {}

 private:
  std::unique_ptr<observation_store::ObservationStore::EnvelopeHolder> SendEnvelopeToBackend(
      std::unique_ptr<observation_store::ObservationStore::EnvelopeHolder> envelope_to_send)
      override;

  [[nodiscard]] std::string name() const override { return "LocalShippingManager"; }

  std::string output_file_path_;
  std::unique_ptr<util::FileSystem> owned_fs_;
  util::FileSystem* fs_;
};

}  // namespace cobalt::uploader

namespace cobalt::encoder {
using uploader::ClearcutV1ShippingManager;
using uploader::LocalShippingManager;
using uploader::ShippingManager;
}  // namespace cobalt::encoder

#endif  // COBALT_SRC_UPLOADER_SHIPPING_MANAGER_H_

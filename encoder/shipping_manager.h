// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_SHIPPING_MANAGER_H_
#define COBALT_ENCODER_SHIPPING_MANAGER_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "./logging.h"
#include "encoder/envelope_maker.h"
#include "encoder/observation_store.h"
#include "encoder/observation_store_update_recipient.h"
#include "encoder/send_retryer.h"
#include "encoder/shuffler_client.h"
#include "encoder/upload_scheduler.h"
#include "logger/internal_metrics.h"
#include "third_party/clearcut/uploader.h"

namespace cobalt {
namespace encoder {

using send_retryer::SendRetryerInterface;

// ShippingManager is a central coordinator for collecting encoded Observations
// and sending them to the Shuffler. Observations are accumulated in the
// ObservationStore and periodically sent in batches to the Shuffler by a
// background worker thread on a regular schedule. ShippingManager also performs
// expedited off-schedule sends when too much unsent Observation data has
// accumulated.  A client may also explicitly request an expedited send.
//
// ShippingManager is used to upload data to a Shuffler. The unit of data sent
// in a single request is the *Envelope*. ShippingManager will get Envelopes
// from the ObservationStore, and attempt to send them.
//
// Usage: Construct a ShippingManager, invoke Start() once. Whenever an
// observation is added to the ObservationStore, call NotifyObservationsAdded()
// which allows ShippingManager to check if it needs to send early. Optionally
// invoke RequestSendSoon() to expedite a send operation.
//
// Usually a single ShippingManager will be constructed for each shuffler
// backend the client device wants to send to. All applications running on that
// device use the same set of ShippingManagers.
class ShippingManager : public ObservationStoreUpdateRecipient {
 public:
  // Constructor
  //
  // upload_scheduler: These control the ShippingManager's behavior with
  // respect to scheduling sends.
  //
  // observation_store: The ObservationStore used for storing and retrieving
  // observations.
  //
  // encrypt_to_shuffler: An util::EncryptedMessageMaker used to encrypt
  // messages to the shuffler and the analyzer.
  ShippingManager(const UploadScheduler& upload_scheduler,
                  ObservationStore* observation_store,
                  util::EncryptedMessageMaker* encrypt_to_shuffler);

  // The destructor will stop the worker thread and wait for it to stop
  // before exiting.
  virtual ~ShippingManager();

  // Starts the worker thread. Destruct this object to stop the worker thread.
  // This method must be invoked exactly once.
  void Start();

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
  void RequestSendSoon(SendCallback send_callback);

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
  size_t num_send_attempts();
  size_t num_failed_attempts();
  grpc::Status last_send_status();

 private:
  // Has the ShippingManager been shut down?
  bool shut_down();

  // Causes the ShippingManager to shut down. Any active sends by the
  // SendRetryer will be canceled. All condition variables will be notified
  // in order to wake up any waiting threads. The worker thread will exit as
  // soon as it can.
  void ShutDown();

  // The main method run by the worker thread. Executes a loop that
  // exits when ShutDown() is invoked.
  void Run();

  // Helper method used by Run(). Does not assume mutex_ lock is held.
  void SendAllEnvelopes();

  // Helper method used by Run(). Does not assume mutex_ lock is held.
  virtual std::unique_ptr<ObservationStore::EnvelopeHolder>
  SendEnvelopeToBackend(
      std::unique_ptr<ObservationStore::EnvelopeHolder> envelope_to_send) = 0;

  virtual std::string name() const = 0;

  UploadScheduler upload_scheduler_;

  // Variables accessed only by the worker thread. These are not
  // protected by a mutex.
  std::chrono::system_clock::time_point next_scheduled_send_time_;

 protected:
  util::EncryptedMessageMaker* encrypt_to_shuffler_;

  send_retryer::CancelHandle cancel_handle_;  // Not protected by a mutex. Only
                                              // accessed by the worker thread.

 private:
  // The background worker thread that runs the method "Run()."
  std::thread worker_thread_;

  // A struct that contains a mutex and all the fields we want to protect
  // with that mutex.
  struct MutexProtectedFields {
    // Protects access to all variables below.
    std::mutex mutex;

    // Variables accessed by the worker thread and the API
    // threads. These are protected by |mutex|.

    bool expedited_send_requested = false;

    // The queue of callbacks that will be invoked when the next send
    // attempt completes.
    std::vector<SendCallback> send_callback_queue;

    // Set shut_down to true in order to stop "Run()".
    bool shut_down = false;

    // We initialize idle_ and waiting_for_schedule_ to true because initially
    // the worker thread isn't even started so WaitUntilIdle() and
    // WaitUntilWorkerWaiting() should return immediately if invoked. We will
    // set them to false in Start().
    bool idle = true;
    bool waiting_for_schedule = true;

    // These diagnostic stats are mostly useful in a testing environment but
    // may possibly prove useful in production also.
    size_t num_send_attempts = 0;
    size_t num_failed_attempts = 0;
    grpc::Status last_send_status;

    ObservationStore* observation_store;

    std::condition_variable add_observation_notifier;
    std::condition_variable expedited_send_notifier;
    std::condition_variable shutdown_notifier;
    std::condition_variable idle_notifier;
    std::condition_variable waiting_for_schedule_notifier;
  };

  // Constructing a LockedFields object constructs a std::unique_lock and
  // therefore locks the mutex in |fields|.
  struct LockedFields {
    explicit LockedFields(MutexProtectedFields* fields)
        : lock(fields->mutex), fields(fields) {}
    std::unique_lock<std::mutex> lock;
    MutexProtectedFields* fields;  // not owned.
  };

  // All of the fields that are accessed by multiple threads and therefore
  // need to be protected by a mutex. Do not access this variable directly.
  // Instead invoke the method lock() which returns a smart pointer to a
  // |LockedFields| that wraps this field. All access to this field should
  // be via the following idiom:
  //
  // auto locked = lock();
  // locked->fields->[field_name].
  MutexProtectedFields _mutex_protected_fields_do_not_access_directly_;

 protected:
  // Provides access to the fields that are protected by a mutex while
  // acquiring a lock on the mutex. Destroy the returned std::unique_ptr to
  // release the mutex.
  std::unique_ptr<LockedFields> lock() {
    return std::unique_ptr<LockedFields>(
        new LockedFields(&_mutex_protected_fields_do_not_access_directly_));
  }

 private:
  // Does the work of RequestSendSoon() and assumes that the fields->mutex lock
  // is held.
  void RequestSendSoonLockHeld(MutexProtectedFields* fields);

  // InvokeSendCallbacksLockHeld invokes all SendCallbacks in
  // send_callback_queue, and also clears the send_callback_queue list.
  void InvokeSendCallbacksLockHeld(MutexProtectedFields* fields, bool success);
};

// LegacyShippingManager uses a SendRetryer to send Observations to the Shuffler
// so in case a send fails it will be retried multiple times with exponential
// back-off.
//
// LegacyShippingManager uses gRPC to send to the Shuffler. The unit of data
// sent in a single gRPC request is the *Envelope*. ShippingManager will access
// individual Envelopes by reading from the ObservationStore.
class LegacyShippingManager : public ShippingManager {
 public:
  // Parameters passed to the LegacyShippingManager constructor that will be
  // passed to the method SendRetryer::SendToShuffler(). See the documentation
  // of that method.
  class SendRetryerParams {
   public:
    SendRetryerParams(std::chrono::seconds initial_rpc_deadline,
                      std::chrono::seconds deadline_per_send_attempt)
        : initial_rpc_deadline_(initial_rpc_deadline),
          deadline_per_send_attempt_(deadline_per_send_attempt) {}

   private:
    friend class ShippingManager;
    friend class LegacyShippingManager;
    friend class ClearcutV1ShippingManager;
    std::chrono::seconds initial_rpc_deadline_;
    std::chrono::seconds deadline_per_send_attempt_;
  };

  // send_retryer_params: Used when the ShippingManager needs to invoke
  // SendRetryer::SendToShuffler().
  //
  // send_retryer: The instance of |SendRetryerInterface| encapsulated by
  // this ShippingManager. ShippingManager does not take ownership of
  // send_retryer which must outlive ShippingManager.
  LegacyShippingManager(const UploadScheduler& upload_scheduler,
                        ObservationStore* observation_store,
                        util::EncryptedMessageMaker* encrypt_to_shuffler,
                        const SendRetryerParams send_retryer_params,
                        SendRetryerInterface* send_retryer);

 private:
  const SendRetryerParams send_retryer_params_;

  SendRetryerInterface* send_retryer_;  // not owned

  std::unique_ptr<ObservationStore::EnvelopeHolder> SendEnvelopeToBackend(
      std::unique_ptr<ObservationStore::EnvelopeHolder> envelope_to_send)
      override;

  std::string name() const override { return "LegacyShippingManager"; }
};

class ClearcutV1ShippingManager : public ShippingManager {
 public:
  ClearcutV1ShippingManager(
      const UploadScheduler& upload_scheduler,
      ObservationStore* observation_store,
      util::EncryptedMessageMaker* encrypt_to_shuffler,
      std::unique_ptr<::clearcut::ClearcutUploader> clearcut,
      logger::LoggerInterface* internal_logger = nullptr);

  // Resets the internal metrics to use the provided logger.
  void ResetInternalMetrics(
      logger::LoggerInterface* internal_logger = nullptr) {
    internal_metrics_ = logger::InternalMetrics::NewWithLogger(internal_logger);
  }

 private:
  std::unique_ptr<ObservationStore::EnvelopeHolder> SendEnvelopeToBackend(
      std::unique_ptr<ObservationStore::EnvelopeHolder> envelope_to_send)
      override;

  std::string name() const override { return "ClearcutV1ShippingManager"; }

  std::mutex clearcut_mutex_;
  std::unique_ptr<::clearcut::ClearcutUploader> clearcut_;
  std::unique_ptr<logger::InternalMetrics> internal_metrics_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_SHIPPING_MANAGER_H_

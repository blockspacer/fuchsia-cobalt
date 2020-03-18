// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_PUBLIC_COBALT_SERVICE_INTERFACE_H_
#define COBALT_SRC_PUBLIC_COBALT_SERVICE_INTERFACE_H_

#include "src/lib/util/clock.h"
#include "src/logger/logger_interface.h"
#include "src/logger/project_context.h"
#include "src/system_data/system_data.h"

namespace cobalt {

// CobaltService is the primary public interface for Cobalt on client platforms.
class CobaltServiceInterface {
 public:
  virtual ~CobaltServiceInterface() = default;

  // NewLogger returns a new instance of a Logger object based on the provided |project_context|.
  virtual std::unique_ptr<logger::LoggerInterface> NewLogger(
      std::unique_ptr<logger::ProjectContext> project_context) = 0;

  // NewLogger returns a new instance of a Logger object using the customer_id and project_id based
  // on the registry provided in the config. If the project is not found, this will return nullptr.
  // If no registry was provided, this will crash.
  virtual std::unique_ptr<logger::LoggerInterface> NewLogger(uint32_t customer_id,
                                                             uint32_t project_id) = 0;

  // SystemClockIsAccurate lets CobaltService know that it no longer needs to maintain an
  // UndatedEventManager, and can flush the data from it into the observation store.
  //
  // This method should be used at most once in the lifetime of a CobaltServiceInterface object.
  //
  // |system_clock|: An instance of SystemClockInterface that is used to add a timestamp to all of
  // the events that were received before the system clock was made accurate. It is then given to
  // the EventAggregator if |start_event_aggregator_worker| is true.
  virtual void SystemClockIsAccurate(std::unique_ptr<util::SystemClockInterface> system_clock,
                                     bool start_event_aggregator_worker) = 0;

  // system_data returns a pointer to the internal SystemDataInterface object. This should only be
  // used for updating the Experiment state or channel in the system data.
  virtual system_data::SystemDataInterface* system_data() = 0;

  enum class DataCollectionPolicy {
    // In COLLECT_AND_UPLOAD mode, we collect and upload data as normal.
    COLLECT_AND_UPLOAD,

    // In DO_NOT_UPLOAD mode, we collect data but do not upload it. This is intended to be a
    // temporary state, and should be followed by setting the policy to either COLLECT_AND_UPLOAD or
    // DO_NOT_COLLECT.
    DO_NOT_UPLOAD,

    // In DO_NOT_COLLECT mode, we silently drop all logged events. When switching to DO_NOT_COLLECT
    // mode, all stored device-specific data will be deleted.
    DO_NOT_COLLECT,
  };

  // Sets the data collection policy.
  virtual void SetDataCollectionPolicy(DataCollectionPolicy policy) = 0;

  //
  // The remaining methods are mostly useful for testing the Cobalt system.
  //

  // Triggers an out of schedule generation of aggregate observations for rolling windows ending on
  // |final_day_index_utc|.
  //
  // Returns the result of the generation.
  //
  // This method is intended for use in the Cobalt testapps which require a single thread to
  // both log events to and generate Observations from an EventAggregator.
  virtual logger::Status GenerateAggregatedObservations(uint32_t final_day_index_utc) = 0;

  // Returns a count of the number of times the aggregation has occurred. This is likely
  // only useful in testing to verify that the worker thread is not running too frequently.
  [[nodiscard]] virtual uint64_t num_aggregator_runs() const = 0;

  // Returns the number of Observations that have been added to the ObservationStore.
  [[nodiscard]] virtual uint64_t num_observations_added() const = 0;

  // Returns a vector containing the number of Observations that have been added
  // to the ObservationStore for each specified report ID.
  [[nodiscard]] virtual std::vector<uint64_t> num_observations_added_for_reports(
      const std::vector<uint32_t>& report_ids) const = 0;

  using SendCallback = std::function<void(bool)>;

  // Register a request for an expedited send of observations to the server. The
  // accumulated unsent Observations will be sent as soon as possible (with some rate limiting).
  //
  // |send_callback| will be invoked with the result of the requested send
  // attempt. More precisely, send_callback will be invoked after the
  // attempt to send all of the outstanding Observations. It will be invoked with true if all such
  // Observations were successfully sent. It will be invoked with false if some
  // Observations were not able to be sent, but the status of any particular
  // Observation may not be determined. This is useful mainly in tests.
  virtual void ShippingRequestSendSoon(const SendCallback& send_callback) = 0;

  // Blocks for |max_wait| seconds or until the sending of all previously added Observations is
  // complete.
  virtual void WaitUntilShippingIdle(std::chrono::seconds max_wait) = 0;

  // These diagnostic stats are mostly useful in a testing environment but
  // may possibly prove useful in production also.
  [[nodiscard]] virtual size_t num_shipping_send_attempts() const = 0;
  [[nodiscard]] virtual size_t num_shipping_failed_attempts() const = 0;
};

}  // namespace cobalt

#endif  // COBALT_SRC_PUBLIC_COBALT_SERVICE_INTERFACE_H_

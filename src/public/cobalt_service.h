// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_PUBLIC_COBALT_SERVICE_H_
#define COBALT_SRC_PUBLIC_COBALT_SERVICE_H_

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

#include "src/lib/clearcut/http_client.h"
#include "src/lib/util/clock.h"
#include "src/lib/util/consistent_proto_store.h"
#include "src/lib/util/encrypted_message_util.h"
#include "src/local_aggregation/event_aggregator_mgr.h"
#include "src/logger/logger.h"
#include "src/logger/observation_writer.h"
#include "src/logger/project_context.h"
#include "src/logger/undated_event_manager.h"
#include "src/observation_store/observation_store.h"
#include "src/public/cobalt_config.h"
#include "src/system_data/client_secret.h"
#include "src/system_data/system_data.h"
#include "src/uploader/shipping_manager.h"

namespace cobalt {

// CobaltService is the primary public interface for Cobalt on client platforms.
//
// It is constructed using a CobaltConfig struct, which provides all available customization options
// for the behavior of Cobalt.
//
// Example:
//
// CobaltConfig cfg;
// cfg.product_name = "product";
// cfg.version = "version";
// ... continue setting config values ...
//
// CobaltService service(std::move(cfg));
//
// // Get a logger:
//
// auto logger = service.NewLogger(project_context);
// logger.LogEvent(Event);
//
class CobaltService {
 public:
  explicit CobaltService(CobaltConfig cfg);

  // NewLogger returns a new instance of a Logger object based on the provided |project_context|.
  std::unique_ptr<logger::Logger> NewLogger(
      std::unique_ptr<logger::ProjectContext> project_context);

 private:
  std::unique_ptr<logger::Logger> NewLogger(std::unique_ptr<logger::ProjectContext> project_context,
                                            bool include_internal_logger);

 public:
  // SystemClockIsAccurate lets CobaltService know that it no longer needs to maintain an
  // UndatedEventManager, and can flush the data from it into the observation store.
  //
  // This method should be used at most once in the lifetime of a CobaltService object.
  //
  // |system_clock|: An instance of SystemClockInterface that is used to add a timestamp to all of
  // the events that were received before the system clock was made accurate. It is then given to
  // the EventAggregator if |start_event_aggregator_worker| is true.
  void SystemClockIsAccurate(std::unique_ptr<util::SystemClockInterface> system_clock,
                             bool start_event_aggregator_worker);

  // system_data returns a pointer to the internal SystemData object. This should only be used for
  // updating the Expirement state or channel in SystemData.
  system_data::SystemData *system_data() { return &system_data_; }

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
  void SetDataCollectionPolicy(DataCollectionPolicy policy);

 private:
  friend class internal::RealLoggerFactory;
  friend class CobaltControllerImpl;

  observation_store::ObservationStore *observation_store() { return observation_store_.get(); }

  local_aggregation::EventAggregatorManager *event_aggregator_manager() {
    return &event_aggregator_manager_;
  }

  uploader::ShippingManager *shipping_manager() { return shipping_manager_.get(); }

  logger::UndatedEventManager *undated_event_manager() { return undated_event_manager_.get(); }

  void ResetLocalAggregation() { event_aggregator_manager_.Reset(); }

  std::unique_ptr<util::FileSystem> fs_;
  system_data::SystemData system_data_;
  std::unique_ptr<observation_store::ObservationStore> observation_store_;
  std::unique_ptr<util::EncryptedMessageMaker> encrypt_to_analyzer_;
  std::unique_ptr<util::EncryptedMessageMaker> encrypt_to_shuffler_;
  std::unique_ptr<uploader::ShippingManager> shipping_manager_;
  logger::Encoder logger_encoder_;
  logger::ObservationWriter observation_writer_;
  local_aggregation::EventAggregatorManager event_aggregator_manager_;
  std::shared_ptr<logger::UndatedEventManager> undated_event_manager_;
  std::unique_ptr<util::ValidatedClockInterface> validated_clock_;
  std::unique_ptr<logger::LoggerInterface> internal_logger_;
};

}  // namespace cobalt

#endif  // COBALT_SRC_PUBLIC_COBALT_SERVICE_H_

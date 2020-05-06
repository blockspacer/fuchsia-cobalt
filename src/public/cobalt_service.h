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
#include "src/logger/project_context_factory.h"
#include "src/logger/undated_event_manager.h"
#include "src/observation_store/observation_store.h"
#include "src/public/cobalt_config.h"
#include "src/public/cobalt_service_interface.h"
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
class CobaltService : public CobaltServiceInterface {
 public:
  explicit CobaltService(CobaltConfig cfg);

  // NewLogger returns a new instance of a Logger object based on the provided |project_context|.
  std::unique_ptr<logger::LoggerInterface> NewLogger(
      std::unique_ptr<logger::ProjectContext> project_context) override;

  std::unique_ptr<logger::LoggerInterface> NewLogger(uint32_t customer_id,
                                                     uint32_t project_id) override;

 private:
  std::unique_ptr<logger::Logger> NewLogger(std::unique_ptr<logger::ProjectContext> project_context,
                                            bool include_internal_logger);

  std::unique_ptr<logger::Logger> NewLogger(uint32_t customer_id, uint32_t project_id,
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
                             bool start_event_aggregator_worker) override;

  // system_data returns a pointer to the internal SystemData object. This should only be used for
  // updating the Expirement state or channel in SystemData.
  system_data::SystemDataInterface *system_data() override { return &system_data_; }

  // Sets the data collection policy.
  void SetDataCollectionPolicy(DataCollectionPolicy policy) override;

  logger::Status GenerateAggregatedObservations(uint32_t final_day_index_utc) override {
    return event_aggregator_manager_.GenerateObservationsNoWorker(final_day_index_utc);
  }

  [[nodiscard]] uint64_t num_aggregator_runs() const override {
    return event_aggregator_manager_.num_runs();
  }

  [[nodiscard]] uint64_t num_observations_added() const override {
    return observation_store_->num_observations_added();
  }

  [[nodiscard]] std::vector<uint64_t> num_observations_added_for_reports(
      const std::vector<uint32_t> &report_ids) const override {
    return observation_store_->num_observations_added_for_reports(report_ids);
  }

  void ShippingRequestSendSoon(const SendCallback &send_callback) override {
    shipping_manager_->RequestSendSoon(send_callback);
  }

  void WaitUntilShippingIdle(std::chrono::seconds max_wait) override {
    shipping_manager_->WaitUntilIdle(max_wait);
  }

  [[nodiscard]] size_t num_shipping_send_attempts() const override {
    return shipping_manager_->num_send_attempts();
  }
  [[nodiscard]] size_t num_shipping_failed_attempts() const override {
    return shipping_manager_->num_failed_attempts();
  }

  bool has_internal_logger() const { return internal_logger_ != nullptr; }

 private:
  friend class internal::RealLoggerFactory;
  friend class CobaltControllerImpl;

  observation_store::ObservationStore *observation_store() { return observation_store_.get(); }

  local_aggregation::EventAggregatorManager *event_aggregator_manager() {
    return &event_aggregator_manager_;
  }

  uploader::ShippingManager *shipping_manager() { return shipping_manager_.get(); }

  std::unique_ptr<util::FileSystem> fs_;
  system_data::SystemData system_data_;
  std::unique_ptr<logger::ProjectContextFactory> global_project_context_factory_;
  std::unique_ptr<observation_store::ObservationStore> observation_store_;
  std::unique_ptr<util::EncryptedMessageMaker> encrypt_to_analyzer_;
  std::unique_ptr<util::EncryptedMessageMaker> encrypt_to_shuffler_;
  std::unique_ptr<uploader::ShippingManager> shipping_manager_;
  logger::Encoder logger_encoder_;
  logger::ObservationWriter observation_writer_;
  local_aggregation::EventAggregatorManager event_aggregator_manager_;
  std::shared_ptr<logger::UndatedEventManager> undated_event_manager_;
  util::ValidatedClockInterface *validated_clock_;
  std::unique_ptr<logger::LoggerInterface> internal_logger_;
};

}  // namespace cobalt

#endif  // COBALT_SRC_PUBLIC_COBALT_SERVICE_H_

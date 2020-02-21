// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_PUBLIC_TESTING_FAKE_COBALT_SERVICE_H_
#define COBALT_SRC_PUBLIC_TESTING_FAKE_COBALT_SERVICE_H_

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

#include "src/lib/util/clock.h"
#include "src/lib/util/encrypted_message_util.h"
#include "src/logger/fake_logger.h"
#include "src/logger/project_context.h"
#include "src/public/cobalt_config.h"
#include "src/public/cobalt_service_interface.h"
#include "src/system_data/fake_system_data.h"
#include "src/system_data/system_data.h"

namespace cobalt::testing {

// A fake for the CobaltService, to use in testing where logged events should not be sent.
//
// This class uses the FakeLogger to return loggers from NewLogger.
//
// Example:
//
// FakeCobaltService fake_service;
//
// // Get a logger and log an Event:
// auto logger = fake_service.NewLogger(project_context);
// logger.LogEvent(Event);
//
// // Verify the logger calls:
// EXPECT_EQ(fake_service_->last_logger_created()->call_count(), 1);
//
class FakeCobaltService : public CobaltServiceInterface {
 public:
  // Returns a new instance of a FakeLogger object for use in testing.
  std::unique_ptr<logger::LoggerInterface> NewLogger(
      std::unique_ptr<logger::ProjectContext> project_context) override {
    auto logger = std::make_unique<cobalt::logger::testing::FakeLogger>();
    last_logger_created_ = logger.get();
    return std::move(logger);
  }

  // Records the accuracy of the system clock for testing.
  void SystemClockIsAccurate(std::unique_ptr<util::SystemClockInterface> system_clock,
                             bool start_event_aggregator_worker) override {
    system_clock_is_accurate_ = true;
  }

  // Records the data collection policy for testing.
  void SetDataCollectionPolicy(DataCollectionPolicy policy) override {
    data_collection_policy_ = policy;
  }

  system_data::SystemDataInterface* system_data() override { return &system_data_; }

  //
  // The remaining methods from the CobaltServiceInterface are not used in testing with this fake,
  // so they are mostly unimplemented.
  //

  logger::Status GenerateAggregatedObservations(uint32_t final_day_index_utc) override {
    return logger::Status::kOK;
  }

  [[nodiscard]] uint64_t num_aggregator_runs() const override { return 0; }

  [[nodiscard]] uint64_t num_observations_added() const override { return 0; }

  [[nodiscard]] std::vector<uint64_t> num_observations_added_for_reports(
      const std::vector<uint32_t>& report_ids) const override {
    return {};
  }

  void ShippingRequestSendSoon(const SendCallback& send_callback) override {}

  void WaitUntilShippingIdle(std::chrono::seconds max_wait) override {}

  [[nodiscard]] size_t num_shipping_send_attempts() const override { return 0; }
  [[nodiscard]] size_t num_shipping_failed_attempts() const override { return 0; }

  //
  // Remaining public methods are for determining the current state of the fake.
  //

  DataCollectionPolicy data_collection_policy() { return data_collection_policy_; }

  cobalt::logger::testing::FakeLogger* last_logger_created() { return last_logger_created_; }

  bool system_clock_is_accurate() { return system_clock_is_accurate_; }

 private:
  cobalt::system_data::FakeSystemData system_data_;
  DataCollectionPolicy data_collection_policy_;
  cobalt::logger::testing::FakeLogger* last_logger_created_;
  bool system_clock_is_accurate_ = false;
};

}  // namespace cobalt::testing

#endif  // COBALT_SRC_PUBLIC_TESTING_FAKE_COBALT_SERVICE_H_

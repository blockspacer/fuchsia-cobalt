// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOCAL_AGGREGATION_TEST_UTILS_TEST_EVENT_AGGREGATOR_MGR_H_
#define COBALT_SRC_LOCAL_AGGREGATION_TEST_UTILS_TEST_EVENT_AGGREGATOR_MGR_H_

#include "src/local_aggregation/event_aggregator_mgr.h"

namespace cobalt::local_aggregation {

// Class to be used in testing to access the internal state of the tested objects.
class TestEventAggregatorManager : public EventAggregatorManager {
 public:
  TestEventAggregatorManager(
      const logger::Encoder* encoder, const logger::ObservationWriter* observation_writer,
      util::ConsistentProtoStore* local_aggregate_proto_store,
      util::ConsistentProtoStore* obs_history_proto_store, size_t backfill_days = 0,
      std::chrono::seconds aggregate_backup_interval = std::chrono::minutes(1),
      std::chrono::seconds generate_obs_interval = std::chrono::hours(1),
      std::chrono::seconds gc_interval = std::chrono::hours(24))
      : EventAggregatorManager(encoder, observation_writer, local_aggregate_proto_store,
                               obs_history_proto_store, backfill_days, aggregate_backup_interval,
                               generate_obs_interval, gc_interval) {}

  TestEventAggregatorManager(const CobaltConfig& cfg, util::FileSystem* fs,
                             const logger::Encoder* encoder,
                             const logger::ObservationWriter* observation_writer)
      : EventAggregatorManager(cfg, fs, encoder, observation_writer) {}

  // Triggers an out of schedule run of GenerateObservations(). This does not change the schedule
  // of future runs.
  logger::Status GenerateObservations(uint32_t day_index_utc, uint32_t day_index_local = 0u) {
    return EventAggregatorManager::aggregate_store_->GenerateObservations(day_index_utc,
                                                                          day_index_local);
  }

  // Triggers an out of schedule run of GarbageCollect(). This does not change the schedule of
  // future runs.
  logger::Status GarbageCollect(uint32_t day_index_utc, uint32_t day_index_local = 0u) {
    return EventAggregatorManager::aggregate_store_->GarbageCollect(day_index_utc, day_index_local);
  }

  // Returns the number of aggregates of type per_device_numeric_aggregates.
  uint32_t NumPerDeviceNumericAggregatesInStore() {
    int count = 0;
    for (const auto& aggregates :
         EventAggregatorManager::aggregate_store_->protected_aggregate_store_.lock()
             ->local_aggregate_store.by_report_key()) {
      if (aggregates.second.has_numeric_aggregates()) {
        count += aggregates.second.numeric_aggregates().by_component().size();
      }
    }
    return count;
  }

  // Sets the EventAggregatorManager's SteadyClockInterface.
  void SetSteadyClock(util::SteadyClockInterface* clock) { steady_clock_.reset(clock); }
};

}  // namespace cobalt::local_aggregation

#endif  // COBALT_SRC_LOCAL_AGGREGATION_TEST_UTILS_TEST_EVENT_AGGREGATOR_MGR_H_

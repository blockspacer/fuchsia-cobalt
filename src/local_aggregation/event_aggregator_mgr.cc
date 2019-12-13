// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/local_aggregation/event_aggregator_mgr.h"

namespace cobalt::local_aggregation {

using util::ConsistentProtoStore;

EventAggregatorManager::EventAggregatorManager(const logger::Encoder* encoder,
                                               const logger::ObservationWriter* observation_writer,
                                               ConsistentProtoStore* local_aggregate_proto_store,
                                               ConsistentProtoStore* obs_history_proto_store,
                                               const size_t backfill_days,
                                               const std::chrono::seconds aggregate_backup_interval,
                                               const std::chrono::seconds generate_obs_interval,
                                               const std::chrono::seconds gc_interval) {
  event_aggregator_ = std::make_unique<EventAggregator>(
      encoder, observation_writer, local_aggregate_proto_store, obs_history_proto_store,
      backfill_days, aggregate_backup_interval, generate_obs_interval, gc_interval);
}

void EventAggregatorManager::Start(std::unique_ptr<util::SystemClockInterface> clock) {
  event_aggregator_->Start(std::move(clock));
}

logger::Status EventAggregatorManager::GenerateObservationsNoWorker(
    uint32_t final_day_index_utc, uint32_t final_day_index_local) {
  return event_aggregator_->GenerateObservationsNoWorker(final_day_index_utc,
                                                         final_day_index_local);
}
}  // namespace cobalt::local_aggregation

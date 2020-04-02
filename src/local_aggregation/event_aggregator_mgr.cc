// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/local_aggregation/event_aggregator_mgr.h"

#include <memory>

#include "src/lib/util/consistent_proto_store.h"
#include "src/lib/util/datetime_util.h"
#include "src/lib/util/file_system.h"

namespace cobalt::local_aggregation {

using logger::kOK;
using logger::kOther;
using logger::Status;
using util::ConsistentProtoStore;
using util::SteadyClock;
using util::TimeToDayIndex;

const std::chrono::seconds EventAggregatorManager::kDefaultAggregateBackupInterval =
    std::chrono::minutes(1);
const std::chrono::seconds EventAggregatorManager::kDefaultGenerateObsInterval =
    std::chrono::hours(1);
const std::chrono::seconds EventAggregatorManager::kDefaultGCInterval = std::chrono::hours(24);

EventAggregatorManager::EventAggregatorManager(const CobaltConfig& cfg, util::FileSystem* fs,
                                               const logger::Encoder* encoder,
                                               const logger::ObservationWriter* observation_writer)
    : encoder_(encoder),
      observation_writer_(observation_writer),
      backfill_days_(cfg.local_aggregation_backfill_days),
      aggregate_backup_interval_(kDefaultAggregateBackupInterval),
      generate_obs_interval_(kDefaultGenerateObsInterval),
      gc_interval_(kDefaultGCInterval),
      owned_local_aggregate_proto_store_(
          new ConsistentProtoStore(cfg.local_aggregate_proto_store_path, fs)),
      owned_obs_history_proto_store_(
          new ConsistentProtoStore(cfg.obs_history_proto_store_path, fs)) {
  Reset();
}

void EventAggregatorManager::Start(std::unique_ptr<util::SystemClockInterface> clock) {
  auto locked = protected_worker_thread_controller_.lock();
  locked->shut_down = false;
  std::thread t(std::bind(
      [this](std::unique_ptr<util::SystemClockInterface>& clock) { this->Run(std::move(clock)); },
      std::move(clock)));
  worker_thread_ = std::move(t);
}

void EventAggregatorManager::ShutDown() {
  if (worker_thread_.joinable()) {
    {
      auto locked = protected_worker_thread_controller_.lock();
      locked->shut_down = true;
      locked->wakeup_notifier.notify_all();
    }
    worker_thread_.join();
  } else {
    protected_worker_thread_controller_.lock()->shut_down = true;
  }
}

void EventAggregatorManager::Run(std::unique_ptr<util::SystemClockInterface> system_clock) {
  std::chrono::steady_clock::time_point steady_time = steady_clock_->now();
  // Schedule Observation generation to happen in the first cycle.
  next_generate_obs_ = steady_time;
  // Schedule garbage collection to happen |gc_interval_| seconds from now.
  next_gc_ = steady_time + gc_interval_;
  // Acquire the mutex protecting the shutdown flag and condition variable.
  auto locked = protected_worker_thread_controller_.lock();
  while (true) {
    num_runs_++;

    // If shutdown has been requested, back up the LocalAggregateStore and
    // exit.
    if (locked->shut_down) {
      aggregate_store_->BackUpLocalAggregateStore();
      return;
    }
    // Sleep until the next scheduled backup of the LocalAggregateStore or
    // until notified of shutdown. Back up the LocalAggregateStore after
    // waking.
    locked->wakeup_notifier.wait_for(locked, aggregate_backup_interval_, [&locked]() {
      if (locked->immediate_run_trigger) {
        locked->immediate_run_trigger = false;
        return true;
      }
      return locked->shut_down || locked->back_up_now;
    });
    aggregate_store_->BackUpLocalAggregateStore();
    if (locked->back_up_now) {
      locked->back_up_now = false;
      aggregate_store_->BackUpObservationHistory();
    }
    // If the worker thread was woken up by a shutdown request, exit.
    // Otherwise, complete any scheduled Observation generation and garbage
    // collection.
    if (locked->shut_down) {
      return;
    }
    // Check whether it is time to generate Observations or to garbage-collect
    // the LocalAggregate store. If so, do that task and schedule the next
    // occurrence.
    DoScheduledTasks(system_clock->now(), steady_clock_->now());
  }
}

void EventAggregatorManager::DoScheduledTasks(std::chrono::system_clock::time_point system_time,
                                              std::chrono::steady_clock::time_point steady_time) {
  auto current_time_t = std::chrono::system_clock::to_time_t(system_time);
  auto yesterday_utc = TimeToDayIndex(current_time_t, MetricDefinition::UTC) - 1;
  auto yesterday_local_time = TimeToDayIndex(current_time_t, MetricDefinition::LOCAL) - 1;

  // Skip the tasks (but do schedule a retry) if either day index is too small.
  uint32_t min_allowed_day_index = kMaxAllowedAggregationDays + backfill_days_;
  bool skip_tasks =
      (yesterday_utc < min_allowed_day_index || yesterday_local_time < min_allowed_day_index);
  if (steady_time >= next_generate_obs_) {
    next_generate_obs_ += generate_obs_interval_;
    if (skip_tasks) {
      LOG_FIRST_N(ERROR, 10) << "EventAggregator is skipping Observation generation because the "
                                "current day index is too small.";
    } else {
      auto obs_status = aggregate_store_->GenerateObservations(yesterday_utc, yesterday_local_time);
      if (obs_status == kOK) {
        aggregate_store_->BackUpObservationHistory();
      } else {
        LOG(ERROR) << "GenerateObservations failed with status: " << obs_status;
      }
    }
  }
  if (steady_time >= next_gc_) {
    next_gc_ += gc_interval_;
    if (skip_tasks) {
      LOG_FIRST_N(ERROR, 10) << "EventAggregator is skipping garbage collection because the "
                                "current day index is too small.";
    } else {
      auto gc_status = aggregate_store_->GarbageCollect(yesterday_utc, yesterday_local_time);
      if (gc_status == kOK) {
        aggregate_store_->BackUpLocalAggregateStore();
      } else {
        LOG(ERROR) << "GarbageCollect failed with status: " << gc_status;
      }
    }
  }
}

logger::Status EventAggregatorManager::GenerateObservationsNoWorker(
    uint32_t final_day_index_utc, uint32_t final_day_index_local) {
  if (worker_thread_.joinable()) {
    LOG(ERROR) << "GenerateObservationsNoWorker() was called while "
                  "worker thread was running.";
    return kOther;
  }
  return aggregate_store_->GenerateObservations(final_day_index_utc, final_day_index_local);
}

void EventAggregatorManager::TriggerBackups() {
  auto locked = protected_worker_thread_controller_.lock();
  locked->back_up_now = true;
  locked->wakeup_notifier.notify_all();
}

void EventAggregatorManager::Reset() {
  aggregate_store_ = std::make_unique<AggregateStore>(
      encoder_, observation_writer_, owned_local_aggregate_proto_store_.get(),
      owned_obs_history_proto_store_.get(), backfill_days_);

  event_aggregator_ = std::make_unique<EventAggregator>(aggregate_store_.get());
  steady_clock_ = std::make_unique<SteadyClock>();
}

}  // namespace cobalt::local_aggregation

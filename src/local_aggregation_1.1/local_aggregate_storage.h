// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOCAL_AGGREGATION_1_1_LOCAL_AGGREGATE_STORAGE_H_
#define COBALT_SRC_LOCAL_AGGREGATION_1_1_LOCAL_AGGREGATE_STORAGE_H_

#include <cstdint>
#include <string>

#include "src/lib/util/consistent_proto_store.h"
#include "src/lib/util/file_system.h"
#include "src/local_aggregation_1.1/local_aggregation.pb.h"
#include "src/logger/project_context_factory.h"
#include "src/registry/cobalt_registry.pb.h"

namespace cobalt::local_aggregation {

class LocalAggregateStorage {
 public:
  // Constructor for a LocalAggregateStorage object
  //
  // |base_directory|: The absolute path to the directory where the local aggregation files are
  //                   stored.
  // |fs|: An instance of the FileSystem interface. Used for reading/writing files.
  // |global_project_context_factory|: The current global registry.
  LocalAggregateStorage(std::string base_directory, util::FileSystem *fs,
                        const logger::ProjectContextFactory *global_project_context_factory);

  // GetMetricAggregate returns a pointer to the live, mutable MetricAggregate that was requested.
  // If no such aggregate exists, nullptr will be returned.
  //
  // Note: After modifying the MetricAggregate returned by this function, the user is expected to
  // call 'SaveMetricAggregate' so that the modified values can be persisted to disk.
  MetricAggregate *GetMetricAggregate(uint32_t customer_id, uint32_t project_id,
                                      uint32_t metric_id);

  // SaveMetricAggregate writes the current state of the MetricAggregate for the given
  // (customer, project, metric) tuple to disk.
  //
  // Note: This should be called after modifying the MetricAggregate returned by GetMetricAggregate.
  util::Status SaveMetricAggregate(uint32_t customer_id, uint32_t project_id, uint32_t metric_id);

 private:
  // LoadData walks the filesystem from the |base_directory_| down and reads the existing
  // MetricAggregate checkpoint files into the |aggregates_| object. Any customer/project ids that
  // are not present in the regsitry will be ignored.
  //
  // TODO(zmbush): In the future non-present metrics will also be ignored.
  // TODO(zmbush): In future these ignored files/directories will be deleted.
  void LoadData();

  // InitializePersistentStore iterates through the registry and creates the customer/project
  // directories for all of the metrics in the registry (if they don't already exist). Additionally,
  // it adds empty MetricAggregate objects to the |aggregates_| object. It does not create metric
  // files, since at this point they are guaranteed to be empty.
  void InitializePersistentStore();

  const std::string base_directory_;
  util::FileSystem *fs_;
  util::ConsistentProtoStore proto_store_;
  const logger::ProjectContextFactory *global_project_context_factory_;
  std::map<std::tuple<uint32_t, uint32_t, uint32_t>, MetricAggregate> aggregates_;
};

}  // namespace cobalt::local_aggregation

#endif  // COBALT_SRC_LOCAL_AGGREGATION_1_1_LOCAL_AGGREGATE_STORAGE_H_

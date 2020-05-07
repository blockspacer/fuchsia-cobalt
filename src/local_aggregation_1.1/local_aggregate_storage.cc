// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/local_aggregation_1.1/local_aggregate_storage.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_join.h"
#include "src/logger/project_context_factory.h"
#include "src/logging.h"

namespace cobalt::local_aggregation {

LocalAggregateStorage::LocalAggregateStorage(
    std::string base_directory, util::FileSystem *fs,
    const logger::ProjectContextFactory *global_project_context_factory)
    : base_directory_(std::move(base_directory)),
      fs_(fs),
      proto_store_(fs_),
      global_project_context_factory_(global_project_context_factory) {
  CHECK(global_project_context_factory_)
      << "No global_project_context_factory provided. Cannot initialize store!";

  InitializePersistentStore();

  // TODO(zmbush): Look into lazy-loading
  LoadData();
}

void LocalAggregateStorage::LoadData() {
  for (const auto &customer_folder : fs_->ListFiles(base_directory_).ConsumeValueOr({})) {
    auto customer_dir = absl::StrCat(base_directory_, "/", customer_folder);

    uint32_t customer_id;
    if (!absl::SimpleAtoi(customer_folder, &customer_id)) {
      LOG(WARNING) << "Found bad customer folder name: " << customer_folder;
      continue;
    }

    for (const auto &project_folder : fs_->ListFiles(customer_dir).ConsumeValueOr({})) {
      auto project_dir = absl::StrCat(customer_dir, "/", project_folder);

      uint32_t project_id;
      if (!absl::SimpleAtoi(project_folder, &project_id)) {
        LOG(WARNING) << "Found bad project folder name: " << project_folder;
        continue;
      }

      auto project_context =
          global_project_context_factory_->NewProjectContext(customer_id, project_id);
      if (!project_context) {
        LOG(WARNING) << "Found customer/project that is not present in the registry: "
                     << customer_id << " / " << project_id;
        // TODO(zmbush): Delete the directory, and all sub-files. For now, we just won't load them
        continue;
      }

      for (const auto &metric_file : fs_->ListFiles(project_dir).ConsumeValueOr({})) {
        uint32_t metric_id;
        if (!absl::SimpleAtoi(metric_file, &metric_id)) {
          LOG(WARNING) << "Found bad metric file name: " << metric_id;
          continue;
        }

        auto status =
            proto_store_.Read(absl::StrCat(project_dir, "/", metric_file),
                              &aggregates_[std::make_tuple(customer_id, project_id, metric_id)]);
        if (!status.ok()) {
          LOG(WARNING) << "Failed to load aggregate file: " << project_dir << "/" << metric_file
                       << ": " << status.error_message();
        }
      }
    }
  }
}

void LocalAggregateStorage::InitializePersistentStore() {
  for (const auto &project : global_project_context_factory_->ListProjects()) {
    auto customer_id = std::get<0>(project);
    auto project_id = std::get<1>(project);

    auto customer_dir = absl::StrCat(base_directory_, "/", customer_id);
    if (!fs_->FileExists(customer_dir)) {
      fs_->MakeDirectory(customer_dir);
    }

    auto project_dir = absl::StrCat(customer_dir, "/", project_id);
    if (!fs_->FileExists(project_dir)) {
      fs_->MakeDirectory(project_dir);
    }

    auto ctx = global_project_context_factory_->NewProjectContext(customer_id, project_id);
    for (const auto &metric_id : ctx->ListMetrics()) {
      auto tuple = std::make_tuple(customer_id, project_id, metric_id);
      auto aggregate = aggregates_.find(tuple);
      if (aggregate == aggregates_.end()) {
        aggregates_[tuple] = MetricAggregate();
      }
    }
  }
}

MetricAggregate *LocalAggregateStorage::GetMetricAggregate(const uint32_t customer_id,
                                                           const uint32_t project_id,
                                                           const uint32_t metric_id) {
  auto aggregate = aggregates_.find(std::make_tuple(customer_id, project_id, metric_id));
  if (aggregate != aggregates_.end()) {
    return &aggregate->second;
  }
  return nullptr;
}

util::Status LocalAggregateStorage::SaveMetricAggregate(const uint32_t customer_id,
                                                        const uint32_t project_id,
                                                        const uint32_t metric_id) {
  auto aggregate = aggregates_.find(std::make_tuple(customer_id, project_id, metric_id));
  if (aggregate != aggregates_.end()) {
    return proto_store_.Write(
        absl::StrCat(base_directory_, "/", customer_id, "/", project_id, "/", metric_id),
        aggregate->second);
  }
  return util::Status(util::StatusCode::NOT_FOUND, "No matching metric aggregate found");
}

}  // namespace cobalt::local_aggregation

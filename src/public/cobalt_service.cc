// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/public/cobalt_service.h"

#include <memory>

#include "src/lib/util/clock.h"
#include "src/lib/util/encrypted_message_util.h"
#include "src/logger/internal_metrics_config.cb.h"
#include "src/logger/project_context.h"
#include "src/observation_store/file_observation_store.h"
#include "src/observation_store/memory_observation_store.h"
#include "src/observation_store/observation_store.h"
#include "src/system_data/configuration_data.h"
#include "src/uploader/shipping_manager.h"

namespace cobalt {

namespace {

std::unique_ptr<observation_store::ObservationStore> NewObservationStore(const CobaltConfig &cfg,
                                                                         util::FileSystem *fs) {
  if (cfg.use_memory_observation_store) {
    return std::make_unique<observation_store::MemoryObservationStore>(
        cfg.max_bytes_per_event, cfg.max_bytes_per_envelope, cfg.max_bytes_total);
  }
  return std::make_unique<observation_store::FileObservationStore>(
      cfg.max_bytes_per_event, cfg.max_bytes_per_envelope, cfg.max_bytes_total, fs,
      cfg.observation_store_directory, "V1 FileObservationStore");
}

std::unique_ptr<util::EncryptedMessageMaker> GetEncryptToAnalyzer(CobaltConfig *cfg) {
  if (cfg->target_pipeline->environment() == system_data::Environment::LOCAL) {
    return util::EncryptedMessageMaker::MakeUnencrypted();
  }

  if (cfg->target_pipeline->analyzer_encryption_key()) {
    return util::EncryptedMessageMaker::MakeForObservations(
               *cfg->target_pipeline->analyzer_encryption_key())
        .ConsumeValueOrDie();
  }

  return util::EncryptedMessageMaker::MakeUnencrypted();
}

std::unique_ptr<util::EncryptedMessageMaker> GetEncryptToShuffler(
    TargetPipelineInterface *pipeline) {
  if (pipeline->environment() == system_data::Environment::LOCAL) {
    return util::EncryptedMessageMaker::MakeUnencrypted();
  }

  if (pipeline->shuffler_encryption_key()) {
    return util::EncryptedMessageMaker::MakeForEnvelopes(*pipeline->shuffler_encryption_key())
        .ConsumeValueOrDie();
  }

  return util::EncryptedMessageMaker::MakeUnencrypted();
}

std::unique_ptr<uploader::ShippingManager> NewShippingManager(
    CobaltConfig *cfg, util::FileSystem *fs, observation_store::ObservationStore *observation_store,
    util::EncryptedMessageMaker *encrypt_to_analyzer,
    const std::unique_ptr<util::EncryptedMessageMaker> &encrypt_to_shuffler) {
  if (cfg->target_pipeline->environment() == system_data::Environment::LOCAL) {
    return std::make_unique<uploader::LocalShippingManager>(observation_store,
                                                            cfg->local_shipping_manager_path, fs);
  }
  auto shipping_manager = std::make_unique<uploader::ClearcutV1ShippingManager>(
      uploader::UploadScheduler(cfg->target_interval, cfg->min_interval, cfg->initial_interval),
      observation_store, encrypt_to_shuffler.get(), encrypt_to_analyzer,
      std::make_unique<lib::clearcut::ClearcutUploader>(cfg->target_pipeline->clearcut_endpoint(),
                                                        cfg->target_pipeline->TakeHttpClient()),
      system_data::ConfigurationData(cfg->target_pipeline->environment()).GetLogSourceId(), nullptr,
      cfg->target_pipeline->clearcut_max_retries(), cfg->api_key);

  return std::move(shipping_manager);
}

}  // namespace

CobaltService::CobaltService(CobaltConfig cfg)
    : fs_(std::move(cfg.file_system)),
      system_data_(cfg.product_name, cfg.board_name_suggestion, cfg.release_stage, cfg.version),
      global_project_context_factory_(
          cfg.global_registry
              ? std::make_unique<logger::ProjectContextFactory>(std::move(cfg.global_registry))
              : nullptr),
      observation_store_(NewObservationStore(cfg, fs_.get())),
      encrypt_to_analyzer_(GetEncryptToAnalyzer(&cfg)),
      encrypt_to_shuffler_(GetEncryptToShuffler(cfg.target_pipeline.get())),
      shipping_manager_(NewShippingManager(&cfg, fs_.get(), observation_store_.get(),
                                           encrypt_to_analyzer_.get(), encrypt_to_shuffler_)),
      logger_encoder_(cfg.client_secret, &system_data_),
      observation_writer_(observation_store_.get(), shipping_manager_.get(),
                          encrypt_to_analyzer_.get()),
      event_aggregator_manager_(cfg, fs_.get(), &logger_encoder_, &observation_writer_),
      local_aggregation_(cfg, global_project_context_factory_.get(), fs_.get(), &logger_encoder_,
                         &observation_writer_),
      undated_event_manager_(new logger::UndatedEventManager(
          &logger_encoder_, event_aggregator_manager_.GetEventAggregator(), &observation_writer_,
          &system_data_)),
      validated_clock_(cfg.validated_clock),
      internal_logger_(
          (global_project_context_factory_)
              ? NewLogger(cobalt::logger::kCustomerId, cobalt::logger::kProjectId, false)
              : nullptr) {
  if (!internal_logger_) {
    LOG(ERROR) << "The global_registry provided does not include the expected internal metrics "
                  "project. Cobalt-measuring-cobalt will be disabled.";
  }
  shipping_manager_->Start();
}

std::unique_ptr<logger::LoggerInterface> CobaltService::NewLogger(
    std::unique_ptr<logger::ProjectContext> project_context) {
  return NewLogger(std::move(project_context), true);
}

std::unique_ptr<logger::LoggerInterface> CobaltService::NewLogger(uint32_t customer_id,
                                                                  uint32_t project_id) {
  return NewLogger(customer_id, project_id, true);
}

std::unique_ptr<logger::Logger> CobaltService::NewLogger(uint32_t customer_id, uint32_t project_id,
                                                         bool include_internal_logger) {
  CHECK(global_project_context_factory_)
      << "No global_registry provided. NewLogger with customer/project id is not supported";
  return NewLogger(global_project_context_factory_->NewProjectContext(customer_id, project_id),
                   include_internal_logger);
}

std::unique_ptr<logger::Logger> CobaltService::NewLogger(
    std::unique_ptr<logger::ProjectContext> project_context, bool include_internal_logger) {
  if (project_context == nullptr) {
    return nullptr;
  }

  auto logger = include_internal_logger ? internal_logger_.get() : nullptr;
  if (undated_event_manager_) {
    return std::make_unique<logger::Logger>(std::move(project_context), &logger_encoder_,
                                            event_aggregator_manager_.GetEventAggregator(),
                                            &observation_writer_, &system_data_, validated_clock_,
                                            undated_event_manager_, logger);
  }
  return std::make_unique<logger::Logger>(std::move(project_context), &logger_encoder_,
                                          event_aggregator_manager_.GetEventAggregator(),
                                          &observation_writer_, &system_data_, logger);
}

void CobaltService::SystemClockIsAccurate(std::unique_ptr<util::SystemClockInterface> system_clock,
                                          bool start_event_aggregator_worker) {
  if (undated_event_manager_) {
    undated_event_manager_->Flush(system_clock.get(), internal_logger_.get());
    undated_event_manager_.reset();
  }

  if (start_event_aggregator_worker) {
    local_aggregation_.Start(std::make_unique<util::SystemClockRef>(system_clock.get()));
    event_aggregator_manager_.Start(std::move(system_clock));
  }
}

void CobaltService::SetDataCollectionPolicy(CobaltService::DataCollectionPolicy policy) {
  switch (policy) {
    case CobaltService::DataCollectionPolicy::COLLECT_AND_UPLOAD:
      LOG(INFO) << "CobaltService: Switch to DataCollectionPolicy COLLECT_AND_UPLOAD";
      observation_store_->Disable(false);
      event_aggregator_manager_.Disable(false);
      shipping_manager_->Disable(false);
      break;
    case CobaltService::DataCollectionPolicy::DO_NOT_UPLOAD:
      LOG(INFO) << "CobaltService: Switch to DataCollectionPolicy DO_NOT_UPLOAD";
      observation_store_->Disable(false);
      event_aggregator_manager_.Disable(false);
      shipping_manager_->Disable(true);
      break;
    case CobaltService::DataCollectionPolicy::DO_NOT_COLLECT:
      LOG(INFO) << "CobaltService: Switch to DataCollectionPolicy DO_NOT_COLLECT";
      observation_store_->Disable(true);
      event_aggregator_manager_.Disable(true);

      observation_store_->DeleteData();
      event_aggregator_manager_.DeleteData();

      shipping_manager_->Disable(true);
      break;
  }
}

}  // namespace cobalt

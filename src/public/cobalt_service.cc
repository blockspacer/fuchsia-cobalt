// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/public/cobalt_service.h"

#include <memory>

#include "src/lib/clearcut/http_client.h"
#include "src/lib/util/clock.h"
#include "src/lib/util/encrypted_message_util.h"
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

std::vector<std::unique_ptr<util::EncryptedMessageMaker>> GetExtraEncryptToShufflers(
    const std::vector<std::unique_ptr<TargetPipelineInterface>> &pipelines) {
  std::vector<std::unique_ptr<util::EncryptedMessageMaker>> retval;

  retval.reserve(pipelines.size());
  for (const auto &pipeline : pipelines) {
    retval.emplace_back(GetEncryptToShuffler(pipeline.get()));
  }

  return retval;
}

std::unique_ptr<uploader::ShippingManager> NewShippingManager(
    CobaltConfig *cfg, util::FileSystem *fs, observation_store::ObservationStore *observation_store,
    util::EncryptedMessageMaker *encrypt_to_analyzer,
    const std::unique_ptr<util::EncryptedMessageMaker> &encrypt_to_shuffler,
    const std::vector<std::unique_ptr<util::EncryptedMessageMaker>> &extra_encrypt_to_shufflers) {
  if (cfg->target_pipeline->environment() == system_data::Environment::LOCAL) {
    CHECK(cfg->extra_pipelines.empty())
        << "Only one backend environment is supported if one is LOCAL.";
    return std::make_unique<uploader::LocalShippingManager>(observation_store,
                                                            cfg->local_shipping_manager_path, fs);
  }
  auto shipping_manager = std::make_unique<uploader::ClearcutV1ShippingManager>(
      uploader::UploadScheduler(cfg->target_interval, cfg->min_interval, cfg->initial_interval),
      observation_store, encrypt_to_analyzer,
      std::make_unique<lib::clearcut::ClearcutUploader>(cfg->target_pipeline->clearcut_endpoint(),
                                                        cfg->target_pipeline->TakeHttpClient()),
      nullptr, cfg->target_pipeline->clearcut_max_retries(), cfg->api_key);

  shipping_manager->AddClearcutDestination(
      encrypt_to_shuffler.get(),
      system_data::ConfigurationData(cfg->target_pipeline->environment()).GetLogSourceId());
  for (int i = 0; i < cfg->extra_pipelines.size(); i++) {
    shipping_manager->AddClearcutDestination(
        extra_encrypt_to_shufflers[i].get(),
        system_data::ConfigurationData(cfg->extra_pipelines[i]->environment()).GetLogSourceId());
  }

  return std::move(shipping_manager);
}

}  // namespace

CobaltService::CobaltService(CobaltConfig cfg)
    : fs_(std::move(cfg.file_system)),
      system_data_(cfg.product_name, cfg.board_name_suggestion, cfg.release_stage, cfg.version),
      observation_store_(NewObservationStore(cfg, fs_.get())),
      encrypt_to_analyzer_(GetEncryptToAnalyzer(&cfg)),
      encrypt_to_shuffler_(GetEncryptToShuffler(cfg.target_pipeline.get())),
      extra_encrypt_to_shufflers_(GetExtraEncryptToShufflers(cfg.extra_pipelines)),
      shipping_manager_(NewShippingManager(&cfg, fs_.get(), observation_store_.get(),
                                           encrypt_to_analyzer_.get(), encrypt_to_shuffler_,
                                           extra_encrypt_to_shufflers_)),
      logger_encoder_(cfg.client_secret, &system_data_),
      observation_writer_(observation_store_.get(), shipping_manager_.get(),
                          encrypt_to_analyzer_.get()),
      event_aggregator_manager_(cfg, fs_.get(), &logger_encoder_, &observation_writer_),
      undated_event_manager_(new logger::UndatedEventManager(
          &logger_encoder_, event_aggregator_manager_.GetEventAggregator(), &observation_writer_,
          &system_data_)),
      validated_clock_(cfg.validated_clock),
      internal_logger_(NewLogger(std::move(cfg.internal_logger_project_context), false)) {
  shipping_manager_->Start();
}

std::unique_ptr<logger::Logger> CobaltService::NewLogger(
    std::unique_ptr<logger::ProjectContext> project_context) {
  return NewLogger(std::move(project_context), true);
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
                                            &observation_writer_, &system_data_,
                                            validated_clock_.get(), undated_event_manager_, logger);
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
    event_aggregator_manager_.Start(std::move(system_clock));
  }
}

}  // namespace cobalt

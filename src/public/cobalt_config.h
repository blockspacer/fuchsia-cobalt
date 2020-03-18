// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_PUBLIC_COBALT_CONFIG_H_
#define COBALT_SRC_PUBLIC_COBALT_CONFIG_H_

#include <memory>

#include "src/lib/clearcut/http_client.h"
#include "src/lib/util/clock.h"
#include "src/lib/util/file_system.h"
#include "src/logger/project_context.h"
#include "src/registry/metric_definition.pb.h"
#include "src/system_data/client_secret.h"
#include "src/system_data/configuration_data.h"

namespace cobalt {

constexpr char kDefaultClearcutEndpoint[] = "https://play.googleapis.com/staging/log";
constexpr char kProductionClearcutEndpoint[] = "https://play.googleapis.com/log";
constexpr size_t kDefaultClearcutMaxRetries = 5;

class TargetPipelineInterface {
 public:
  explicit TargetPipelineInterface(system_data::Environment environment)
      : environment_(environment){};
  virtual ~TargetPipelineInterface() = default;

  // The target environment for the pipeline. Used to determine where to send the data.
  [[nodiscard]] system_data::Environment environment() const { return environment_; }

  // An encoded CobaltEncryptionKey proto representing the encryption key to be used for envelopes
  // sent to the shuffler. (Or nullopt for no encryption)
  [[nodiscard]] virtual std::optional<std::string> shuffler_encryption_key() const {
    return std::nullopt;
  }

  // An encoded CobaltEncryptionKey proto representing the encryption key to be used for
  // observations sent to the analyzer. (Or nullopt for no encryption)
  [[nodiscard]] virtual std::optional<std::string> analyzer_encryption_key() const {
    return std::nullopt;
  }

  // The URL for the desired clearcut endpoint.
  [[nodiscard]] virtual std::string clearcut_endpoint() const { return ""; };

  // Returns an implementation of lib::clearcut::HTTPClient. Will be used for uploading data in the
  // ShippingManager.
  [[nodiscard]] virtual std::unique_ptr<lib::clearcut::HTTPClient> TakeHttpClient() {
    return nullptr;
  }

  // How many times should the clearcut upload be reattempted, before returning the observations to
  // the ObservationStore.
  [[nodiscard]] virtual size_t clearcut_max_retries() const { return 0; }

 private:
  system_data::Environment environment_;
};

class LocalPipeline : public TargetPipelineInterface {
 public:
  LocalPipeline() : TargetPipelineInterface(system_data::Environment::LOCAL) {}
  ~LocalPipeline() override = default;
};

class TargetPipeline : public TargetPipelineInterface {
 public:
  TargetPipeline(system_data::Environment environment, std::string shuffler_encryption_key,
                 std::string analyzer_encryption_key,
                 std::unique_ptr<lib::clearcut::HTTPClient> http_client,
                 size_t clearcut_max_retries = kDefaultClearcutMaxRetries)
      : TargetPipelineInterface(environment),
        shuffler_encryption_key_(std::move(shuffler_encryption_key)),
        analyzer_encryption_key_(std::move(analyzer_encryption_key)),
        http_client_(std::move(http_client)),
        clearcut_max_retries_(clearcut_max_retries) {}
  ~TargetPipeline() override = default;

  [[nodiscard]] std::optional<std::string> shuffler_encryption_key() const override {
    return shuffler_encryption_key_;
  }
  [[nodiscard]] std::optional<std::string> analyzer_encryption_key() const override {
    return analyzer_encryption_key_;
  }

  [[nodiscard]] std::string clearcut_endpoint() const override {
    if (environment() == system_data::Environment::PROD) {
      return kProductionClearcutEndpoint;
    }
    return kDefaultClearcutEndpoint;
  };
  [[nodiscard]] std::unique_ptr<lib::clearcut::HTTPClient> TakeHttpClient() override {
    return std::move(http_client_);
  }
  [[nodiscard]] size_t clearcut_max_retries() const override { return clearcut_max_retries_; }

 private:
  std::string shuffler_encryption_key_;
  std::string analyzer_encryption_key_;
  std::unique_ptr<lib::clearcut::HTTPClient> http_client_;
  size_t clearcut_max_retries_;
};

struct CobaltConfig {
  // |product_name|: The value to use for the |product_name| field of the SystemProfile.
  std::string product_name = "";

  // |board_name_suggestion|: A suggestion for the value to use for the |board_name| field of the
  // SystemProfile. This may be ignored if SystemData is able to determine the board name directly.
  // A value of "" indicates that the caller has no information about board name, so one should be
  // guessed.
  std::string board_name_suggestion = "";

  // |version|: The version of the running system. The use of this field is system-specific. For
  // example on Fuchsia a possible value for |version| is "20190220_01_RC00".
  std::string version = "";

  // |release_stage|: The ReleaseStage of the running system.
  ReleaseStage release_stage = ReleaseStage::GA;

  // |file_system|: The FileSystem implementation to be used for all file system operations in
  // Cobalt.
  std::unique_ptr<util::FileSystem> file_system;

  // |use_memory_observation_store|: If true, the ObservaitonStore used will be memory backed intead
  // of file backed.
  bool use_memory_observation_store = false;

  // These three values are provided to the ObservationStore.
  //
  //|max_bytes_per_event|: Attempting to log an event that is larger than this value will result in
  // an error code kObservationTooBig. (See observation_store.h)
  //
  // |max_bytes_per_envelope|: When pooling together Observaitons into an Envelope, the
  // ObservationStore will try not to form envelopes larger than this size. This value is used to
  // avoid sending messages over HTTP that are too large. (see observation_store.h)
  //
  // |max_bytes_total|: This is the maximum size of the Observations in the ObservationStore. If the
  // size of the accumulated Observation data reaches this value, then ObservationStore will not
  // accept any more Observations, resulting in an error code of kStoreFull. (See
  // observaiton_store.h)
  //
  // REQUIRED:
  // 0 <= max_bytes_per_event <= max_bytes_per_envelope <= max_bytes_total
  // 0 <= max_bytes_per_envelope
  size_t max_bytes_per_event;
  size_t max_bytes_per_envelope;
  size_t max_bytes_total;

  // |observation_store_directory|: If |use_memory_observation_store| is false, this is the absolute
  // path to the directory the observation_store will use to store observations.
  std::string observation_store_directory;

  // |local_aggregate_proto_store_path|: The absolute path where the local aggregate proto should be
  // stored.
  std::string local_aggregate_proto_store_path;

  // |obs_history_proto_store_path|: The absolute path where the observation history proto should be
  // stored.
  std::string obs_history_proto_store_path;

  // These three values are provided to the UploadScheduler of the shipping manager.
  //
  // |target_interval|: How frequently should ShippingManager perform regular periodic sends to the
  // Shuffler? Set to kMaxSeconds to effectively disable periodic sends.
  //
  // |min_interval|: Used as the basis for exponentially increasing the upload interval. The
  // resulting interval starts at this value, and then is multiplied by 2 each time until the value
  // is greater than or equal to |target_interval|.
  //
  // REQUIRED:
  // 0 <= min_interval <= target_interval <= kMaxSeconds
  std::chrono::seconds target_interval;
  std::chrono::seconds min_interval;
  std::chrono::seconds initial_interval;

  // |target_pipeline|: Used to determine where to send observations, and how to encrypt them.
  std::unique_ptr<TargetPipelineInterface> target_pipeline;

  // |local_shipping_manager_path|: If |environments| is equal to {LOCAL}, the observations will be
  // written to this path, instead of being shipped to clearcut.
  std::string local_shipping_manager_path;

  // |api_key|: An API key included in each request to the Shuffler. If the API key is unrecognized
  // on the server, the observations may be discarded.
  std::string api_key;

  // |client_secret|: The ClientSecret for this device.
  system_data::ClientSecret client_secret;

  // DEPRECATED: |internal_logger_project_context|: A ProjectContext that can be used to construct
  // the internal Logger.
  //
  // TODO(zmbush): Remove once this is no longer used.
  std::unique_ptr<logger::ProjectContext> internal_logger_project_context;

  // |global_registry|: The complete registry of all customers, projects, metrics, and reports.
  std::unique_ptr<CobaltRegistry> global_registry = nullptr;

  // |local_aggregation_backfill_days|: The number of past days for which the AggregateStore
  // generates and sends Observations, in addition to a requested day index.
  size_t local_aggregation_backfill_days;

  // |validated_clock|: A reference to a ValidatedClockInterface, used to determine when the system
  // has a clock that we can rely on.
  util::ValidatedClockInterface* validated_clock;
};

}  // namespace cobalt

#endif  // COBALT_SRC_PUBLIC_COBALT_CONFIG_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_REGISTRY_CLIENT_CONFIG_H_
#define COBALT_SRC_REGISTRY_CLIENT_CONFIG_H_

#include <memory>
#include <string>
#include <utility>

#include "src/registry/cobalt_registry.pb.h"
#include "src/registry/encoding_config.h"
#include "src/registry/encodings.pb.h"
#include "src/registry/metric_config.h"
#include "src/registry/metrics.pb.h"

namespace cobalt {
namespace config {

// ClientConfig provides a convenient interface over a |CobaltRegistry|
// that is intended to be used on Cobalt's client-side.
//
// A CobaltRegistry can be in one of three states:
//
// (1) It can contain data for a single Cobalt 0.1 project.
// (2) It can contain data for a single Cobalt 1.0 project.
// (3) It can contain data for multiple Cobalt projects. In this case it
//     may contain data for some Cobalt 0.1 projects and some Cobalt 1.0
//     projects.
//
// This class is part of Cobalt 0.1. and as such it ignores the Cobalt 1.0
// data in a |CobaltRegistry| and gives access only to the Cobalt 0.1 data.
// So an instance of this class can be in one of three states corresponding
// to the three states above:
//
// (1) It can contain data for a single Cobalt 0.1 project.
// (2) It can be empty
// (3) It can contain data for multiple Cobalt 0.1 projects.
//
// See the class |ProjectConfigs| for the Cobalt 1.0 analogue of this class.
class ClientConfig {
 public:
  // Constructs and returns an instance of ClientConfig by first parsing
  // a CobaltRegistry proto message from |cobalt_registry_base64|, which should
  // contain the Base64 encoding of the bytes of the binary serialization of
  // such a message, and then extracting the Metrics and EncodingConfigs from
  // that.
  //
  // Returns nullptr to indicate failure.
  static std::unique_ptr<ClientConfig> CreateFromCobaltRegistryBase64(
      const std::string& cobalt_registry_base64);

  // Constructs and returns an instance of ClientConfig by first parsing
  // a CobaltRegistry proto message from |cobalt_registry_bytes|, which should
  // contain the bytes of the binary serialization of such a message, and then
  // extracting the Metrics and EncodingConfigs from that.
  //
  // Returns nullptr to indicate failure.
  static std::unique_ptr<ClientConfig> CreateFromCobaltRegistryBytes(
      const std::string& cobalt_registry_bytes);

  // Constructs and returns and instance of ClientConfig from
  // |cobalt_registry|.
  static std::unique_ptr<ClientConfig> CreateFromCobaltRegistryProto(
      std::unique_ptr<CobaltRegistry> cobalt_registry);

  // Constructs a ClientConfig that wraps the given registries.
  ClientConfig(std::shared_ptr<config::EncodingRegistry> encoding_configs,
               std::shared_ptr<config::MetricRegistry> metrics);

  // Returns the EncodingConfig with the given ID triple, or nullptr if there is
  // no such EncodingConfig. The caller does not take ownership of the returned
  // pointer.
  const EncodingConfig* EncodingConfig(uint32_t customer_id, uint32_t project_id,
                                       uint32_t encoding_config_id);

  // Returns the Metric with the given ID triple, or nullptr if there is
  // no such Metric. The caller does not take ownership of the returned
  // pointer.
  const class Metric* Metric(uint32_t customer_id, uint32_t project_id, uint32_t metric_id);

  // Returns the Metric with the given triple, or nullptr if there is no
  // such Metric. The caller does not take ownership of the returned pointer.
  const class Metric* Metric(uint32_t customer_id, uint32_t project_id,
                             const std::string& metric_name);

  // Returns whether or not this instance of ClientConfig contains data for
  // exactly one project.
  bool is_single_project() { return is_single_project_; }

  // Returns whether or not this instance of ClientConfig contains no
  // project data.
  bool is_empty() { return is_empty_; }

  // If is_single_project() is true then this returns the customer ID of
  // the single project. Otherwise the return value is undefined.
  uint32_t single_customer_id() { return single_customer_id_; }

  // If is_single_project() is true then this returns the project ID of
  // the single project. Otherwise the return value is undefined.
  uint32_t single_project_id() { return single_project_id_; }

 private:
  void DetermineIfSingleProject();

  std::shared_ptr<config::EncodingRegistry> encoding_configs_;
  std::shared_ptr<config::MetricRegistry> metrics_;

  bool is_single_project_;
  bool is_empty_;
  uint32_t single_customer_id_ = 0;
  uint32_t single_project_id_ = 0;
};

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_SRC_REGISTRY_CLIENT_CONFIG_H_

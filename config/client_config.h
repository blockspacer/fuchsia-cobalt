// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CONFIG_CLIENT_CONFIG_H_
#define COBALT_CONFIG_CLIENT_CONFIG_H_

#include <memory>
#include <string>
#include <utility>

#include "config/cobalt_registry.pb.h"
#include "config/encoding_config.h"
#include "config/encodings.pb.h"
#include "config/metric_config.h"
#include "config/metrics.pb.h"

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

  // DEPRECATED: This method is being removed. Do not use.
  //
  // This method first invokes CreateFromCobaltRegistryBytes() and then
  // checks the resulting ClientConfig to see if it represents a single
  // project. If so it returns a pair consisting of the ClientConfig
  // and the single project ID.
  //
  // Returns nullptr in the first component to indicate failure.
  static std::pair<std::unique_ptr<ClientConfig>, uint32_t>
  CreateFromCobaltProjectRegistryBytes(
      const std::string& cobalt_registry_bytes);

  // DEPRECATED: Use CreateFromCobaltRegistryProto() instead.
  //
  // Constructs and returns an instance of ClientConfig by swapping all of
  // the Metrics and EncodingConfigs from the given |cobalt_registry|.
  //
  // Returns nullptr to indicate failure.
  static std::unique_ptr<ClientConfig> CreateFromCobaltRegistry(
      CobaltRegistry* cobalt_registry);

  // Returns the EncodingConfig with the given ID triple, or nullptr if there is
  // no such EncodingConfig. The caller does not take ownership of the returned
  // pointer.
  const EncodingConfig* EncodingConfig(uint32_t customer_id,
                                       uint32_t project_id,
                                       uint32_t encoding_config_id);

  // Returns the Metric with the given ID triple, or nullptr if there is
  // no such Metric. The caller does not take ownership of the returned
  // pointer.
  const class Metric* Metric(uint32_t customer_id, uint32_t project_id,
                             uint32_t metric_id);

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

  // DEPRECATED: This method is being removed. Do not use.
  bool IsLegacy() { return !single_customer_config_; }

  // DEPRECATED: This method is being removed. Do not use.
  // If is_single_project() is true and IsLegacy() is false then this
  // method returns the single CustomerConfig form the CobaltRegistry.
  std::unique_ptr<CustomerConfig> TakeCustomerConfig() {
    return std::move(single_customer_config_);
  }

 private:
  // Constructs an ClientConfig that wraps the given registries. (v0.1)
  ClientConfig(std::unique_ptr<config::EncodingRegistry> encoding_configs,
               std::unique_ptr<config::MetricRegistry> metrics);

  void DetermineIfSingleProject();

  std::unique_ptr<config::EncodingRegistry> encoding_configs_;
  std::unique_ptr<config::MetricRegistry> metrics_;

  // DEPRECATED: This field is being removed. Do not use.
  // If there is only a single project and it is of type Cobalt 1.0
  // then this holds the single CustomerConfig.
  std::unique_ptr<CustomerConfig> single_customer_config_;

  bool is_single_project_;
  bool is_empty_;
  uint32_t single_customer_id_ = 0;
  uint32_t single_project_id_ = 0;
};

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_CLIENT_CONFIG_H_

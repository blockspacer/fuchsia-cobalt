// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_CHANNEL_MAPPER_H_
#define COBALT_LOGGER_CHANNEL_MAPPER_H_

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/registry/metric_definition.pb.h"
#include "third_party/statusor/statusor.h"
#include "util/protected_fields.h"

namespace cobalt {
namespace logger {

// ChannelMapper is used to for mapping a channel string to a ReleaseStage enum
// value.
class ChannelMapper {
 public:
  // Constructs an instance of ChannelMapper using the provided data.
  //
  // |channel_map| A mapping from ReleaseStage to an array of channel names
  // that should map to that ReleaseStage. All unmatched channels will map to
  // ReleaseStage::GA. If a channel is listed for multiple ReleaseStages, only
  // the first encountered ReleaseStage will be used.
  explicit ChannelMapper(const std::map<ReleaseStage, std::vector<std::string>> &channel_map);

  // Constructs an instance of ChannelMapper in which any of the given channels
  // map to ReleaseStage::DEBUG and all other channels map to ReleaseStage::GA.
  //
  // |debug_channels| A list of channel names that should be mapped to
  // ReleaseStage::DEBUG.
  explicit ChannelMapper(std::vector<std::string> debug_channels);

  ReleaseStage ToReleaseStage(const std::string &channel);

 private:
  const std::unordered_map<std::string, ReleaseStage> release_stage_map_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_CHANNEL_MAPPER_H_

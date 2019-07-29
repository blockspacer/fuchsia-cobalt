// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/channel_mapper.h"

#include <utility>

#include "./logging.h"

namespace cobalt {
namespace logger {

namespace {

std::unordered_map<std::string, ReleaseStage> ConstructReleaseStageMap(
    const std::map<ReleaseStage, std::vector<std::string>> &channel_map) {
  std::unordered_map<std::string, ReleaseStage> retval;

  for (const auto &entry : channel_map) {
    for (const std::string &channel : entry.second) {
      if (retval.find(channel) != retval.end()) {
        LOG(ERROR) << "Found duplicate release stage mapping. " << channel
                   << " maps to two different ReleaseStages";
        continue;
      }

      retval[channel] = entry.first;
    }
  }

  return retval;
}

}  // namespace

ChannelMapper::ChannelMapper(const std::map<ReleaseStage, std::vector<std::string>> &channel_map)
    : release_stage_map_(ConstructReleaseStageMap(channel_map)) {}

ChannelMapper::ChannelMapper(std::vector<std::string> debug_channels)
    : ChannelMapper({{ReleaseStage::DEBUG, std::move(debug_channels)}}) {}

ReleaseStage ChannelMapper::ToReleaseStage(const std::string &channel) {
  const auto it = release_stage_map_.find(channel);
  if (it != release_stage_map_.end()) {
    return it->second;
  }
  return ReleaseStage::GA;
}

}  // namespace logger
}  // namespace cobalt

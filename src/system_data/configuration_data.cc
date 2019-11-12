// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/system_data/configuration_data.h"

namespace cobalt::system_data {

// IDs of the Clearcut log sources (Cobalt Shuffler Input) that Cobalt can write to.
//
// Can be used to write logs for Clearcut's demo application.
// static const int32_t kClearcutDemoSource = 177;
static const int32_t kLogSourceIdDevel = 844;
static const int32_t kLogSourceIdProd = 1176;

const char* EnvironmentString(const Environment& environment) {
  switch (environment) {
    case PROD:
      return "PROD";
    case DEVEL:
      return "DEVEL";
    case LOCAL:
      return "LOCAL";
  }
}

std::ostream& operator<<(std::ostream& os, Environment environment) {
  return os << EnvironmentString(environment);
}

int32_t ConfigurationData::GetLogSourceId() const {
  switch (environment_) {
    case PROD:
      return kLogSourceIdProd;
    case DEVEL:
      return kLogSourceIdDevel;
    case LOCAL:
      return 0;
  }
}

}  // namespace cobalt::system_data

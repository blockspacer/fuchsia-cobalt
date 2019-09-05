// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/registry/id.h"

#include <string>

namespace cobalt::config {

constexpr uint32_t kFnvPrime = 0x1000193;
constexpr uint32_t kFnvOffsetBasis = 0x811c9dc5;

uint32_t IdFromName(const std::string &name) {
  uint32_t hash = kFnvOffsetBasis;
  for (char ch : name) {
    hash *= kFnvPrime;
    hash ^= ch;
  }
  return hash;
}

}  // namespace cobalt::config

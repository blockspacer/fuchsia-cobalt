// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file contains the code that computes config ids from config names.
//
// We use the Fowler-Noll-Vo hash function
#ifndef COBALT_SRC_REGISTRY_ID_H_
#define COBALT_SRC_REGISTRY_ID_H_

#include <string>

namespace cobalt {
namespace config {

uint32_t IdFromName(const std::string &name);

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_SRC_REGISTRY_ID_H_

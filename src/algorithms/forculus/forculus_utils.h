// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef COBALT_SRC_ALGORITHMS_FORCULUS_FORCULUS_UTILS_H_
#define COBALT_SRC_ALGORITHMS_FORCULUS_FORCULUS_UTILS_H_

#include "src/lib/util/datetime_util.h"
#include "src/registry/encodings.pb.h"

// Common utilities used in the encoder and analyzer.

namespace cobalt {
namespace forculus {

using util::kInvalidIndex;

// Compute the Forculus epoch index for the given |day_index| based on
// the given |epoch_type|.
uint32_t EpochIndexFromDayIndex(uint32_t day_index, const EpochType& epoch_type);

}  // namespace forculus
}  // namespace cobalt

#endif  // COBALT_SRC_ALGORITHMS_FORCULUS_FORCULUS_UTILS_H_
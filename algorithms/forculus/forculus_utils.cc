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

#include "algorithms/forculus/forculus_utils.h"

namespace cobalt {
namespace forculus {

// Compute the Forculus epoch index for the given |day_index| based on
// the given |epoch_type|.
uint32_t EpochIndexFromDayIndex(uint32_t day_index, const EpochType& epoch_type) {
  switch (epoch_type) {
    case DAY:
      return day_index;
    case WEEK:
      return util::DayIndexToWeekIndex(day_index);
    case MONTH:
      return util::DayIndexToMonthIndex(day_index);
    default:
      return kInvalidIndex;
  }
}

}  // namespace forculus
}  // namespace cobalt

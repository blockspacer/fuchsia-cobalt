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

#ifndef COBALT_CONFIG_ENCODING_CONFIG_H_
#define COBALT_CONFIG_ENCODING_CONFIG_H_

#include "config/config.h"
#include "config/encodings.pb.h"

namespace cobalt {
namespace config {

// A container for all of the EncodingConfigs registered in Cobalt. This
// is used by both an Encoder client and the Analyzer.
using EncodingRegistry = Registry<RegisteredEncodings>;

// For ease of understanding we specify the interface below as if
// EncodingRegistry were not a template specialization but a stand-alone class.

/*
class EncodingRegistry {
 public:
  // Returns the number of EncodingConfigs in this registry.
  size_t size();

  // Returns the EncodingConfig with the given ID triple, or nullptr if there is
  // no such EncodingConfig. The caller does not take ownership of the returned
  // pointer.
  const EncodingConfig* const Get(uint32_t customer_id,
                                  uint32_t project_id,
                                  uint32_t id);
};
*/

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_ENCODING_CONFIG_H_

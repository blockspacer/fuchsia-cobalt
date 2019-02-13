# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# validator library contains the common configuration validation code to be used
# across cobalt.
add_library(validator valid_cobalt_config.cc)
add_cobalt_dependencies(validator)
target_link_libraries(validator statusor)

add_executable(validate validate_main.cc)
target_link_libraries(validate validator)

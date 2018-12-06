# Copyright 2017 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_custom_command(OUTPUT ${CONFIG_CHANGE_VALIDATOR_BINARY}
  # Compiles config_change_validator_main and all its dependencies.
  COMMAND ${GO_BIN} build -o ${CONFIG_CHANGE_VALIDATOR_BINARY} config_change_validator_main.go
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/config_change_validator_main.go
  DEPENDS ${CONFIG_PB_GO_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)


# Force the build to occur.
add_custom_target(build_config_change_validator ALL
    DEPENDS ${YAMLPB_TEST_BIN}
    DEPENDS ${CONFIG_CHANGE_VALIDATOR_TEST_BIN}
    DEPENDS ${CONFIG_CHANGE_VALIDATOR_BINARY}
    DEPENDS ${CONFIG_VALIDATOR_TEST_BIN}
    DEPENDS ${SOURCE_GENERATOR_TEST_BIN}
    DEPENDS ${CONFIG_CHANGE_VALIDATOR_STABILITY_TEST_BIN}
)

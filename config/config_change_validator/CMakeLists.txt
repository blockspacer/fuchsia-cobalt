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

set(VALIDATOR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/validator/registries.go
                  ${CMAKE_CURRENT_SOURCE_DIR}/src/validator/customers.go
                  ${CMAKE_CURRENT_SOURCE_DIR}/src/validator/escape_hatch.go
                  ${CMAKE_CURRENT_SOURCE_DIR}/src/validator/metrics.go
                  ${CMAKE_CURRENT_SOURCE_DIR}/src/validator/projects.go)

add_custom_command(OUTPUT ${CONFIG_CHANGE_VALIDATOR_BINARY}
  # Compiles config_change_validator_main and all its dependencies.
  COMMAND ${GO_BIN} build -o ${CONFIG_CHANGE_VALIDATOR_BINARY} config_change_validator_main.go
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/config_change_validator_main.go
  DEPENDS ${CONFIG_PB_GO_FILES}
  DEPENDS ${VALIDATOR_SRC}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(VALIDATOR_TEST_BIN ${GO_TESTS}/config_change_validator_test)
set(VALIDATOR_TEST_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/validator/escape_hatch_test.go
  ${CMAKE_CURRENT_SOURCE_DIR}/src/validator/metrics_test.go
  ${CMAKE_CURRENT_SOURCE_DIR}/src/validator/projects_test.go)

add_custom_command(OUTPUT ${VALIDATOR_TEST_BIN}
  COMMAND ${GO_BIN} test -c -o ${VALIDATOR_TEST_BIN} ${VALIDATOR_TEST_SRC} ${VALIDATOR_SRC}
  DEPENDS ${VALIDATOR_SRC}
  DEPENDS ${VALIDATOR_TEST_SRC}
  DEPENDS ${CONFIG_PB_GO_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Force the build to occur.
add_custom_target(build_config_change_validator ALL
    DEPENDS ${YAMLPB_TEST_BIN}
    DEPENDS ${CONFIG_CHANGE_VALIDATOR_TEST_BIN}
    DEPENDS ${CONFIG_CHANGE_VALIDATOR_BINARY}
    DEPENDS ${VALIDATOR_TEST_BIN}
)

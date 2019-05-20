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

cobalt_protobuf_generate_go(generate_yamlpb_test_pb_go_files
                            YAMLPB_TEST_PB_GO
                            false
                            src/yamlpb/yamlpb_test)

set(YAMLPB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/yamlpb/yamlpb.go)
set(YAMLPB_TEST_BIN ${GO_TESTS}/yamlpb_test)
set(YAMLPB_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/yamlpb/yamlpb_test.go")
add_custom_command(OUTPUT ${YAMLPB_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${YAMLPB_TEST_BIN} ${YAMLPB_TEST_SRC} ${YAMLPB_SRC}
    DEPENDS ${YAMLPB_SRC}
    DEPENDS ${YAMLPB_TEST_SRC}
    DEPENDS ${YAMLPB_TEST_PB_GO}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(CONFIG_PARSER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser/project_list.go
                      ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser/project_config.go
                      ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser/git.go
                      ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser/config_reader.go
                      ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser/id.go
                      ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser/filter.go
                      ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser/config_parser.go)

set(CONFIG_VALIDATOR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/validator.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/system_profile_field.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/encodings.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/metrics.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/metric_definitions.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/report_definitions.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/common_validator.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/reports.go)

set(SOURCE_GENERATOR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/source_outputter.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/ident.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/simple.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/cpp.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/dart.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/rust.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/writer.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/dep.go
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/utils.go)

add_custom_command(OUTPUT ${CONFIG_PARSER_BINARY}
  # Compiles config_parser_main and all its dependencies.
  COMMAND ${GO_BIN} build -o ${CONFIG_PARSER_BINARY} config_parser_main.go
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser_main.go
  DEPENDS ${CONFIG_PARSER_SRC}
  DEPENDS ${CONFIG_VALIDATOR_SRC}
  DEPENDS ${SOURCE_GENERATOR_SRC}
  DEPENDS ${CONFIG_PB_GO_FILES}
  DEPENDS ${YAMLPB_SRC}
  DEPENDS ../validation/validator
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)


# Build tests
set(CONFIG_PARSER_TEST_BIN ${GO_TESTS}/config_parser_test)
set(CONFIG_PARSER_TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser/project_list_test.go
                            ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser/config_reader_test.go
                            ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser/id_test.go
                            ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser/project_config_test.go
                            ${CMAKE_CURRENT_SOURCE_DIR}/src/config_parser/filter_test.go)

add_custom_command(OUTPUT ${CONFIG_PARSER_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${CONFIG_PARSER_TEST_BIN} ${CONFIG_PARSER_TEST_SRC} ${CONFIG_PARSER_SRC}
    DEPENDS ${CONFIG_PARSER_SRC}
    DEPENDS ${CONFIG_PARSER_TEST_SRC}
    DEPENDS ${CONFIG_PB_GO_FILES}
    DEPENDS ${YAMLPB_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(CONFIG_VALIDATOR_TEST_BIN ${GO_TESTS}/config_validator_tests)
set(CONFIG_VALIDATOR_TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/system_profile_field_test.go
                              ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/encodings_test.go
                              ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/reports_test.go
                              ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/metrics_test.go
                              ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/report_definitions_test.go
                              ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/metric_definitions_test.go
                              ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/common_validator_test.go
                              ${CMAKE_CURRENT_SOURCE_DIR}/src/config_validator/testutil.go)
add_custom_command(OUTPUT ${CONFIG_VALIDATOR_TEST_BIN}
  COMMAND ${GO_BIN} test -c -o ${CONFIG_VALIDATOR_TEST_BIN} ${CONFIG_VALIDATOR_TEST_SRC} ${CONFIG_VALIDATOR_SRC}
  DEPENDS ${CONFIG_VALIDATOR_SRC}
  DEPENDS ${CONFIG_VALIDATOR_TEST_SRC}
  DEPENDS ${CONFIG_PB_GO_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(SOURCE_GENERATOR_TEST_BIN ${GO_TESTS}/source_generator_test)
set(SOURCE_GENERATOR_TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/source_generator_test.go
                              ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/ident_test.go
                              ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/source_outputter_test.go
                              ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/utils_test.go
                              ${CMAKE_CURRENT_SOURCE_DIR}/src/source_generator/dep_test.go)
add_custom_command(OUTPUT ${SOURCE_GENERATOR_TEST_BIN}
  COMMAND ${GO_BIN} test -c -o ${SOURCE_GENERATOR_TEST_BIN} ${SOURCE_GENERATOR_TEST_SRC} ${SOURCE_GENERATOR_SRC}
  DEPENDS ${SOURCE_GENERATOR_SRC}
  DEPENDS ${SOURCE_GENERATOR_TEST_SRC}
  DEPENDS ${CONFIG_PB_GO_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(CONFIG_PARSER_STABILITY_TEST_BIN ${DIR_CONFIG_PARSER_TESTS}/test_stable_config_parser.py)
set(CONFIG_PARSER_STABILITY_TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/test_stable_config_parser.py)
add_custom_command(OUTPUT ${CONFIG_PARSER_STABILITY_TEST_BIN}
  COMMAND cmake -E create_symlink ${CONFIG_PARSER_STABILITY_TEST_SRC} ${CONFIG_PARSER_STABILITY_TEST_BIN}
  DEPENDS ${CONFIG_PARSER_STABILITY_TEST_SRC}
)

# Force the build to occur.
add_custom_target(build_config_parser ALL
    DEPENDS ${YAMLPB_TEST_BIN}
    DEPENDS ${CONFIG_PARSER_TEST_BIN}
    DEPENDS ${CONFIG_PARSER_BINARY}
    DEPENDS ${CONFIG_VALIDATOR_TEST_BIN}
    DEPENDS ${SOURCE_GENERATOR_TEST_BIN}
    DEPENDS ${CONFIG_PARSER_STABILITY_TEST_BIN}
)

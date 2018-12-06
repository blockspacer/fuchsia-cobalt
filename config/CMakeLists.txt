# Copyright 2016 The Fuchsia Authors
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

set(CONFIG_PARSER_BINARY "${CMAKE_CURRENT_BINARY_DIR}/config_parser/config_parser")
set(CONFIG_VALIDATOR_BINARY "${CMAKE_CURRENT_BINARY_DIR}/validation/validate")
set(CONFIG_CHANGE_VALIDATOR_BINARY
  "${CMAKE_CURRENT_BINARY_DIR}/config_change_validator/config_change_validator")

add_subdirectory(config_parser)
add_subdirectory(config_change_validator)
add_subdirectory(validation)

# Generate the C++ bindings for the config .proto files.
# Also compile the generated C++ files into a static library.
cobalt_make_protobuf_cpp_lib(config_proto_lib
                             CONFIG_PROTO_HDRS
                             false
                             encodings metrics metric_definition
                             report_configs report_definition
                             cobalt_config window_size annotations project)

# Generate Go bindings for the config .proto files.
cobalt_protobuf_generate_go(generate_config_pb_go_files
                            CONFIG_PB_GO_FILES
                            false
                            cobalt_config encodings metrics metric_definition
                            report_configs report_definition window_size
                            annotations project)

add_library(buckets_config
            buckets_config.cc
            ${CONFIG_PROTO_HDRS})
target_link_libraries(buckets_config
    config_proto_lib)
add_cobalt_dependencies(buckets_config)

# Build the analyzer_config library
add_library(analyzer_config
            analyzer_config.cc
            analyzer_config_manager.cc
            ${CONFIG_PROTO_HDRS})
target_link_libraries(analyzer_config
    config_proto_lib)
add_cobalt_dependencies(analyzer_config)

# Build the client_config library
add_library(client_config
            client_config.cc
            ${CONFIG_PROTO_HDRS})
target_link_libraries(client_config
    config_proto_lib cobalt_crypto)
add_cobalt_dependencies(client_config)

# Build the project_configs library
add_library(project_configs
            project_configs.cc
            ${CONFIG_PROTO_HDRS})
target_link_libraries(project_configs
    config_proto_lib cobalt_crypto)
add_cobalt_dependencies(project_configs)

add_library(config_ids
            id.cc)
target_link_libraries(config_ids)

# Build the config_gen tool
add_executable(config_gen config_gen.cc ${CONFIG_PROTO_HDRS})
add_cobalt_dependencies(config_gen)

# Build the tests
add_executable(config_tests
               analyzer_config_test.cc
               client_config_test.cc
               project_configs_test.cc
               config_test.cc
               buckets_config_test.cc
               id_test.cc
               ${CONFIG_PROTO_HDRS})
target_link_libraries(config_tests
                      analyzer_config
                      buckets_config
                      client_config
                      project_configs
                      config_ids)
add_cobalt_test_dependencies(config_tests ${DIR_GTESTS})

set(CONFIG_DIR "${CMAKE_SOURCE_DIR}/third_party/config")
set(CONFIG_PROTO "${CMAKE_BINARY_DIR}/third_party/config/cobalt_config.binproto")
add_custom_command(OUTPUT ${CONFIG_PROTO}
    COMMAND ${CONFIG_PARSER_BINARY}
    ARGS --config_dir ${CONFIG_DIR}
    ARGS --output_file ${CONFIG_PROTO}
    ARGS --config_validator_bin ${CONFIG_VALIDATOR_BINARY}
    DEPENDS ${CONFIG_PARSER_BINARY}
    DEPENDS ${CONFIG_VALIDATOR_BINARY}
)

add_custom_target(build_config_proto ALL
    DEPENDS ${CONFIG_PROTO}
)

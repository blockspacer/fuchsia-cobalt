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

set_source_files_properties(${COBALT_PROTO_HDRS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${CONFIG_PROTO_HDRS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${CLEARCUT_EXTENSIONS_PROTO_HDRS} PROPERTIES GENERATED TRUE)

set(ENCODER_TEST_CONFIG_H "${CMAKE_CURRENT_BINARY_DIR}/encoder_test_config.h")
generate_test_config_h(${CMAKE_CURRENT_SOURCE_DIR}/encoder_test_config.yaml
      ${ENCODER_TEST_CONFIG_H} 1 1 "cobalt_registry_base64")

set(ENVELOPE_MAKER_TEST_CONFIG_H "${CMAKE_CURRENT_BINARY_DIR}/envelope_maker_test_config.h")
generate_test_config_h(${CMAKE_CURRENT_SOURCE_DIR}/envelope_maker_test_config.yaml
      ${ENVELOPE_MAKER_TEST_CONFIG_H} 1 1 "cobalt_registry_base64")

set(SHIPPING_MANAGER_TEST_CONFIG_H "${CMAKE_CURRENT_BINARY_DIR}/shipping_manager_test_config.h")
generate_test_config_h(${CMAKE_CURRENT_SOURCE_DIR}/shipping_manager_test_config.yaml
      ${SHIPPING_MANAGER_TEST_CONFIG_H} 1 1 "cobalt_registry_base64")

set(FILE_OBSERVATION_STORE_TEST_CONFIG_H "${CMAKE_CURRENT_BINARY_DIR}/file_observation_store_test_config.h")
generate_test_config_h(${CMAKE_CURRENT_SOURCE_DIR}/file_observation_store_test_config.yaml
      ${FILE_OBSERVATION_STORE_TEST_CONFIG_H} 1 1 "cobalt_registry_base64")

# client_secret library
add_library(client_secret client_secret.cc)
target_link_libraries(client_secret cobalt_crypto)
add_cobalt_dependencies(client_secret)

cobalt_make_protobuf_cpp_lib(file_observation_store_internal
                             FILE_OBSERVATION_STORE_INTERNAL_HDRS
                             false
                             file_observation_store_internal)

# encoder library
add_library(encoder
            encoder.cc
            envelope_maker.cc
            project_context.cc
            send_retryer.cc
            shipping_manager.cc
            shuffler_client.cc
            system_data.cc
            observation_store.cc
            memory_observation_store.cc
            file_observation_store.cc
            upload_scheduler.cc
            "../third_party/protobuf/src/google/protobuf/util/delimited_message_util.cc"
            ${COBALT_PROTO_HDRS} ${CONFIG_PROTO_HDRS}
            ${FILE_OBSERVATION_STORE_INTERNAL_HDRS})
target_link_libraries(encoder
                      absl::synchronization
                      client_config
                      client_secret
                      encrypted_message_util
                      forculus_encrypter
                      rappor_encoder
                      shuffler_grpc_client
                      clearcut_extensions_proto_lib
                      file_observation_store_internal)
add_cobalt_dependencies(encoder)

add_executable(encoder_tests
               client_secret_test.cc
               encoder_test.cc
               ${ENCODER_TEST_CONFIG_H}
               envelope_maker_test.cc
               ${ENVELOPE_MAKER_TEST_CONFIG_H}
               send_retryer_test.cc
               shipping_manager_test.cc
               ${SHIPPING_MANAGER_TEST_CONFIG_H}
               shuffler_client_test.cc
               system_data_test.cc
               ${FILE_OBSERVATION_STORE_TEST_CONFIG_H}
               file_observation_store_test.cc
               upload_scheduler_test.cc)
target_link_libraries(encoder_tests
                      client_secret
                      encoder
                      posix_file_system)
add_cobalt_test_dependencies(encoder_tests ${DIR_GTESTS})
add_dependencies(encoder_tests build_config_parser)

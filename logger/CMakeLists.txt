# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(INTERNAL_METRICS_CONFIG_H "${CMAKE_CURRENT_BINARY_DIR}/internal_metrics_config.cb.h")
generate_cobalt_registry_h(
  CUSTOMER_ID 2147483647
  PROJECT_NAME metrics
  NAMESPACE cobalt.logger
  OUTPUT_FILE "${INTERNAL_METRICS_CONFIG_H}"
)

cobalt_make_protobuf_cpp_lib(local_aggregation_proto_lib
                             LOCAL_AGGREGATION_PROTO_HDRS
                             false
                             local_aggregation)
add_cobalt_dependencies(local_aggregation_proto_lib)

add_library(project_context
            project_context.cc)
target_link_libraries(project_context
                      local_aggregation_proto_lib)
add_cobalt_dependencies(project_context)
target_link_libraries(project_context project_configs tensorflow_statusor)

add_library(observation_writer
            observation_writer.cc)
add_cobalt_dependencies(observation_writer)

add_library(internal_metrics
            internal_metrics.cc
            "${INTERNAL_METRICS_CONFIG_H}")
add_cobalt_dependencies(internal_metrics)

add_library(event_aggregator
            event_aggregator.cc)
target_link_libraries(event_aggregator
                      local_aggregation_proto_lib
                      proto_util)
add_cobalt_dependencies(event_aggregator)

add_library(encoder2
            encoder.cc)
target_link_libraries(encoder2
                      client_secret
                      forculus_encrypter
                      project_context
                      rappor_config_helper
                      rappor_encoder)
add_cobalt_dependencies(encoder2)

add_library(logger
            logger.cc)
target_link_libraries(logger
                      encoder2
                      encrypted_message_util
                      event_aggregator
                      observation_writer
                      project_context
                      rappor_config_helper
                      internal_metrics
                      config_ids)
add_cobalt_dependencies(logger)

add_library(logger_test_utils
            logger_test_utils.cc)
target_link_libraries(logger_test_utils
                      consistent_proto_store
                      encoder2
                      encoder
                      logger
                      local_aggregation_proto_lib
                      posix_file_system
		      proto_util)
add_cobalt_dependencies(logger_test_utils)

add_executable(logger_tests
               encoder_test.cc
               event_aggregator_test.cc
               logger_test.cc
               project_context_test.cc)
target_link_libraries(logger_tests
                      encoder2
                      event_aggregator
                      logger
                      logger_test_utils)
add_cobalt_test_dependencies(logger_tests ${DIR_GTESTS})
add_dependencies(logger_tests build_config_parser)

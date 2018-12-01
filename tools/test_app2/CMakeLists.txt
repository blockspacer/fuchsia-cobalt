# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build the test_app library.
add_library(test_app2 test_app.cc)
target_link_libraries(test_app2
                      consistent_proto_store
                      curl_http_client
                      encoder
                      event_aggregator
                      logger
                      pem_util
                      posix_file_system
                      project_configs)
add_cobalt_dependencies(test_app2)

# Build the cobalt_test_app executable.
add_executable(cobalt_test_app2
               test_app_main.cc)
target_link_libraries(cobalt_test_app2
                      test_app2)
add_cobalt_dependencies(cobalt_test_app2)

# Build the tests
add_executable(test_app2_tests
               test_app_test.cc)
target_link_libraries(test_app2_tests
                      logger test_app2)
add_cobalt_test_dependencies(test_app2_tests ${DIR_GTESTS})

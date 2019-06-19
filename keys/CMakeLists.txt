# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

add_executable(keys_tests
               keys_tests.cc)
target_link_libraries(keys_tests
                      encrypted_message_util
                      file_util)
add_cobalt_test_dependencies(keys_tests ${DIR_GTESTS})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/analyzer_public_key.tink
  ${DIR_GTESTS}/keys/analyzer_public_key.tink COPYONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/shuffler_public_key.tink
  ${DIR_GTESTS}/keys/shuffler_public_key.tink COPYONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/analyzer_public.cobalt_key
  ${DIR_GTESTS}/keys/analyzer_public.cobalt_key COPYONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/shuffler_public.cobalt_key
  ${DIR_GTESTS}/keys/shuffler_public.cobalt_key COPYONLY)

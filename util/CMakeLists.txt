# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set_source_files_properties(${COBALT_PROTO_SRCS} PROPERTIES GENERATED TRUE)

add_library(datetime_util datetime_util.cc)
target_link_libraries(datetime_util)
add_cobalt_dependencies(datetime_util)

add_library(encrypted_message_util
            encrypted_message_util.cc
            ${COBALT_PROTO_SRCS})
target_link_libraries(encrypted_message_util
                      cobalt_crypto)
add_cobalt_dependencies(encrypted_message_util)

add_library(pem_util
            pem_util.cc)
target_link_libraries(pem_util
                      cobalt_crypto)
add_cobalt_dependencies(pem_util)

add_executable(util_tests
               datetime_util_test.cc
               encrypted_message_util_test.cc
               consistent_proto_store_test.cc)
target_link_libraries(util_tests
                      datetime_util
                      encrypted_message_util
                      posix_file_system
                      consistent_proto_store
                      consistent_proto_store_test_proto)
add_cobalt_test_dependencies(util_tests ${DIR_GTESTS})

add_subdirectory(clearcut)
add_subdirectory(crypto_util)
add_subdirectory(gcs)
add_subdirectory(lossmin)

add_library(status
            status.cc
            status_codes.h
            status.h)
add_cobalt_dependencies(status)

add_library(posix_file_system
            posix_file_system.h
            posix_file_system.cc)
target_link_libraries(posix_file_system
                      status
                      tensorflow_statusor)
add_cobalt_dependencies(posix_file_system)

cobalt_make_protobuf_cpp_lib(consistent_proto_store_test_proto
                             CONSISTENT_PROTO_STORE_PROTO_HDRS
                             false
                             consistent_proto_store_test)

add_library(consistent_proto_store
            consistent_proto_store.h
            consistent_proto_store.cc
            ${CONSISTENT_PROTO_STORE_PROTO_HDRS})
target_link_libraries(consistent_proto_store
                      status)
add_cobalt_dependencies(consistent_proto_store)

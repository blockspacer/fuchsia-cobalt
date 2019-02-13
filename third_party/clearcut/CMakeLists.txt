# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


# Runs the protoc compiler on a set of .proto files to generate c++ files.
# Compiles the c++ files into a static library.
#
# Args:
# LIB_NAME: The name of the CMake target for the generated static library
# HDRS_OUT: A variable in which to write the list of names of the generated
#           header files
# USE_GRPC: A bool indicating whether or not use the gRPC plugin.
# <remaining args>: List of simple names of .proto files to include from the
#                   current source directory. The names should not include
#                   ".proto"
# example usage:
#
# make_protobuf_cpp_lib_cc(report_master_proto_lib
#                              REPORT_PROTO_HDRS
#                              true
#                              report_master report_internal)
#
# This will compile the files report_master.proto and report_internal.proto in
# the current source directory and generate a static library with a
# target name of report_master_proto_lib containing the ReportMaster gRPC
# service as well as the compiled protos from report_internal. The variable
# REPORT_PROTO_HDRS will contain the list of strings:
# { <some-path>/report_internal.grpc.pb.h
#   <some-path>/report_internal.pb.h
#   <some-path>/report_master.grpc.pb.h
#   <some-path>/report_master.pb.h}
# (Note that report_internal.proto does not contain a gRPC service definition
#  so that report_internal.grpc.pb.h is essentially empty.)
macro(make_protobuf_cpp_lib_cc LIB_NAME HDRS_OUT USE_GRPC)
  set(_protofiles)
  set(_generated_srcs)
  set(_generated_hdrs)
  foreach(name ${ARGN})
      list(APPEND _protofiles "${CMAKE_CURRENT_SOURCE_DIR}/${name}.proto")
      list(APPEND _generated_srcs "${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.cc")
      list(APPEND _generated_hdrs "${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.h")
      set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.cc" PROPERTIES GENERATED TRUE)
      set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.h" PROPERTIES GENERATED TRUE)
      if(${USE_GRPC})
        list(APPEND _generated_srcs "${CMAKE_CURRENT_BINARY_DIR}/${name}.grpc.pb.cc")
        list(APPEND _generated_hdrs "${CMAKE_CURRENT_BINARY_DIR}/${name}.grpc.pb.h")
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/${name}.grpc.pb.cc" PROPERTIES GENERATED TRUE)
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/${name}.grpc.pb.h" PROPERTIES GENERATED TRUE)
      endif()
  endforeach()
  set(_grpc_spec)
  if (${USE_GRPC})
    set(_grpc_spec
      --grpc_out=${CMAKE_BINARY_DIR}
      --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
    )
  endif()
  add_custom_command(OUTPUT ${_generated_srcs} ${_generated_hdrs}
    COMMAND ${PROTOC} ${_protofiles}
            -I ${CMAKE_SOURCE_DIR}
            -I ${PROTOBUF_INCLUDE_DIR}
            --cpp_out=${CMAKE_BINARY_DIR}
            ${_grpc_spec}
    DEPENDS ${_protofiles}
    DEPENDS build_external_projects
  )
  add_library(${LIB_NAME}
    ${_generated_srcs}
  )
  if(${USE_GRPC})
    # This is a bit of a hack. We want to add dependencies on
    # cobalt_proto_lib and config_proto_lib as long as those aren't the
    # library we are currently building. We know those two are not gRPC
    # libraries.
    add_cobalt_dependencies(${LIB_NAME})
  else()
    add_base_dependencies(${LIB_NAME})
  endif()
  set(${HDRS_OUT} ${_generated_hdrs})
endmacro()

make_protobuf_cpp_lib_cc(clearcut_proto_lib
                         CLEARCUT_PROTO_HDRS
                         false
                         clearcut)

add_library(clearcut
            http_client.h
            uploader.h
            uploader.cc)
target_link_libraries(clearcut
                      absl::strings clearcut_proto_lib
                      statusor status)


add_executable(clearcut_tests
               uploader_test.cc)
target_link_libraries(clearcut_tests
                      clearcut
                      gtest
                      gtest_main)
set_target_properties(clearcut_tests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gtests")

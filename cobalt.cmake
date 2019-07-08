# This file can be used to include almost all the Cobalt source files. See
# [fuchsia docs](https://fuchsia.googlesource.com/fuchsia/+/master/docs/development/languages/c-cpp/editors.md#cmake)
# for usage.
cmake_minimum_required(VERSION 3.9)
project(cobalt)
set(CMAKE_CXX_STANDARD 17)
# Some header files include additional header files based on `__Fuchsia__`'s presence
# (e.g. https://fuchsia.googlesource.com/garnet/+/master/public/lib/fidl/cpp/internal/header.h).
add_compile_definitions(__Fuchsia__)
# This section adds any sub-directory named "include" with the assumption that it is the root for
# include paths.
file(GLOB_RECURSE ALL_DIRS LIST_DIRECTORIES true
        ${PROJECT_SOURCE_DIR}/out/*
        ${PROJECT_SOURCE_DIR}/src/*
        ${PROJECT_SOURCE_DIR}/third_party/*
        )
set(INCLUDE_DIRS "")
foreach(dir IN LISTS ALL_DIRS)
    if(${dir} MATCHES "\/include$")
        message("adding ${dir}")
        set(INCLUDE_DIRS ${INCLUDE_DIRS} ${dir})
    endif()
endforeach()
include_directories(${INCLUDE_DIRS})
LIST(LENGTH INCLUDE_DIRS n_dirs)
message("${n_dirs} directories named 'include' added to include_directories")
# Any other include roots must be manually added here.
include_directories(
        ${PROJECT_SOURCE_DIR}
        )
# Find all sources files and add them to an executable so that CLion will know to scan them.
file(GLOB_RECURSE SRC
        ${PROJECT_SOURCE_DIR}/*.h
        ${PROJECT_SOURCE_DIR}/*.hh
        ${PROJECT_SOURCE_DIR}/*.hpp
        ${PROJECT_SOURCE_DIR}/*.c
        ${PROJECT_SOURCE_DIR}/*.cc
        ${PROJECT_SOURCE_DIR}/*.cpp
        )
add_executable(fuchsia ${SRC})

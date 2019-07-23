// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CONFIG_CONFIG_TEXT_PARSER_H_
#define COBALT_CONFIG_CONFIG_TEXT_PARSER_H_

#include <memory>
#include <string>
#include <utility>

#include <google/protobuf/text_format.h>

#include "config/config.h"

// This file contains templatized functions for parsing Protocol Buffer
// text files. It works in conjunction with config.h. The text parsing is
// isolated to a separate file and only used in tests. This is because
// text parsing is not included in the *proto lite* library and we wish to
// use this version of the library in Fuchsia.
//
// RT should be one of:
// RegisteredEncodings, RegisteredReports, RegisteredMetrics.

namespace cobalt {
namespace config {

template <class RT>
// Populates a new instance of Registry<RT> by reading and parsing the
// specified file. Returns a pair consisting of a pointer to the result and a
// Status.
//
// If the operation is successful then the status is kOK. Otherwise the
// Status indicates the error.
//
// If |error_collector| is not null then it will be notified of any parsing
// errors or warnings.
static std::pair<std::unique_ptr<Registry<RT>>, Status> FromFile(
    const std::string& file_path, google::protobuf::io::ErrorCollector* error_collector) {
  // Make an empty registry to return;
  std::unique_ptr<Registry<RT>> registry(new Registry<RT>());

  // Try to open the specified file.
  int fd = open(file_path.c_str(), O_RDONLY);
  if (fd < 0) {
    return std::make_pair(std::move(registry), kFileOpenError);
  }

  // Try to parse the specified file.
  google::protobuf::io::FileInputStream file_input_stream(fd);
  file_input_stream.SetCloseOnDelete(true);
  // The contents of the file should be a serialized |RT|.
  RT registered_configs;
  google::protobuf::TextFormat::Parser parser;
  if (error_collector) {
    parser.RecordErrorsTo(error_collector);
  }
  if (!parser.Parse(&file_input_stream, &registered_configs)) {
    return std::make_pair(std::move(registry), kParsingError);
  }

  return Registry<RT>::TakeFrom(&registered_configs, error_collector);
}

template <class RT>
// Populates a new instance of Registry<RT> by reading and parsing the
// specified string. Returns a pair consisting of a pointer to the result and
// a Status.
//
// If the operation is successful then the status is kOK. Otherwise the
// Status indicates the error.
//
// If |error_collector| is not null then it will be notified of any parsing
// errors or warnings.
static std::pair<std::unique_ptr<Registry<RT>>, Status> FromString(
    const std::string& contents, google::protobuf::io::ErrorCollector* error_collector) {
  // Make an empty registry to return;
  std::unique_ptr<Registry<RT>> registry(new Registry<RT>());

  // Try to parse the specified string
  // The contents of the string should be a serialized |RT|.
  RT registered_configs;
  google::protobuf::TextFormat::Parser parser;
  if (error_collector) {
    parser.RecordErrorsTo(error_collector);
  }
  if (!parser.ParseFromString(contents, &registered_configs)) {
    return std::make_pair(std::move(registry), kParsingError);
  }

  return Registry<RT>::TakeFrom(&registered_configs, error_collector);
}

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_CONFIG_TEXT_PARSER_H_

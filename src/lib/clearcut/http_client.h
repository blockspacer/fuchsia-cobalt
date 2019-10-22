// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_CLEARCUT_HTTP_CLIENT_H_
#define COBALT_SRC_LIB_CLEARCUT_HTTP_CLIENT_H_

#include <future>
#include <map>

#include "src/lib/statusor/statusor.h"
#include "src/lib/util/status.h"

namespace cobalt::lib::clearcut {

using cobalt::util::Status;
using lib::statusor::StatusOr;

// HTTPResponse contains the response from the server.
//
// This class is move-only since response may be large.
class HTTPResponse {
 public:
  std::string response;
  Status status;
  int64_t http_code;
  std::map<std::string, std::string> headers;

  HTTPResponse() = default;
  HTTPResponse(std::string response, Status status, int64_t http_code,
               std::map<std::string, std::string> headers = std::map<std::string, std::string>())
      : response(std::move(response)),
        status(std::move(status)),
        http_code(http_code),
        headers(std::move(headers)) {}

  HTTPResponse(HTTPResponse&&) = default;
  HTTPResponse& operator=(HTTPResponse&&) = default;

  HTTPResponse(const HTTPResponse&) = delete;
  HTTPResponse& operator=(const HTTPResponse&) = delete;
};

// HTTPRequest contains information used to make a Post request to clearcut.
//
// This class is non-copyable since url/body may be large.
class HTTPRequest {
 public:
  std::string url;
  std::string body;
  std::map<std::string, std::string> headers;

  explicit HTTPRequest(std::string url, std::string body = "")
      : url(std::move(url)), body(std::move(body)) {}
  HTTPRequest(HTTPRequest&&) = default;
  HTTPRequest& operator=(HTTPRequest&&) = default;

  HTTPRequest(const HTTPRequest&) = delete;
  HTTPRequest& operator=(const HTTPRequest&) = delete;
};

class HTTPClient {
 public:
  // Post an HTTPRequest which will timeout after |timeout_ms| milliseconds.
  virtual std::future<StatusOr<HTTPResponse>> Post(
      HTTPRequest request, std::chrono::steady_clock::time_point deadline) = 0;

  virtual ~HTTPClient() = default;
};

}  // namespace cobalt::lib::clearcut

#endif  // COBALT_SRC_LIB_CLEARCUT_HTTP_CLIENT_H_

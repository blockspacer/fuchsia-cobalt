// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_CLEARCUT_HTTP_CLIENT_H_
#define THIRD_PARTY_CLEARCUT_HTTP_CLIENT_H_

#include <future>
#include <map>

#include "src/lib/util/status.h"
#include "third_party/statusor/statusor.h"

namespace clearcut {

using cobalt::util::Status;
using statusor::StatusOr;

// HTTPResponse contains the response from the server.
//
// This class is move-only since response may be large.
class HTTPResponse {
 public:
  std::string response;
  Status status;
  int64_t http_code;

  HTTPResponse() {}
  HTTPResponse(std::string response, Status status, int64_t http_code)
      : response(std::move(response)), status(status), http_code(http_code) {}

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

  HTTPRequest(std::string url, std::string body = "")
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

}  // namespace clearcut

#endif  // THIRD_PARTY_CLEARCUT_HTTP_CLIENT_H_

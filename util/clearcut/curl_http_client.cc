// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/clearcut/curl_http_client.h"

#include <iostream>
#include <utility>

#include "util/clearcut/curl_handle.h"

namespace cobalt {
namespace util {
namespace clearcut {

using clearcut::HTTPClient;
using clearcut::HTTPRequest;
using clearcut::HTTPResponse;

bool CurlHTTPClient::global_init_called_ = false;

CurlHTTPClient::CurlHTTPClient() {
  if (!CurlHTTPClient::global_init_called_) {
    CurlHTTPClient::global_init_called_ = true;
    curl_global_init(CURL_GLOBAL_ALL);
  }
}

std::future<StatusOr<HTTPResponse>> CurlHTTPClient::Post(
    HTTPRequest request, std::chrono::steady_clock::time_point deadline) {
  return std::async(std::launch::async,
                    [request = std::move(request), deadline]() mutable -> StatusOr<HTTPResponse> {
                      auto handle_or = CurlHandle::Init();
                      if (!handle_or.ok()) {
                        return handle_or.status();
                      }
                      auto handle = handle_or.ConsumeValueOrDie();
                      auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            deadline - std::chrono::steady_clock::now())
                                            .count();
                      handle->SetTimeout(timeout_ms);
                      handle->SetHeaders(request.headers);
                      return handle->Post(request.url, request.body);
                    });
}

}  // namespace clearcut
}  // namespace util
}  // namespace cobalt

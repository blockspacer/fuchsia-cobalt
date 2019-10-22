// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_CLEARCUT_CURL_HANDLE_H_
#define COBALT_SRC_LIB_CLEARCUT_CURL_HANDLE_H_

#include <map>
#include <memory>
#include <string>

#include <curl/curl.h>

#include "src/lib/clearcut/http_client.h"
#include "src/lib/util/status.h"
#include "third_party/statusor/statusor.h"

namespace cobalt::lib::clearcut {

using clearcut::HTTPResponse;
using statusor::StatusOr;
using util::Status;

// CurlHandle wraps around a CURL * to make it easier to interact with curl.
class CurlHandle {
 public:
  ~CurlHandle();

  template <class Param>
  Status Setopt(CURLoption option, Param parameter);
  Status SetHeaders(const std::map<std::string, std::string> &headers);
  Status SetTimeout(int64_t timeout_ms);

  static StatusOr<std::unique_ptr<CurlHandle>> Init();

  StatusOr<HTTPResponse> Post(const std::string &url, std::string body);

 private:
  CurlHandle();

  static size_t WriteResponseData(char *ptr, size_t size, size_t nmemb, void *userdata);
  Status CURLCodeToStatus(CURLcode code);

  char errbuf_[CURL_ERROR_SIZE];
  std::string response_body_;
  CURL *handle_;

  // Disallow copy and assign
  CurlHandle(const CurlHandle &) = delete;
  CurlHandle &operator=(const CurlHandle &) = delete;
};

}  // namespace cobalt::lib::clearcut

#endif  // COBALT_SRC_LIB_CLEARCUT_CURL_HANDLE_H_

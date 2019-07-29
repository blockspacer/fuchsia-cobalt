// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/clearcut/curl_handle.h"

#include <iostream>
#include <map>
#include <memory>
#include <string>

namespace cobalt {
namespace util {
namespace clearcut {

CurlHandle::CurlHandle() {
  handle_ = curl_easy_init();
  if (handle_ == nullptr) {
    throw Status(StatusCode::INTERNAL, "curl_easy_init() returned nullptr");
  }
}

CurlHandle::~CurlHandle() {
  if (handle_ != nullptr) {
    curl_easy_cleanup(handle_);
    handle_ = nullptr;
  }
}

StatusOr<std::unique_ptr<CurlHandle>> CurlHandle::Init() {
  try {
    std::unique_ptr<CurlHandle> handle(new CurlHandle());
    RETURN_IF_ERROR(handle->Setopt(CURLOPT_ERRORBUFFER, handle->errbuf_));
    RETURN_IF_ERROR(handle->Setopt(CURLOPT_WRITEDATA, &handle->response_body_));
    RETURN_IF_ERROR(handle->Setopt(CURLOPT_WRITEFUNCTION, CurlHandle::WriteResponseData));
    return handle;
  } catch (const Status &s) {
    return s;
  }
}

size_t CurlHandle::WriteResponseData(char *ptr, size_t size, size_t nmemb, void *userdata) {
  (reinterpret_cast<std::string *>(userdata))->append(reinterpret_cast<char *>(ptr), size * nmemb);
  return size * nmemb;
}

template <class Param>
Status CurlHandle::Setopt(CURLoption option, Param parameter) {
  auto res = curl_easy_setopt(handle_, option, parameter);
  if (res != CURLE_OK) {
    return CURLCodeToStatus(res);
  }
  return Status::OK;
}

Status CurlHandle::SetHeaders(const std::map<std::string, std::string> &headers) {
  if (!headers.empty()) {
    struct curl_slist *header_list = nullptr;
    for (const auto &header : headers) {
      std::string header_str = header.first + ": " + header.second;
      if (header.second.empty()) {
        header_str = header.first + ";";
      }
      header_list = curl_slist_append(header_list, header_str.c_str());
    }
    return Setopt(CURLOPT_HTTPHEADER, header_list);
  }
  return Status::OK;
}

Status CurlHandle::SetTimeout(int64_t timeout_ms) {
  if (timeout_ms > 0) {
    return Setopt(CURLOPT_TIMEOUT_MS, timeout_ms);
  }
  return Status::OK;
}

Status CurlHandle::CURLCodeToStatus(CURLcode code) {
  std::string details;
  if (strlen(errbuf_) != 0) {
    details = errbuf_;
  }
  return Status(StatusCode::INTERNAL, curl_easy_strerror(code), details);
}

StatusOr<HTTPResponse> CurlHandle::Post(const std::string &url, std::string body) {
  RETURN_IF_ERROR(Setopt(CURLOPT_URL, url.c_str()));
  RETURN_IF_ERROR(Setopt(CURLOPT_POSTFIELDSIZE, body.size()));
  RETURN_IF_ERROR(Setopt(CURLOPT_POSTFIELDS, body.data()));
  auto result = curl_easy_perform(handle_);

  switch (result) {
    case CURLE_OK:
      int64_t response_code;
      curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &response_code);
      return HTTPResponse(response_body_, Status::OK, response_code);
    case CURLE_OPERATION_TIMEDOUT:
      return Status(StatusCode::DEADLINE_EXCEEDED, "Post request timed out.");
    default:
      return CURLCodeToStatus(result);
  }
}

}  // namespace clearcut
}  // namespace util
}  // namespace cobalt

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/clearcut/uploader.h"

#include <algorithm>
#include <cmath>

#include "src/lib/clearcut/clearcut.pb.h"
#include "src/logging.h"
#include "third_party/statusor/status_macros.h"
#include "unistd.h"

namespace cobalt::lib::clearcut {

using cobalt::util::SleeperInterface;
using cobalt::util::Status;
using cobalt::util::StatusCode;
using cobalt::util::SteadyClockInterface;

ClearcutUploader::ClearcutUploader(std::string url, std::unique_ptr<HTTPClient> client,
                                   int64_t upload_timeout_millis, int64_t initial_backoff_millis,
                                   std::unique_ptr<SteadyClockInterface> steady_clock,
                                   std::unique_ptr<cobalt::util::SleeperInterface> sleeper)
    : url_(std::move(url)),
      client_(std::move(client)),
      upload_timeout_(std::chrono::milliseconds(upload_timeout_millis)),
      initial_backoff_(std::chrono::milliseconds(initial_backoff_millis)),
      steady_clock_(std::move(steady_clock)),
      sleeper_(std::move(sleeper)),
      pause_uploads_until_(steady_clock_->now())  // Set this to now() so that we
                                                  // can immediately upload.
{
  CHECK(steady_clock_);
  CHECK(sleeper_);
}

Status ClearcutUploader::UploadEvents(LogRequest* log_request, int32_t max_retries) {
  int32_t i = 0;
  auto deadline = std::chrono::steady_clock::time_point::max();
  if (upload_timeout_ > std::chrono::milliseconds(0)) {
    deadline = steady_clock_->now() + upload_timeout_;
  }
  auto backoff = initial_backoff_;
  while (true) {
    Status response = TryUploadEvents(log_request, deadline);
    if (response.ok() || ++i == max_retries) {
      return response;
    }
    switch (response.error_code()) {
      case StatusCode::INVALID_ARGUMENT:
      case StatusCode::NOT_FOUND:
      case StatusCode::PERMISSION_DENIED:
        // Don't retry permanent errors.
        LOG(WARNING) << "Got a permanent error from TryUploadEvents: " << response.error_message();
        return response;
      default:
        break;
    }
    if (steady_clock_->now() > deadline) {
      return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded.");
    }
    // Exponential backoff.
    auto time_until_pause_end = pause_uploads_until_ - steady_clock_->now();
    if (time_until_pause_end > backoff) {
      VLOG(5) << "ClearcutUploader: Sleeping for time requested by server: "
              << std::chrono::duration<double>(time_until_pause_end).count() << "s";
      sleeper_->sleep_for(
          std::chrono::duration_cast<std::chrono::milliseconds>(time_until_pause_end));
    } else {
      VLOG(5) << "ClearcutUploader: Sleeping for backoff time: "
              << std::chrono::duration<double>(backoff).count() << "s";
      sleeper_->sleep_for(backoff);
    }
    backoff *= 2;
  }
}

Status ClearcutUploader::TryUploadEvents(LogRequest* log_request,
                                         std::chrono::steady_clock::time_point deadline) {
  if (steady_clock_->now() < pause_uploads_until_) {
    return Status(StatusCode::RESOURCE_EXHAUSTED,
                  "Uploads are currently paused at the request of the "
                  "clearcut server");
  }

  log_request->mutable_client_info()->set_client_type(kFuchsiaClientType);
  HTTPResponse response;
  // Because we will be moving the request body into the Post() method it will not be available to
  // us later. Here we keep an escaped copy of the request body just in case we need to use it
  // in an error log message later.
  std::string escaped_request_body;
  {
    HTTPRequest request(url_);
    if (!log_request->SerializeToString(&request.body)) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    "ClearcutUploader: Unable to serialize log_request to binary proto.");
    }
    escaped_request_body = absl::CEscape(request.body);
    VLOG(5) << "ClearcutUploader: Sending POST request to " << url_ << ".";
    auto response_future = client_->Post(std::move(request), deadline);

    auto response_or = response_future.get();
    if (!response_or.ok()) {
      const Status& status = response_or.status();
      VLOG(5) << "ClearcutUploader: Failed to send POST request: (" << status.error_code() << ") "
              << status.error_message();
      return status;
    }

    response = response_or.ConsumeValueOrDie();
  }

  constexpr auto kHttpOk = 200;
  constexpr auto kHttpBadRequest = 400;
  constexpr auto kHttpUnauhtorized = 401;
  constexpr auto kHttpForbidden = 403;
  constexpr auto kHttpFNotFound = 404;
  constexpr auto kHttpServiceUnavailable = 503;

  VLOG(5) << "ClearcutUploader: Received POST response: " << response.http_code << ".";
  if (response.http_code != kHttpOk) {
    std::ostringstream s;
    std::string escaped_response_body = absl::CEscape(response.response);
    s << "ClearcutUploader: Response was not OK: " << response.http_code << ".";
    s << " url=" << url_;
    s << " response contained " << response.headers.size() << " <headers>";
    for (const auto& pair : response.headers) {
      s << "<key>" << pair.first << "</key>"
        << ":"
        << "<value>" << pair.second << "</value>,";
    }
    s << "</headers>";
    s << " request <body>" << escaped_request_body << "</body>.";
    s << " response <body>" << escaped_response_body << "</body>.";
    VLOG(1) << s.str();

    std::ostringstream stauts_string_stream;
    stauts_string_stream << response.http_code << ": ";
    switch (response.http_code) {
      case kHttpBadRequest:  // bad request
        stauts_string_stream << "Bad Request";
        return Status(StatusCode::INVALID_ARGUMENT, stauts_string_stream.str());
      case kHttpUnauhtorized:  // Unauthorized
      case kHttpForbidden:     // forbidden
        stauts_string_stream << "Permission Denied";
        return Status(StatusCode::PERMISSION_DENIED, stauts_string_stream.str());
      case kHttpFNotFound:  // not found
        stauts_string_stream << "Not Found";
        return Status(StatusCode::NOT_FOUND, stauts_string_stream.str());
      case kHttpServiceUnavailable:  // service unavailable
        stauts_string_stream << "Service Unavailable";
        return Status(StatusCode::RESOURCE_EXHAUSTED, stauts_string_stream.str());
      default:
        stauts_string_stream << "Unknown Error Code";
        return Status(StatusCode::UNKNOWN, stauts_string_stream.str());
    }
  }

  LogResponse log_response;
  if (!log_response.ParseFromString(response.response)) {
    return Status(StatusCode::INTERNAL, "Unable to parse response from clearcut server");
  }

  if (log_response.next_request_wait_millis() >= 0) {
    pause_uploads_until_ =
        steady_clock_->now() + std::chrono::milliseconds(log_response.next_request_wait_millis());
  }

  return Status::OK;
}

}  // namespace cobalt::lib::clearcut

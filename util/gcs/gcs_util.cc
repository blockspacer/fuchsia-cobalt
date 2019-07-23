// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/gcs/gcs_util.h"

#include <memory>
#include <string>
#include <vector>

#include "glog/logging.h"
#include "third_party/google-api-cpp-client/service_apis/storage/google/storage_api/storage_api.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/auth/oauth2_authorization.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/auth/oauth2_service_authorization.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/data/data_reader.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/transport/curl_http_transport.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/transport/http_transport.h"
#include "third_party/google-api-cpp-client/src/googleapis/strings/stringpiece.h"
#include "util/log_based_metrics.h"
#include "util/pem_util.h"

using google_storage_api::BucketsResource_GetMethod;
using google_storage_api::ObjectsResource_InsertMethod;
using google_storage_api::StorageService;
using googleapis::client::CurlHttpTransportFactory;
using googleapis::client::DataReader;
using googleapis::client::HttpTransportLayerConfig;
using googleapis::client::NewUnmanagedInMemoryDataReader;
using googleapis::client::NewUnmanagedIstreamDataReader;
using googleapis::client::OAuth2Credential;
using googleapis::client::OAuth2ServiceAccountFlow;

namespace cobalt {
namespace util {
namespace gcs {

// Stackdriver metric constants
namespace {
constexpr char kInitFailure[] = "gcs-util-init-failure";
constexpr char kUploadFailure[] = "gcs-util-upload-failure";
constexpr char kPingFailure[] = "gcs-util-ping-failure";
}  // namespace

struct GcsUtil::Impl {
  googleapis::client::OAuth2Credential oauth_credential_;
  std::unique_ptr<google_storage_api::StorageService> storage_service_;
  std::unique_ptr<googleapis::client::OAuth2ServiceAccountFlow> oauth_flow_;
  std::unique_ptr<googleapis::client::HttpTransportLayerConfig> http_config_;
};

GcsUtil::GcsUtil() : impl_(new Impl()) {}

GcsUtil::~GcsUtil() {}

bool GcsUtil::InitFromDefaultPaths() {
  // When ReportMaster is deployed to Google Container Engine, the environment
  // variable "GRPC_DEFAULT_SSL_ROOTS_FILE_PATH" is set in the file
  // //kubernetes/cobalt_common/Dockerfile
  char* p = std::getenv("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH");
  if (!p) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kInitFailure)
        << "The environment variable GRPC_DEFAULT_SSL_ROOTS_FILE_PATH "
           "is not set.";
    return false;
  }
  std::string ca_certs_path(p);

  // When ReportMaster is deployed to Google Container Engine, the environment
  // variable "COBALT_GCS_SERVICE_ACCOUNT_CREDENTIALS" is set in the
  // file //kubernets/report_master/Dockerfile
  p = std::getenv("COBALT_GCS_SERVICE_ACCOUNT_CREDENTIALS");
  if (!p) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kInitFailure)
        << "The environment variable "
           "COBALT_GCS_SERVICE_ACCOUNT_CREDENTIALS is not set.";
    return false;
  }
  std::string service_account_json_path(p);
  return Init(ca_certs_path, service_account_json_path);
}

bool GcsUtil::Init(const std::string ca_certs_path,
                   const std::string& service_account_json_path) {
  // Set up HttpTransportLayer.
  impl_->http_config_.reset(new HttpTransportLayerConfig);
  impl_->http_config_->ResetDefaultTransportFactory(
      new CurlHttpTransportFactory(impl_->http_config_.get()));
  impl_->http_config_->mutable_default_transport_options()->set_cacerts_path(
      ca_certs_path);

  // Set up OAuth 2.0 flow for a service account.
  googleapis::util::Status status;
  impl_->oauth_flow_.reset(new OAuth2ServiceAccountFlow(
      impl_->http_config_->NewDefaultTransport(&status)));
  if (!status.ok()) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kInitFailure)
        << "GcsUitl::Init(). Error creating new Http transport: "
        << status.ToString();
    return false;
  }

  // Load the the contents of the service account json into a string.
  std::string json;
  PemUtil::ReadTextFile(service_account_json_path, &json);
  if (json.empty()) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kInitFailure)
        << "GcsUitl::Init(). Unable to read service account json from: "
        << service_account_json_path;
    return false;
  }
  // Initialize the flow with the contents of the service account json.
  impl_->oauth_flow_->InitFromJson(json);
  impl_->oauth_flow_->set_default_scopes(
      StorageService::SCOPES::DEVSTORAGE_READ_WRITE);
  // Connect the credential with the AuthFlow.
  impl_->oauth_credential_.set_flow(impl_->oauth_flow_.get());

  // Construct the storage service.
  impl_->storage_service_.reset(
      new StorageService(impl_->http_config_->NewDefaultTransport(&status)));
  if (!status.ok()) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kInitFailure)
        << "GcsUitl::Init(). Error creating new Http transport: "
        << status.ToString();
    return false;
  }

  return true;
}

bool GcsUtil::Upload(const std::string& bucket, const std::string& path,
                     const std::string mime_type, const char* data,
                     size_t num_bytes, uint32_t timeout_seconds) {
  googleapis::StringPiece str;
  str.set(data, num_bytes);
  return Upload(bucket, path, mime_type, NewUnmanagedInMemoryDataReader(str),
                timeout_seconds);
}

bool GcsUtil::Upload(const std::string& bucket, const std::string& path,
                     const std::string mime_type, std::istream* stream,
                     uint32_t timeout_seconds) {
  return Upload(bucket, path, mime_type, NewUnmanagedIstreamDataReader(stream),
                timeout_seconds);
}

bool GcsUtil::Upload(const std::string& bucket, const std::string& path,
                     const std::string mime_type, void* data_reader,
                     uint32_t timeout_seconds) {
  // Build the request.
  // Note that according to the comments on the method
  // MediaUploader::set_media_content_reader() in //third_party/ \
  //     google-api-cpp-client/src/googleapis/client/service/media_uploader.h
  // this takes ownership of |data_reader|.
  std::unique_ptr<ObjectsResource_InsertMethod> request(
      impl_->storage_service_->get_objects().NewInsertMethod(
          &(impl_->oauth_credential_), bucket, nullptr, mime_type.c_str(),
          reinterpret_cast<DataReader*>(data_reader)));
  request->set_name(path);

  request->mutable_http_request()->mutable_options()->set_timeout_ms(
      1000 * timeout_seconds);

  // Execute the request.
  Json::Value value;
  google_storage_api::Object response(&value);
  auto status = request->ExecuteAndParseResponse(&response);
  if (status.ok()) {
    return true;
  }
  LOG_STACKDRIVER_COUNT_METRIC(ERROR, kUploadFailure)
      << "Error attempting upload: " << status.ToString();
  return false;
}

bool GcsUtil::Ping(const std::string& bucket) {
  // Construct the request.
  std::unique_ptr<BucketsResource_GetMethod> request(
      impl_->storage_service_->get_buckets().NewGetMethod(
          &(impl_->oauth_credential_), bucket));

  // Execute the request.
  auto status = request->Execute();
  if (status.ok()) {
    return true;
  }
  LOG_STACKDRIVER_COUNT_METRIC(ERROR, kPingFailure)
      << "Error attempting to ping bucket: " << status.ToString();
  return false;
}

}  // namespace gcs
}  // namespace util
}  // namespace cobalt

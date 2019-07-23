// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_UTIL_GCS_GCS_UTIL_H_
#define COBALT_UTIL_GCS_GCS_UTIL_H_

#include <memory>
#include <string>
#include <vector>

namespace cobalt {
namespace util {
namespace gcs {

// A utility for uploading files to Google Cloud Storage.
//
// Usage:
//   - construct an instance of GcsUtil
//   - Invoke one of the two Init..() methods.
//   - Repeatedly invoke Upload() to upload files.
//
// The implementation uses the library in google-api-cpp-client which in
// in turn uses libcurl for HTTP transport. From experimentation it appears
// that often times the first upload after the instance is created will fail
// with a timeout. We do not understand why. Subsequent uploads succeed. One
// workaround is to invoke the Ping() method prior to the first upload. This
// appears to succeed. We do not understand why. See
// https://github.com/google/google-api-cpp-client/issues/48
class GcsUtil {
 public:
  GcsUtil();
  ~GcsUtil();

  // Initializes this instance using default file paths for the CA root
  // certificates and service account json.
  //
  // The path to the CA root cert file is read from the environment varialbe
  // "GRPC_DEFAULT_SSL_ROOTS_FILE_PATH". The path to the service account
  // json file is read from the environment variable
  // "GOOGLE_APPLICATION_CREDENTIALS".
  //
  // Returns true on success. On failure, logs an Error and returns false.
  // If this method returns false, do not continue to use this instance.
  // Discard this instance and try again.
  bool InitFromDefaultPaths();

  // Initializes this instance by reading the CA root certificates and
  // service account json from the given paths.
  //
  // Returns true on success. On failure, logs an Error and returns false.
  // If this method returns false, do not continue to use this instance.
  // Discard this instance and try again.
  //
  // ca_certs_path. The path to a file containing a PEM encoding of CA root
  // certificates.
  //
  // service_account_json_path. The path to a json file containg a Google
  // service account private key.
  bool Init(const std::string ca_certs_path, const std::string& service_account_json_path);

  // Uploads a blob to Google Cloud Storage. |num_bytes| from |data| are
  // uploaded to the given |path| within the given |bucket|. This will succeed
  // only if the bucket already exists and the service account specified when
  // this instance was initalized has write permission on the bucket.
  // Returns true on success. On failure, logs an Error and returns false.
  //
  // |path| Will be used as the full path of the uploaded file. It must
  // follow the Google Cloud Storage Object name requirements. For best
  // results it should contain only letters, numbers, underscores, dashes
  // and forward slashes.
  //
  // |timeout_seconds| is used as the HTTP request timeout.
  bool Upload(const std::string& bucket, const std::string& path, const std::string mime_type,
              const char* data, size_t num_bytes, uint32_t timeout_seconds);

  // Uploads a blob to Google Cloud Storage. Bytes will be read from |stream|
  // until EOF. The bytes are uploaded to the given |path| within the given
  // |bucket|. This will succeed only if the bucket already exists and the
  // service account specified when this instance was initalized has write
  // permission on the bucket. Returns true on success. On failure, logs an
  // Error and returns false.
  //
  // |path| Will be used as the full path of the uploaded file. It must
  // follow the Google Cloud Storage Object name requirements. For best
  // results it should contain only letters, numbers, underscores, dashes
  // and forward slashes.
  //
  // |timeout_seconds| is used as the HTTP request timeout.
  bool Upload(const std::string& bucket, const std::string& path, const std::string mime_type,
              std::istream* stream, uint32_t timeout_seconds);

  // Attempts to connect with Google Cloud Storage and query for the metadata
  // for the specified bucket. This will succeed only if the service
  // account specified when this instance was initialized has read permission
  // on the bucket. Returns true on success. On failure, logs an Error and
  // returns.
  //
  // NOTE: For reasons not fully understood, it appears it is sometimes
  // necessary to perform a Ping() prior to the first Upload. In testing we
  // have observed that Upload will timeout unless at least one Ping() is
  // performed first. We do not understand why.
  bool Ping(const std::string& bucket);

 private:
  // This is a helper function for the two public Upload() methods. The runtime
  // type of |data_reader| must be googleapis::client::DataReader*. It is
  // declared void* here in order to avoid #including
  // //third_party/google-api-cpp-client in this header file.
  //
  // This method takes ownership of |data_reader|.
  bool Upload(const std::string& bucket, const std::string& path, const std::string mime_type,
              void* data_reader, uint32_t timeout_seconds);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace gcs
}  // namespace util
}  // namespace cobalt

#endif  // COBALT_UTIL_GCS_GCS_UTIL_H_

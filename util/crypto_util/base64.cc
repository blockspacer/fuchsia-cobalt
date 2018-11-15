// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "util/crypto_util/base64.h"

#include <openssl/base64.h>
#include <algorithm>

namespace cobalt {
namespace crypto {

bool Base64Encode(const byte* data, int len, std::string* encoded_out) {
  if (!data || !encoded_out) {
    return false;
  }
  size_t required_length;
  if (!EVP_EncodedLength(&required_length, len)) {
    return false;
  }
  encoded_out->resize(required_length);
  bool success =
      EVP_EncodeBlock(reinterpret_cast<byte*>(&(*encoded_out)[0]), data, len);
  if (!success) {
    return false;
  }
  // Remove the trailing null EVP_EncodeBlock writes. It is trying to be
  // helpful by creating a C string but we don't need it in a std::string.
  encoded_out->resize(required_length - 1);
  return true;
}

bool Base64Encode(const std::vector<byte>& data, std::string* encoded_out) {
  return Base64Encode(data.data(), data.size(), encoded_out);
}

bool Base64Encode(const std::string& data, std::string* encoded_out) {
  return Base64Encode(reinterpret_cast<const byte*>(data.data()), data.size(),
                      encoded_out);
}

bool Base64Decode(const std::string& encoded_in,
                  std::vector<byte>* decoded_out) {
  size_t required_length;
  if (!EVP_DecodedLength(&required_length, encoded_in.size())) {
    return false;
  }
  decoded_out->resize(required_length);
  size_t actual_size;
  if (!EVP_DecodeBase64(decoded_out->data(), &actual_size, decoded_out->size(),
                        reinterpret_cast<const byte*>(encoded_in.data()),
                        encoded_in.size())) {
    return false;
  }
  decoded_out->resize(actual_size);
  return true;
}

bool Base64Decode(const std::string& encoded_in, std::string* decoded_out) {
  if (!decoded_out) {
    return false;
  }
  size_t required_length;
  if (!EVP_DecodedLength(&required_length, encoded_in.size())) {
    return false;
  }
  decoded_out->resize(required_length);
  size_t actual_size;
  if (!EVP_DecodeBase64(reinterpret_cast<byte*>(&(*decoded_out)[0]),
                        &actual_size, decoded_out->size(),
                        reinterpret_cast<const byte*>(encoded_in.data()),
                        encoded_in.size())) {
    return false;
  }
  decoded_out->resize(actual_size);
  return true;
}

bool RegexEncode(const std::string& data, std::string* encoded_out) {
  if (!Base64Encode(data, encoded_out)) {
    return false;
  }
  std::replace(encoded_out->begin(), encoded_out->end(), '+', '_');
  return true;
}

bool RegexDecode(std::string encoded_in, std::string* decoded_out) {
  if (encoded_in.find('+') != std::string::npos) {
    return false;
  }
  std::replace(encoded_in.begin(), encoded_in.end(), '_', '+');
  return Base64Decode(encoded_in, decoded_out);
}

}  // namespace crypto
}  // namespace cobalt

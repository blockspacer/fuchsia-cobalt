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

#include "algorithms/forculus/field_element.h"

#include <cstring>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>

#include "util/crypto_util/types.h"

namespace cobalt {
namespace forculus {

/****************************** WARNING *************************************
 *
 * This is a temporary, insecure implementation of FieldElement. It uses a
 * prime field of size less than 2^32. This is far to small to be
 * cryptographically secure. Do not release Cobalt with this implementation.
 * We are using this implementation temporarily as we develop the
 * ForculusEncrypter and ForculusDecrypter. Our intention is to replace
 * this with the field GF(2^128).
 *
 * In this temporary implementation we assume that the hardware architecture
 * is little-endian.
 *
 *****************************************************************************/

// This is the largest prime number less than 2^32. It is 2^32 - 267.
// It is defined as 64 bits so we do 64-bit arithmetic with it by default.
static const uint64_t kLargestPrime = 4294967029;

namespace {
// Returns the uint32_t described by the first 32 bits of |bytes|
// in hardware byte order.
uint32_t AsInt32(const std::vector<byte>& bytes) {
  uint32_t x;
  std::memcpy(&x, bytes.data(), sizeof(uint32_t));
  return x;
}

// Populates result with all zeroes except for the first 32 bits which
// are taken from the uint64_t |x|, in hardware byte order.
void FromUInt64(const uint64_t& x, std::vector<byte>* result) {
  result->resize(FieldElement::kDataSize, 0);
  std::memcpy(result->data(), &x, sizeof(uint32_t));
}

// Returns the inverse of b mod kLargestPrime.
//
// In more detail, returns the integer t such that 1 <= t <= kLargestPrime
// and such that b * t mod kLargestPrime = 1.
uint32_t Inverse(uint32_t b) {
  // Becuase GCD(b, kLargestPrime) = 1 there exists integers
  // s and t such that kLargestPrime * s + b * t  = 1.
  // The least positive such t is the inverse we are looking for.
  //
  // To find t we perform the extended Euclidean algorithm. See
  // https://en.wikipedia.org/wiki/Euclidean_algorithm
  //
  // r_{k-2} = q_k * r_{k-1} + r_k
  //
  // with r_{-2} = kLargestPrime, k_{-1} = b
  //
  // t_k = t_{k-2} - q_k * t_{k-1}
  //
  // with t_{-2} = 0, t_{-1} = 1
  //
  // Stop when r_k = 0 and return t_{k-1}.

  // Initialize r_{k-2} = r_{-2} = kLargestPrime.
  uint64_t r_k2 = kLargestPrime;

  // Initialize r_{k-1} = r_{-1} = b
  uint64_t r_k1 = b;

  // Initialze t_{k-2} = t_{-2} = 0
  uint64_t t_k2 = 0;

  // Initialze t_{k-1} = t_{-1} = 1
  uint64_t t_k1 = 1;

  while (r_k1 != 0) {
    uint64_t q_k = r_k2 / r_k1;
    uint64_t r_k = r_k2 % r_k1;
    uint64_t t_k = ((t_k2 + kLargestPrime) - (q_k * t_k1) % kLargestPrime) % kLargestPrime;

    r_k2 = r_k1;
    r_k1 = r_k;
    t_k2 = t_k1;
    t_k1 = t_k;
  }
  // ASSERT: rk_1=0, r_k2=1=GCD(b, kLargestPrime)
  // kLargestPrime*s + b*t_k2 = 1, for some s we are not keeping track of.

  return t_k2;
}
}  // namespace

const size_t FieldElement::kDataSize;

FieldElement::FieldElement(std::vector<byte>&& bytes) : bytes_(std::move(bytes)) {
  // NOTE: In our temporary, insecure implementation we discard all but the
  // first 32 bits of the input.
  bytes_.resize(sizeof(uint32_t));
  bytes_.resize(kDataSize, 0);
}

FieldElement::FieldElement(const std::string& data) : bytes_(kDataSize, 0) {
  // NOTE: In our temporary, insecure implementation we discard all but the
  // first 32 bits of the input.
  for (size_t i = 0; i < sizeof(uint32_t) && i < data.size(); i++) {
    bytes_[i] = static_cast<byte>(data[i]);
  }
}

FieldElement::FieldElement(bool one) : bytes_(kDataSize, 0) {
  if (one) {
    // NOTE: In our temporary, insecure implementation we use the first
    // 32 bits of |bytes_| to represent a non-negative integer in
    // hardware architecture byte order which we are here assuming is
    // little-endian.
    bytes_[0] = 1;
  }
}

FieldElement FieldElement::operator+(const FieldElement& other) const {
  // NOTE: In our temporary, insecure implementation we use the first
  // 32 bits of |bytes_| to represent a non-negative integer in
  // hardware architecture byte order.
  uint64_t this_number = AsInt32(bytes_);
  uint64_t other_number = AsInt32(other.bytes_);
  uint64_t sum = (this_number + other_number) % kLargestPrime;
  std::vector<byte> result;
  FromUInt64(sum, &result);
  return FieldElement(std::move(result));
}

void FieldElement::operator+=(const FieldElement& other) {
  uint64_t sum =
      (static_cast<uint64_t>(AsInt32(bytes_)) + static_cast<uint64_t>(AsInt32(other.bytes_))) %
      kLargestPrime;
  FromUInt64(sum, &bytes_);
}

FieldElement FieldElement::operator-(const FieldElement& other) const {
  uint64_t this_number = AsInt32(bytes_) + kLargestPrime;
  uint64_t other_number = AsInt32(other.bytes_);
  uint64_t difference = (this_number - other_number) % kLargestPrime;
  std::vector<byte> result;
  FromUInt64(difference, &result);
  return FieldElement(std::move(result));
}

void FieldElement::operator-=(const FieldElement& other) {
  uint64_t this_number = AsInt32(bytes_) + kLargestPrime;
  uint64_t other_number = AsInt32(other.bytes_);
  uint64_t difference = (this_number - other_number) % kLargestPrime;
  std::vector<byte> result;
  FromUInt64(difference, &bytes_);
}

FieldElement FieldElement::operator*(const FieldElement& other) const {
  uint64_t this_number = AsInt32(bytes_);
  uint64_t other_number = AsInt32(other.bytes_);
  uint64_t product = (this_number * other_number) % kLargestPrime;
  std::vector<byte> result;
  FromUInt64(product, &result);
  return FieldElement(std::move(result));
}

void FieldElement::operator*=(const FieldElement& other) {
  uint64_t this_number = AsInt32(bytes_);
  uint64_t other_number = AsInt32(other.bytes_);
  uint64_t product = (this_number * other_number) % kLargestPrime;
  std::vector<byte> result;
  FromUInt64(product, &bytes_);
}

FieldElement FieldElement::operator/(const FieldElement& other) const {
  uint64_t this_number = AsInt32(bytes_);
  uint64_t other_inverse = Inverse(AsInt32(other.bytes_));
  uint64_t quotient = (this_number * other_inverse) % kLargestPrime;
  std::vector<byte> result;
  FromUInt64(quotient, &result);
  return FieldElement(std::move(result));
}

void FieldElement::operator/=(const FieldElement& other) {
  uint64_t this_number = AsInt32(bytes_);
  uint64_t other_inverse = Inverse(AsInt32(other.bytes_));
  uint64_t quotient = (this_number * other_inverse) % kLargestPrime;
  std::vector<byte> result;
  FromUInt64(quotient, &bytes_);
}

const byte* FieldElement::KeyBytes() const { return bytes_.data(); }

std::ostream& operator<<(std::ostream& os, const FieldElement& el) {
  const byte* data = el.KeyBytes();
  for (size_t i = 0; i < FieldElement::kDataSize; i++) {
    os << std::hex << std::setfill('0') << std::setw(2) << static_cast<uint32_t>(*data) << " ";
    data++;
  }
  return os;
}

}  // namespace forculus
}  // namespace cobalt

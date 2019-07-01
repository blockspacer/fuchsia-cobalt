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

#ifndef COBALT_ALGORITHMS_FORCULUS_FIELD_ELEMENT_H_
#define COBALT_ALGORITHMS_FORCULUS_FIELD_ELEMENT_H_

#include <cstring>
#include <cwchar>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "util/crypto_util/types.h"

namespace cobalt {
namespace forculus {

using crypto::byte;

// A FieldElement is an element of the Forculus Field, the field over which
// Forculs encryption takes place.
class FieldElement {
 public:
  // The number of bytes of data used to represent a FieldElement. The size
  // of the Forculus Field is 2^{8 * kDataSize}.
  static const size_t kDataSize = 256 / 8;

  // Constructs a FieldElement by moving kDataSize bytes out of |bytes|.
  // If the length of |bytes| is greater than kDataSize than the extra bytes
  // will be discarded from the end. If the length of |bytes| is less than
  // kDataSize then zero bytes will be appendeed to the end.
  explicit FieldElement(std::vector<byte>&& bytes);

  // Constructs a FieldElement by copying kDataSize bytes out of the string
  // |data|. If the size of |data| is greater than kDataSize than the extra
  // bytes will be discarded from the end. If the size of |data| is less than
  // kDataSize then zero bytes will be appendeed to the end.
  explicit FieldElement(const std::string& data);

  // Constructs the FieldElement zero or one depending on the value of |one|.
  explicit FieldElement(bool one);

  // Move constructor.
  FieldElement(FieldElement&& other) : bytes_(std::move(other.bytes_)) {}

  // Copy constructor.
  FieldElement(const FieldElement& other) : bytes_(other.bytes_) {}

  // Copy assignment operator
  void operator=(const FieldElement& other) { bytes_ = other.bytes_; }

  // Move assignment operator
  void operator=(FieldElement&& other) { bytes_ = std::move(other.bytes_); }

  bool operator==(const FieldElement& other) const {
    return bytes_ == other.bytes_;
  }

  bool operator!=(const FieldElement& other) const {
    return bytes_ != other.bytes_;
  }

  // FieldElements are ordered lexicographically by their byte representation.
  // There is nothing mathematically natural about this ordering but having
  // some ordering is necessary in order to use FieldElements as the keys
  // of a map.
  bool operator<(const FieldElement& other) const {
    return bytes_ < other.bytes_;
  }

  // Convenience function that copies the underlying bytes of this element
  // into *target_string.
  void CopyBytesToString(std::string* target_string) {
    target_string->assign(reinterpret_cast<const char*>(bytes_.data()),
                          bytes_.size());
  }

  // Returns a pointer to a buffer of bytes of length
  // crypto::SymmetricCipher::KEY_SIZE that may be used as the key to a
  // symmetric cipher. The returned bytes are based on the underlying byte
  // representation of the FieldElement. Each FieldElement yields a different
  // key.
  const byte* KeyBytes() const;

  // Arithmetic operations below

  // Returns the sum of this element plus the |other| element.
  FieldElement operator+(const FieldElement& other) const;

  // Sets this element to the sum of this element and the |other| element.
  void operator+=(const FieldElement& other);

  // Returns the difference of this element minus the |other| element.
  FieldElement operator-(const FieldElement& other) const;

  // Sets this element to the difference of this element minus the |other|.
  void operator-=(const FieldElement& other);

  // Returns the product of this element times the |other| element.
  FieldElement operator*(const FieldElement& other) const;

  // Sets this element to the product of this element and the |other| element.
  void operator*=(const FieldElement& other);

  // Returns the quotient of this element divided by the |other| element.
  // The behavior is undefined if |other| is the zero element.
  FieldElement operator/(const FieldElement& other) const;

  // Sets this element to the quotent of this element divided by the |other|.
  // The behavior is undefined if |other| is the zero element.
  void operator/=(const FieldElement& other);

 private:
  std::vector<byte> bytes_;
};

std::ostream& operator<<(std::ostream& os, const FieldElement& el);

}  // namespace forculus
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_FORCULUS_FIELD_ELEMENT_H_

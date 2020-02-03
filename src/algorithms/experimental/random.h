// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_ALGORITHMS_EXPERIMENTAL_RANDOM_H_
#define COBALT_SRC_ALGORITHMS_EXPERIMENTAL_RANDOM_H_

#include <cstdint>
#include <random>
#include <type_traits>

namespace cobalt {

// A class template for an interface to a source of random instances of an unsigned integer type
// |T|. On Fuchsia this can be implemented by a class which draws from the Zircon CPRNG.
//
// Classes generated from this template satisfy the requirements of the UniformRandomBitGenerator
// C++ concept.
template <typename T>
class BitGeneratorInterface {
 public:
  static_assert(std::is_unsigned<T>::value,
                "BitGeneratorInterface is only valid for unsigned integer types");
  using result_type = T;

  BitGeneratorInterface() = default;
  virtual ~BitGeneratorInterface() = 0;
  static constexpr result_type min() { return std::numeric_limits<result_type>::min(); }
  static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }
  virtual result_type operator()() = 0;
};

template <typename T>
BitGeneratorInterface<T>::~BitGeneratorInterface() = default;

// A placeholder implementation of BitGeneratorInterface.
class RandomNumberGenerator : public BitGeneratorInterface<uint32_t> {
 public:
  RandomNumberGenerator() { engine_ = std::mt19937(rd_()); };
  explicit RandomNumberGenerator(uint32_t seed) { engine_ = std::mt19937(seed); };
  result_type operator()() override { return engine_(); }

 private:
  std::random_device rd_;
  std::mt19937 engine_;
};

// A fake random number generator that always returns |val|.
class FakeRandomNumberGenerator : public BitGeneratorInterface<uint32_t> {
 public:
  explicit FakeRandomNumberGenerator(uint32_t val) : val_(val) {}
  result_type operator()() override { return val_; }

 private:
  uint32_t val_;
};

}  // namespace cobalt

#endif  // COBALT_SRC_ALGORITHMS_EXPERIMENTAL_RANDOM_H_

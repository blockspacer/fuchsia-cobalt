// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_UTIL_CLOCK_H_
#define COBALT_UTIL_CLOCK_H_

#include <chrono>

namespace cobalt {
namespace util {

// Allows us to mock out a clock for tests.
class ClockInterface {
 public:
  virtual ~ClockInterface() = default;

  virtual std::chrono::system_clock::time_point now() = 0;
};

// A clock that returns the real system time.
class SystemClock : public ClockInterface {
 public:
  std::chrono::system_clock::time_point now() override {
    return std::chrono::system_clock::now();
  }
};

// A clock that returns an incrementing sequence of tics each time it is called.
// Optionally a callback may be set that will be invoked each time the
// clock ticks.
class IncrementingClock : public ClockInterface {
 public:
  std::chrono::system_clock::time_point now() override {
    time_ += increment_;
    if (callback_) {
      callback_(time_);
    }
    return time_;
  }

  // Return the current value of time_ without advancing time.
  std::chrono::system_clock::time_point peek_now() { return time_; }

  void set_increment(std::chrono::system_clock::duration increment) {
    increment_ = increment;
  }

  void set_time(std::chrono::system_clock::time_point t) { time_ = t; }

  void set_callback(
      std::function<void(std::chrono::system_clock::time_point)> c) {
    callback_ = c;
  }

 private:
  std::chrono::system_clock::time_point time_ =
      std::chrono::system_clock::time_point(
          std::chrono::system_clock::duration(0));
  std::chrono::system_clock::duration increment_ =
      std::chrono::system_clock::duration(1);
  std::function<void(std::chrono::system_clock::time_point)> callback_;
};

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_UTIL_CLOCK_H_

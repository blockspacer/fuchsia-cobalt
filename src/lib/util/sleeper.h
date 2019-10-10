// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_UTIL_SLEEPER_H_
#define COBALT_SRC_LIB_UTIL_SLEEPER_H_

#include <chrono>
#include <string>
#include <thread>

#include "src/lib/util/clock.h"

namespace cobalt {
namespace util {

// Allows us to mock out sleeping for tests.
class SleeperInterface {
 public:
  virtual ~SleeperInterface() = default;

  virtual void sleep_for(const std::chrono::milliseconds& sleep_duration) = 0;
};

// Implements SleeperInterface by invoking the real std::this_thread::sleep_for
// method.
class Sleeper : public SleeperInterface {
 public:
  Sleeper() = default;

  void sleep_for(const std::chrono::milliseconds& sleep_duration) override {
    std::this_thread::sleep_for(sleep_duration);
  }
};

// A fake implementation of SleeperInterface that does not actually sleep. It
// records the length of the most recent invocation of sleep_for. Optionally
// it can invoke a user-supplied callback and it can update a fake clock.
class FakeSleeper : public SleeperInterface {
 public:
  FakeSleeper() = default;

  // If a callback is set, it will be invoked each time sleep_for is invoked.
  void set_callback(std::function<void(const std::chrono::milliseconds&)> c) {
    callback_ = std::move(c);
  }

  // If a clock is set, its increment_by() method will be invoked each time
  // sleep_for() is invoked. In this way, in a test where both sleeping and the
  // clock are fakes, the fake clock will be advanced by the fake sleep time.
  void set_incrementing_steady_clock(cobalt::util::IncrementingSteadyClock* clock) {
    incrementing_steady_clock_ = clock;
  }

  void sleep_for(const std::chrono::milliseconds& sleep_duration) override {
    last_sleep_duration_ = sleep_duration;
    if (callback_) {
      callback_(sleep_duration);
    }
    if (incrementing_steady_clock_) {
      incrementing_steady_clock_->increment_by(sleep_duration);
    }
  }

  std::chrono::milliseconds last_sleep_duration() { return last_sleep_duration_; }

  void Reset() { last_sleep_duration_ = std::chrono::milliseconds(0); }

 private:
  std::chrono::milliseconds last_sleep_duration_;
  std::function<void(const std::chrono::milliseconds&)> callback_ = nullptr;
  cobalt::util::IncrementingSteadyClock* incrementing_steady_clock_ = nullptr;
};

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_SRC_LIB_UTIL_SLEEPER_H_

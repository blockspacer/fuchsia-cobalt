// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_UTIL_CLOCK_H_
#define COBALT_SRC_LIB_UTIL_CLOCK_H_

#include <chrono>
#include <iomanip>
#include <optional>
#include <queue>
#include <sstream>
#include <string>

namespace cobalt {
namespace util {

// Allows us to mock out a clock for tests.
class SystemClockInterface {
 public:
  virtual ~SystemClockInterface() = default;

  virtual std::chrono::system_clock::time_point now() = 0;
};

// A clock that returns the real system time.
class SystemClock : public SystemClockInterface {
 public:
  std::chrono::system_clock::time_point now() override { return std::chrono::system_clock::now(); }
};

// Allows us to mock out a clock for tests.
class SteadyClockInterface {
 public:
  virtual ~SteadyClockInterface() = default;

  virtual std::chrono::steady_clock::time_point now() = 0;
};

// A clock that returns the real system time.
class SteadyClock : public SteadyClockInterface {
 public:
  std::chrono::steady_clock::time_point now() override { return std::chrono::steady_clock::now(); }
};

// A system clock that returns an incrementing sequence of tics each time it is
// called. Optionally a callback may be set that will be invoked each time the
// clock ticks. Intended for use in tests only.
class IncrementingSystemClock : public SystemClockInterface {
 public:
  IncrementingSystemClock() = default;
  // Constructs an IncrementingSystemClock which increments by |increment|
  // seconds each time it is called.
  explicit IncrementingSystemClock(std::chrono::system_clock::duration increment)
      : increment_(increment) {}

  std::chrono::system_clock::time_point now() override {
    time_ += increment_;
    if (callback_) {
      callback_(time_);
    }
    return time_;
  }

  // Return the current value of time_ without advancing time.
  std::chrono::system_clock::time_point peek_now() { return time_; }

  // Set the value by which the clock is incremented each time it is called.
  void set_increment(std::chrono::system_clock::duration increment) { increment_ = increment; }

  // Increment the clock's current time once.
  void increment_by(std::chrono::system_clock::duration increment) {
    time_ += increment;
    if (callback_) {
      callback_(time_);
    }
  }

  void set_time(std::chrono::system_clock::time_point t) { time_ = t; }

  void set_callback(std::function<void(std::chrono::system_clock::time_point)> c) {
    callback_ = std::move(c);
  }

  std::string DebugString() {
    std::ostringstream stream;
    std::time_t now = std::chrono::system_clock::to_time_t(time_);
    stream << "time: " << std::put_time(std::localtime(&now), "%F %T") << "\n";
    return stream.str();
  }

 private:
  std::chrono::system_clock::time_point time_ =
      std::chrono::system_clock::time_point(std::chrono::system_clock::duration(0));
  std::chrono::system_clock::duration increment_ = std::chrono::system_clock::duration(1);
  std::function<void(std::chrono::system_clock::time_point)> callback_;
};

// An abstract interface to a clock that refuses to provide the time if a quality condition is not
// satisfied; for example, if the clock has not been initialized from a trustworthy source.
class ValidatedClockInterface {
 public:
  virtual ~ValidatedClockInterface() = default;

  // Returns the current time if an accurate clock is avaliable or nullopt otherwise.
  virtual std::optional<std::chrono::system_clock::time_point> now() = 0;
};

// A clock that returns an incrementing sequence of tics each time it is called.
// Optionally a callback may be set that will be invoked each time the
// clock ticks.
class IncrementingSteadyClock : public SteadyClockInterface {
 public:
  IncrementingSteadyClock() = default;
  // Constructs an IncrementingClock which increments by |increment| seconds
  // each time it is called.
  explicit IncrementingSteadyClock(std::chrono::steady_clock::duration increment)
      : increment_(increment) {}

  std::chrono::steady_clock::time_point now() override {
    time_ += increment_;
    if (callback_) {
      callback_(time_);
    }
    return time_;
  }

  // Returns the current value of time_ without advancing time.
  std::chrono::steady_clock::time_point peek_now() { return time_; }

  // Returns the value that now() would return without advancing time. This is
  // different than peek_now() which returns instead the last value that now()
  // would have already returned that previous time it was invoked.
  std::chrono::steady_clock::time_point peek_later() { return time_ + increment_; }

  // Set the value by which the clock is incremented each time it is called.
  void set_increment(std::chrono::steady_clock::duration increment) { increment_ = increment; }

  // Increment the clock's current time once.
  void increment_by(std::chrono::steady_clock::duration increment) {
    time_ += increment;
    if (callback_) {
      callback_(time_);
    }
  }

  void set_time(std::chrono::steady_clock::time_point t) { time_ = t; }

  void set_callback(std::function<void(std::chrono::steady_clock::time_point)> c) {
    callback_ = std::move(c);
  }

  std::string DebugString() {
    std::ostringstream stream;
    stream << "elapsed time: "
           << std::chrono::duration_cast<std::chrono::seconds>(time_.time_since_epoch()).count()
           << " seconds\n";
    return stream.str();
  }

 private:
  std::chrono::steady_clock::time_point time_ =
      std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(0));
  std::chrono::steady_clock::duration increment_ = std::chrono::steady_clock::duration(1);
  std::function<void(std::chrono::steady_clock::time_point)> callback_;
};

// A wrapper for a SystemClockInterface that is always accurate.
class AlwaysAccurateClock : public util::ValidatedClockInterface {
 public:
  explicit AlwaysAccurateClock(std::unique_ptr<SystemClockInterface> system_clock)
      : system_clock_(std::move(system_clock)) {}

  std::optional<std::chrono::system_clock::time_point> now() override {
    return system_clock_->now();
  }

 private:
  std::unique_ptr<SystemClockInterface> system_clock_;
};

// A wrapper for a SystemClockInterface that allows a test to control whether the clock is accurate.
class FakeValidatedClock : public util::ValidatedClockInterface {
 public:
  explicit FakeValidatedClock(SystemClockInterface* incrementing_clock)
      : incrementing_clock_(incrementing_clock) {}

  std::optional<std::chrono::system_clock::time_point> now() override {
    bool accurate = accurate_sequence_.front();
    if (accurate_sequence_.size() > 1) {
      accurate_sequence_.pop_front();
    }
    if (accurate) {
      return incrementing_clock_->now();
    }
    return std::nullopt;
  }

  void SetAccurate(bool accurate) { accurate_sequence_ = std::deque<bool>({accurate}); }

  // Set the sequence of accurate/inaccurate responses this clock should return.
  // If more calls are made than are in this sequence, the last item in the sequence continues to be
  // returned.
  void SetAccurate(const std::vector<bool>& accurate_sequence) {
    accurate_sequence_ = std::deque<bool>(accurate_sequence.begin(), accurate_sequence.end());
  }

 private:
  SystemClockInterface* incrementing_clock_;
  std::deque<bool> accurate_sequence_{false};
};

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_SRC_LIB_UTIL_CLOCK_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_UTIL_PROTECTED_FIELDS_H_
#define COBALT_UTIL_PROTECTED_FIELDS_H_

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace cobalt {
namespace util {

// ProtectedFields is a useful abstraction for having an object that is
// protected by a mutex.
//
// Example usage:
//
// struct SafeField {
//   int protected_int;
// };
// ProtectedFields<SafeField> protected_fields;
// protected_fields.lock()->protected_int = 100;
// LOG(INFO) << "Current protected_int: " <<
//              protected_fields.const_lock()->protected_int;
//
template <class Fields>
class ProtectedFields {
 public:
  // ConstLockedFieldsPtr holds a pointer to Fields, as well a
  // unique_lock<mutex>.
  //
  // The semantics of this object is similar to a pointer.
  class LockedFieldsPtr {
   public:
    Fields* operator->() { return fields_; }
    Fields& operator*() { return *fields_; }

    // Calls the wait() method of a condition variable with a reference to the
    // mutex.
    void wait_with(std::condition_variable* cv) { return cv->wait(lock_); }

    template <class Predicate>
    void wait_with(std::condition_variable* cv, Predicate pred) {
      return cv->wait(lock_, pred);
    }

    // Calls the wait_for() method of a condition variable with a reference to
    // the mutex.
    template <class Rep, class Period>
    std::cv_status wait_for_with(std::condition_variable* cv,
                                 const std::chrono::duration<Rep, Period>& rel_time) {
      return cv->wait(lock_, rel_time);
    }

    template <class Rep, class Period, class Predicate>
    bool wait_for_with(std::condition_variable* cv,
                       const std::chrono::duration<Rep, Period>& rel_time, Predicate pred) {
      return cv->wait_for(lock_, rel_time, pred);
    }

    // Calls the wait_until() method of a condition variable with a reference to
    // the mutex.
    template <class Clock, class Duration>
    std::cv_status wait_until_with(std::condition_variable* cv,
                                   const std::chrono::time_point<Clock, Duration>& timeout_time) {
      return cv->wait_until(lock_, timeout_time);
    }

    template <class Clock, class Duration, class Predicate>
    bool wait_until_with(std::condition_variable* cv,
                         const std::chrono::time_point<Clock, Duration>& timeout_time,
                         Predicate pred) {
      return cv->wait_until(lock_, timeout_time, pred);
    }

   private:
    friend class ProtectedFields;
    LockedFieldsPtr(std::mutex* mutex, Fields* fields) : lock_(*mutex), fields_(fields) {}

    std::unique_lock<std::mutex> lock_;
    Fields* fields_;

   public:
    // Disable copy/assign. Only allow move.
    LockedFieldsPtr(LockedFieldsPtr&&) noexcept;
    LockedFieldsPtr& operator=(LockedFieldsPtr&&) noexcept;
    LockedFieldsPtr& operator=(const LockedFieldsPtr&) = delete;
    LockedFieldsPtr(const LockedFieldsPtr&) = delete;
  };

  // ConstLockedFieldsPtr holds a const pointer to Fields, as well a
  // unique_lock<mutex>.
  //
  // The semantics of this object is similar to a const pointer.
  class ConstLockedFieldsPtr {
   public:
    const Fields* operator->() { return fields_; }
    const Fields& operator*() { return *fields_; }

    // Calls the wait() method of a condition variable with a reference to the
    // mutex.
    void wait_with(std::condition_variable* cv) { return cv->wait(lock_); }

    template <class Predicate>
    void wait_with(std::condition_variable* cv, Predicate pred) {
      return cv->wait(lock_, pred);
    }

    // Calls the wait_for() method of a condition variable with a reference to
    // the mutex.
    template <class Rep, class Period>
    std::cv_status wait_for_with(std::condition_variable* cv,
                                 const std::chrono::duration<Rep, Period>& rel_time) {
      return cv->wait(lock_, rel_time);
    }

    template <class Rep, class Period, class Predicate>
    bool wait_for_with(std::condition_variable* cv,
                       const std::chrono::duration<Rep, Period>& rel_time, Predicate pred) {
      return cv->wait_for(lock_, rel_time, pred);
    }

    // Calls the wait_until() method of a condition variable with a reference to
    // the mutex.
    template <class Clock, class Duration>
    std::cv_status wait_until_with(std::condition_variable* cv,
                                   const std::chrono::time_point<Clock, Duration>& timeout_time) {
      return cv->wait_until(lock_, timeout_time);
    }

    template <class Clock, class Duration, class Predicate>
    bool wait_until_with(std::condition_variable* cv,
                         const std::chrono::time_point<Clock, Duration>& timeout_time,
                         Predicate pred) {
      return cv->wait_until(lock_, timeout_time, pred);
    }

   private:
    friend class ProtectedFields;
    ConstLockedFieldsPtr(std::mutex* mutex, const Fields* fields)
        : lock_(*mutex), fields_(fields) {}

    std::unique_lock<std::mutex> lock_;
    const Fields* fields_;

   public:
    // Disable copy/assign. Only allow move.
    ConstLockedFieldsPtr(ConstLockedFieldsPtr&&) noexcept;
    ConstLockedFieldsPtr& operator=(ConstLockedFieldsPtr&&) noexcept;
    ConstLockedFieldsPtr& operator=(const ConstLockedFieldsPtr&) = delete;
    ConstLockedFieldsPtr(const ConstLockedFieldsPtr&) = delete;
  };

  LockedFieldsPtr lock() { return LockedFieldsPtr(&mutex_, &fields_); }
  ConstLockedFieldsPtr const_lock() const { return ConstLockedFieldsPtr(&mutex_, &fields_); }

 private:
  mutable std::mutex mutex_;
  Fields fields_;

 public:
  ProtectedFields& operator=(const ProtectedFields&) = delete;
  ProtectedFields(const ProtectedFields&) = delete;
  ProtectedFields() = default;
};

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_UTIL_PROTECTED_FIELDS_H_

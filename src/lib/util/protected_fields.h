// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_UTIL_PROTECTED_FIELDS_H_
#define COBALT_SRC_LIB_UTIL_PROTECTED_FIELDS_H_

#include <mutex>
#include <shared_mutex>

// ProtectedFields is a useful abstraction for having a set of fields that are protected by a mutex.
// Using protected fields instead of a plain mutex has several advantages including:
//
//   1. The only way to access the protected fields is by locking the mutex, thereby ensuring that
//      any places that read/write to the fields are safe. (This increases confidence in code
//      correctness for the author, and reduces the burden on the code reviewer)
//   2. The mutex locks use RAII semantics, and as such the fields can be locked, accessed, and
//      re-locked in a single line.
//   3. The ProtectedFields can be locked mutably (using lock()) or immutably (using const_lock())
//      which allows reading protected fields in const functions (without needing to declare your
//      ProtectedFields as mutable.
//
// Simple example usage:
//
//  struct SafeField {
//    int protected_int;
//  };
//  ProtectedFields<SafeField> protected_fields;
//  protected_fields.lock()->protected_int = 100;
//  LOG(INFO) << "Current protected_int: " <<
//               protected_fields.const_lock()->protected_int;
//
// Example that holds the lock for multiple lines:
//
//  struct SafeFields {
//    int protected_int;
//    char protected_char;
//  };
//  ProtectedFields<SafeField> protected_fields;
//  auto fields = protected_fields.lock();
//  fields->protected_int = 100;
//  fields->protected_char = 'a';
//
// Example of taking locked fields as an argument:
//
//  struct SafeFields {
//    int protected_int;
//    char protected_char;
//  };
//  void SetFieldsLocked(ProtectedFields<SafeFields>::LockedFieldsPtr *fields) {
//   auto &f = *fields;
//   f->protected_int = 100;
//   f->protected_char = 'a';
//  }
//
// Example using RWProtectedFields:
//
//  RWProtectedFields<int> rw_protected_int(10);
//  auto reader_lock = rw_protected_int.const_lock();
//  std::thread([&rw_protected_int]() {
//    // This works, since any number of reader locks can be taken at once.
//    rw_protected_int.const_lock();
//  }).join();
//
// Example using LockedFieldsPtr with a condition variable:
//
//  struct SafeFields {
//    std::condition_variable_any shutdown_notifier;
//    bool shutdown;
//  }
//  ProtectedFields<SafeFields> protected_fields;
//  auto fields = protected_fields.lock();
//  // Wait until shutdown is true.
//  fields->shutdown_notifier.wait(fields, [&fields]() { return fields->shutdown; });

namespace cobalt::util {

// BaseProtectedFields is a template class and is specialized into ProtectedFields and
// RWProtectedFields. (See below).
//
// Template types:
// |Fields| can be any type, and is the object that will be protected.
// |Mutex| is any type that implements the *Mutex* named requirement. (See:
//         https://en.cppreference.com/w/cpp/named_req/Mutex)
// |ConstLock| is the lock type used for const_lock(). E.g. for ProtectedFields, it will be
//             std::unique_lock<std::mutex>, and for RWProtectedFields it will be
//             std::shared_lock<std::shared_mutex>.
template <class Fields, class Mutex, class ConstLock>
class BaseProtectedFields {
 public:
  // BaseLockedFieldsPtr holds a pointer to Fields, as well a
  // unique_lock<mutex>.
  //
  // Variables of this type are accessed using pointer syntax.
  //
  // Implements:
  // - *BasicLockable*: This allows ProtectedFields objects to be used as the lock value in the wait
  //                    methods of std::condition_variable_any. (See:
  //                    https://en.cppreference.com/w/cpp/named_req/BasicLockable)
  //
  //                    N.B. std::condition_variable may be more performant in some cases, and may
  //                    be preferred in performance critical situations. In those cases, raw
  //                    std::unique_lock<std::mutex> should be used.
  //
  // Template types:
  // |InnerFields| is either the same as Fields above, or in the case of a ConstLockedFieldsPtr, it
  //               is `const Fields`.
  // |LockType| The lock type to hold. (e.g. std::unique_lock<std::mutex>)
  template <class InnerFields, class LockType>
  class BaseLockedFieldsPtr {
   public:
    InnerFields* operator->() { return fields_; }
    InnerFields& operator*() { return *fields_; }

    // Blocks until a lock can be obtained for the current execution agent (thread, process, task).
    // If an exception is thrown, no lock is obtained. Additionally, after the lock is obtained, the
    // inner fields become available again.
    void lock() {
      lock_.lock();
      // Make the inner fields_ accessible again.
      std::swap(fields_, hidden_fields_);
    }

    // Releases the lock held by the execution agent. Throws no exceptions. Additionally, after the
    // lock is released, the inner fields are no longer accessible.
    //
    // Requires: The current execution agent should hold the lock.
    void unlock() {
      // Make the inner fields_ inaccessible. When the lock is not held, accessing the fields using
      // * or -> is undefined behavior.
      std::swap(fields_, hidden_fields_);
      lock_.unlock();
    }

   private:
    friend class BaseProtectedFields;
    BaseLockedFieldsPtr(Mutex* mutex, InnerFields* fields) : lock_(*mutex), fields_(fields) {}

    LockType lock_;
    InnerFields* fields_;
    InnerFields* hidden_fields_;

   public:
    // Disable copy/assign. Only allow move.
    BaseLockedFieldsPtr(BaseLockedFieldsPtr&&) noexcept = default;
    BaseLockedFieldsPtr& operator=(BaseLockedFieldsPtr&&) noexcept = default;
    BaseLockedFieldsPtr& operator=(const BaseLockedFieldsPtr&) = delete;
    BaseLockedFieldsPtr(const BaseLockedFieldsPtr&) = delete;
  };

  // Alias LockedFieldsPtrImpl for const and non-const variants.
  using LockedFieldsPtr = BaseLockedFieldsPtr<Fields, std::unique_lock<Mutex>>;
  using ConstLockedFieldsPtr = BaseLockedFieldsPtr<const Fields, ConstLock>;

  LockedFieldsPtr lock() { return LockedFieldsPtr(&mutex_, &fields_); }
  ConstLockedFieldsPtr const_lock() const { return ConstLockedFieldsPtr(&mutex_, &fields_); }

 private:
  mutable Mutex mutex_;
  Fields fields_;

 public:
  explicit BaseProtectedFields(Fields initial_fields) : fields_(initial_fields) {}

  BaseProtectedFields& operator=(const BaseProtectedFields&) = delete;
  BaseProtectedFields(const BaseProtectedFields&) = delete;
  BaseProtectedFields() = default;
};

// The default implementation of ProtectedFields. Uses standard mutexes.
template <class Fields>
using ProtectedFields = BaseProtectedFields<Fields, std::mutex, std::unique_lock<std::mutex>>;

// A ProtectedFields implementation that supports Reader/Writer semantics. (const_lock is a reader
// lock, lock is a writer lock)
template <class Fields>
using RWProtectedFields =
    BaseProtectedFields<Fields, std::shared_mutex, std::shared_lock<std::shared_mutex>>;

}  // namespace cobalt::util

#endif  // COBALT_SRC_LIB_UTIL_PROTECTED_FIELDS_H_

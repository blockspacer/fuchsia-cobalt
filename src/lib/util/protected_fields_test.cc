// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/util/protected_fields.h"

#include <condition_variable>
#include <sstream>
#include <string>
#include <thread>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::util {

TEST(ProtectedFields, BasicFunctionality) {
  struct SafeField {
    int protected_int;
  };
  ProtectedFields<SafeField> protected_fields;
  protected_fields.lock()->protected_int = 100;
  ASSERT_EQ(100, protected_fields.const_lock()->protected_int);
}

TEST(ProtectedFields, StressTest) {
  std::vector<std::thread> threads;
  struct SafeFields {
    int protected_int = 0;
    std::string protected_str = "";
  };
  ProtectedFields<SafeFields> protected_fields;

  // Force lock contention.
  auto locked_fields = protected_fields.const_lock();
  for (auto i = 0; i < 100; i += 1) {
    threads.emplace_back(std::thread([i, &protected_fields]() {
      auto fields = protected_fields.lock();
      fields->protected_int += 10;
      std::stringstream ss;
      ss << "Thread: " << i;
      fields->protected_str = ss.str();
    }));
  }

  // Nothing should have changed yet, since the lock has been held since before the threads started.
  ASSERT_EQ(0, locked_fields->protected_int);
  ASSERT_EQ("", locked_fields->protected_str);
  locked_fields.unlock();

  for (auto &thread : threads) {
    thread.join();
  }

  ASSERT_EQ(100 * 10, protected_fields.const_lock()->protected_int);
  ASSERT_NE("", protected_fields.const_lock()->protected_str);
}

TEST(ProtectedFields, ConditionVariables) {
  struct SafeFields {
    std::condition_variable_any started_notifier;
    bool started = false;

    std::condition_variable_any shutdown_notifier;
    bool shutdown = false;
    bool shutdown_complete = false;
  };
  ProtectedFields<SafeFields> protected_fields;

  auto important_work = std::thread([&protected_fields]() {
    auto fields = protected_fields.lock();
    fields->started = true;
    fields->started_notifier.notify_all();
    fields->shutdown_notifier.wait(fields, [&fields]() { return fields->shutdown; });
    fields->shutdown_complete = true;
  });

  {
    // Wait until the thread has started.
    auto fields = protected_fields.lock();
    fields->started_notifier.wait(fields, [&fields]() { return fields->started; });
  }

  {
    // Notify the thread to shut down.
    auto fields = protected_fields.lock();
    fields->shutdown = true;
    fields->shutdown_notifier.notify_all();
  }

  important_work.join();

  ASSERT_EQ(true, protected_fields.const_lock()->shutdown_complete);
}

TEST(ProtectedFields, Constructor) {
  struct ConstructableFields {
    uint32_t value = 1000;
    std::string other_value;
    int32_t num;

    ConstructableFields(std::string v, int32_t num) : other_value(std::move(v)), num(num) {}
  };

  ProtectedFields<ConstructableFields> protected_fields("Constructed Value", 54321);

  ASSERT_EQ(1000u, protected_fields.lock()->value);
  ASSERT_EQ("Constructed Value", protected_fields.lock()->other_value);
  ASSERT_EQ(54321, protected_fields.lock()->num);
}

TEST(RWProtectedFields, StressTest) {
  std::vector<std::thread> threads;
  struct SafeField {
    int value = 0;
  };
  RWProtectedFields<SafeField> rw_protected_field;
  rw_protected_field.lock()->value = 15;

  ProtectedFields<int> counter(0);

  // Force lock contention;
  auto locked_counter = counter.lock();

  // Take the reader lock, the threads should be able to continue.
  auto locked_fields = rw_protected_field.const_lock();

  for (auto i = 0; i < 100; i += 1) {
    threads.emplace_back(std::thread([&counter, &rw_protected_field]() {
      auto fields = rw_protected_field.const_lock();
      *counter.lock() += fields->value;
    }));
  }

  locked_counter.unlock();

  for (auto &thread : threads) {
    thread.join();
  }

  ASSERT_EQ(15 * 100, *counter.lock());
}

}  // namespace cobalt::util

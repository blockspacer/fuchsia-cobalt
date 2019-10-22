// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_STATUSOR_STATUS_MACROS_H_
#define COBALT_SRC_LIB_STATUSOR_STATUS_MACROS_H_

#include <memory>
#include <ostream>  // NOLINT
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "src/lib/statusor/statusor.h"

namespace cobalt::lib::statusor::status_macros {

using cobalt::util::Status;

// Stream object used to collect error messages in MAKE_ERROR macros
// or append error messages with APPEND_ERROR.  It accepts any
// arguments with operator<< to build an error string, and then has an
// implicit cast operator to Status, which converts the
// logged string to a Status object and returns it, after logging the
// error.  At least one call to operator<< is required; a compile time
// error will be generated if none are given. Errors will only be
// logged by default for certain status codes, as defined in
// IsLoggedByDefault. This class will give ERROR errors if you don't
// retrieve a Status exactly once before destruction.
//
// The class converts into an intermediate wrapper object
// MakeErrorStreamWithOutput to check that the error stream gets at least one
// item of input.
class MakeErrorStream {
 public:
  // Wrapper around MakeErrorStream that only allows for output. This
  // is created as output of the first operator<< call on
  // MakeErrorStream. The bare MakeErrorStream does not have a
  // Status operator. The net effect of that is that you
  // have to call operator<< at least once or else you'll get a
  // compile time error.
  class MakeErrorStreamWithOutput {
   public:
    explicit MakeErrorStreamWithOutput(MakeErrorStream* error_stream)
        : wrapped_error_stream_(error_stream) {}

    template <typename T>
    MakeErrorStreamWithOutput& operator<<(const T& value) {
      *wrapped_error_stream_ << value;
      return *this;
    }

    // Implicit cast operators to Status and StatusOr.
    // Exactly one of these must be called exactly once before destruction.
    operator Status() {  // NOLINT(google-explicit-constructor)
      return wrapped_error_stream_->GetStatus();
    }
    template <typename T>
    operator StatusOr<T>() {  // NOLINT(google-explicit-constructor)
      return wrapped_error_stream_->GetStatus();
    }

   private:
    MakeErrorStream* wrapped_error_stream_;
  };

  // When starting from an existing error status, this determines whether we'll
  // append or prepend to that status's error message.
  enum PriorMessageHandling { kAppendToPriorMessage, kPrependToPriorMessage };

  // Make an error with the given code.
  template <typename ERROR_CODE_TYPE>
  MakeErrorStream(const char* file, int line, ERROR_CODE_TYPE code)
      : impl_(new Impl(file, line, code, this, true)) {}

  template <typename T>
  MakeErrorStreamWithOutput& operator<<(const T& value) {
    CheckNotDone();
    impl_->stream_ << value;
    return impl_->make_error_stream_with_output_wrapper_;
  }

  // When this message is logged (see with_logging()), include the stack trace.
  MakeErrorStream& with_log_stack_trace() {
    impl_->should_log_stack_trace_ = true;
    return *this;
  }

  // Adds RET_CHECK failure text to error message.
  MakeErrorStreamWithOutput& add_ret_check_failure(const char* condition) {
    return *this << "RET_CHECK failure (" << impl_->file_ << ":" << impl_->line_ << ") "
                 << condition << " ";
  }

 private:
  class Impl {
   public:
    Impl(const char* file, int line, cobalt::util::StatusCode code, MakeErrorStream* error_stream,
         bool is_logged_by_default = true);
    Impl(const Status& status, PriorMessageHandling prior_message_handling, const char* file,
         int line, MakeErrorStream* error_stream);

    ~Impl();

    // This must be called exactly once before destruction.
    Status GetStatus();

    void CheckNotDone() const;

   private:
    const char* file_;
    int line_;
    cobalt::util::StatusCode code_;

    PriorMessageHandling prior_message_handling_ = kAppendToPriorMessage;
    std::string prior_message_;
    bool is_done_;  // true after Status object has been returned
    std::ostringstream stream_;
    bool should_log_;
    int log_severity_;
    bool should_log_stack_trace_;

    // Wrapper around the MakeErrorStream object that has a
    // Status conversion. The first << operator called on
    // MakeErrorStream will return this object, and only this object
    // can implicitly convert to Status. The net effect of
    // this is that you'll get a compile time error if you call
    // MAKE_ERROR etc. without adding any output.
    MakeErrorStreamWithOutput make_error_stream_with_output_wrapper_;

    friend class MakeErrorStream;
  };

  void CheckNotDone() const;

  // Returns the status. Used by MakeErrorStreamWithOutput.
  [[nodiscard]] Status GetStatus() const { return impl_->GetStatus(); }

  // Store the actual data on the heap to reduce stack frame sizes.
  std::unique_ptr<Impl> impl_;
};

// Provides a conversion to bool so that it can be used inside an if statement
// that declares a variable.
class StatusAdaptorForMacros {
 public:
  explicit StatusAdaptorForMacros(Status status) : status_(std::move(status)) {}

  StatusAdaptorForMacros(const StatusAdaptorForMacros&) = delete;
  StatusAdaptorForMacros& operator=(const StatusAdaptorForMacros&) = delete;

  explicit operator bool() const { return status_.ok(); }

  Status&& Consume() { return std::move(status_); }

 private:
  Status status_;
};

}  // namespace cobalt::lib::statusor::status_macros

#define CB_RET_CHECK(condition)                                                                \
  while (!(condition))                                                                         \
  return util::status_macros::MakeErrorStream(__FILE__, __LINE__, tensorflow::error::INTERNAL) \
      .with_log_stack_trace()                                                                  \
      .add_ret_check_failure(#condition)

#define CB_ASSERT_OK_AND_ASSIGN(lhs, rexpr)                                                      \
  CB_ASSERT_OK_AND_ASSIGN_IMPL(CB_STATUS_MACROS_CONCAT_NAME(_status_or_value, __COUNTER__), lhs, \
                               rexpr);

#define CB_ASSERT_OK_AND_ASSIGN_IMPL(statusor, lhs, rexpr)               \
  auto statusor = (rexpr);                                  /* NOLINT */ \
  ASSERT_TRUE(statusor.status().ok()) << statusor.status(); /* NOLINT */ \
  lhs = std::move(statusor.ValueOrDie())                    /* NOLINT */

#define CB_STATUS_MACROS_CONCAT_NAME(x, y) CB_STATUS_MACROS_CONCAT_IMPL(x, y)
#define CB_STATUS_MACROS_CONCAT_IMPL(x, y) x##y

#define CB_ASSIGN_OR_RETURN(lhs, rexpr) \
  CB_ASSIGN_OR_RETURN_IMPL(CB_STATUS_MACROS_CONCAT_NAME(_status_or_value, __COUNTER__), lhs, rexpr)

#define CB_ASSIGN_OR_RETURN_IMPL(statusor, lhs, rexpr) \
  auto statusor = (rexpr);    /* NOLINT */             \
  if (!statusor.ok()) {       /* NOLINT */             \
    return statusor.status(); /* NOLINT */             \
  }                                                    \
  lhs = std::move(statusor.ValueOrDie()) /* NOLINT */

#endif  // COBALT_SRC_LIB_STATUSOR_STATUS_MACROS_H_

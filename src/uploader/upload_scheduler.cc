// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/uploader/upload_scheduler.h"

namespace cobalt::encoder {

// Definition of the static constant declared in shipping_manager.h.
// This must be less than 2^31. There appears to be a bug in
// std::condition_variable::wait_for() in which setting the wait time to
// std::chrono::seconds::max() effectively sets the wait time to zero.
constexpr std::chrono::seconds UploadScheduler::kMaxSeconds(999999999);

UploadScheduler::UploadScheduler(std::chrono::seconds target_interval,
                                 std::chrono::seconds min_interval,
                                 std::chrono::seconds initial_interval)
    : current_interval_(initial_interval),
      target_interval_(target_interval),
      min_interval_(min_interval) {
  CHECK_GE(min_interval.count(), 0);
  CHECK_LE(current_interval_.count(), target_interval_.count());
  CHECK_LE(min_interval_.count(), target_interval_.count());
  CHECK_LE(target_interval.count(), kMaxSeconds.count());
}

UploadScheduler::UploadScheduler(std::chrono::seconds target_interval,
                                 std::chrono::seconds min_interval)
    : UploadScheduler(target_interval, min_interval, target_interval) {}

std::chrono::seconds UploadScheduler::Interval() {
  auto interval = current_interval_;
  if (current_interval_ < target_interval_) {
    current_interval_ *= 2;
    if (current_interval_ >= target_interval_) {
      current_interval_ = target_interval_;
    }
  }
  return interval;
}

}  // namespace cobalt::encoder

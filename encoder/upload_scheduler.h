// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_UPLOAD_SCHEDULER_H_
#define COBALT_ENCODER_UPLOAD_SCHEDULER_H_

#include <chrono>

#include "./logging.h"

namespace cobalt {
namespace encoder {

// UploadScheduler enables providing ShippingManager with information about when
// to schedule uploads to the backend. If provided with an initial_interval that
// is < target_interval, it will exponentially increase that interval until it
// is equal to target_interval. This allows us to start a cobalt client that
// does its first upload quickly, but in the steady state uploads infrequently,
// since the longer a device is up, the more likely it is to remain up.
class UploadScheduler {
 public:
  // Use this constant instead of std::chrono::seconds::max() in
  // UploadScheduler below in order to effectively set the wait time to
  // infinity.
  static const std::chrono::seconds kMaxSeconds;

  // target_interval: How frequently should ShippingManager perform regular
  // periodic sends to the Shuffler? Set to kMaxSeconds to effectively
  // disable periodic sends.
  //
  // min_interval: Because of expedited sends, ShippingManager may sometimes
  // send to the Shuffler more frequently than |target_interval|. This
  // parameter is a safety setting. ShippingManager will never perform two
  // sends within a single period of |min_interval| seconds.
  //
  // initial_interval: Used as the basis for the exponentially increasing
  // Interval() value. The result of Interval starts by returning this value
  // and multiplies it by 2 for each call until the value is greater than or
  // equal to target_interval.
  //
  // REQUIRED:
  // 0 <= min_interval <= target_interval <= kMaxSeconds
  UploadScheduler(std::chrono::seconds target_interval,
                  std::chrono::seconds min_interval,
                  std::chrono::seconds initial_interval);

  UploadScheduler(std::chrono::seconds target_interval,
                  std::chrono::seconds min_interval);

  [[nodiscard]] std::chrono::seconds MinInterval() const {
    return min_interval_;
  }
  std::chrono::seconds Interval();

 private:
  std::chrono::seconds current_interval_;
  std::chrono::seconds target_interval_;
  std::chrono::seconds min_interval_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_UPLOAD_SCHEDULER_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/registry/buckets_config.h"

#include "src/logging.h"

namespace cobalt::config {

uint32_t IntegerBucketConfig::BucketIndex(int64_t val) const {
  // 0 is the underflow bucket.
  if (val < floors_[0]) {
    return 0;
  }

  // TODO(azani): Maybe switch to binary search?
  for (uint32_t i = 1; i < floors_.size(); i++) {
    if (val >= floors_[i - 1] && val < floors_[i]) {
      return i;
    }
  }

  // floors_.size() is the overflow bucket.
  return floors_.size();
}

std::unique_ptr<IntegerBucketConfig> IntegerBucketConfig::CreateFromProto(
    const IntegerBuckets& int_buckets) {
  switch (int_buckets.buckets_case()) {
    case IntegerBuckets::kExponential:
      return CreateExponential(
          int_buckets.exponential().floor(), int_buckets.exponential().num_buckets(),
          int_buckets.exponential().initial_step(), int_buckets.exponential().step_multiplier());

    case IntegerBuckets::kLinear:
      return CreateLinear(int_buckets.linear().floor(), int_buckets.linear().num_buckets(),
                          int_buckets.linear().step_size());

    case IntegerBuckets::BUCKETS_NOT_SET:
      LOG(ERROR) << "IntegerBuckets with buckets field not set.";
      return std::unique_ptr<IntegerBucketConfig>();
  }
}

int64_t IntegerBucketConfig::BucketFloor(uint32_t index) const {
  int64_t floor = INT64_MIN;
  if (index > 0 && index <= floors_.size()) {
    floor = floors_[index - 1];
  }
  return floor;
}

std::unique_ptr<IntegerBucketConfig> IntegerBucketConfig::CreateLinear(int64_t floor,
                                                                       uint32_t num_buckets,
                                                                       uint32_t step_size) {
  if (num_buckets == 0) {
    LOG(ERROR) << "LinearIntegerBucket with 0 buckets.";
    return std::unique_ptr<IntegerBucketConfig>();
  }

  if (step_size == 0) {
    LOG(ERROR) << "LinearIntegerBucket with 0 step size.";
    return std::unique_ptr<IntegerBucketConfig>();
  }

  std::vector<int64_t> floors(num_buckets + 1);
  for (uint32_t i = 0; i < num_buckets + 1; i++) {
    floors[i] = floor + i * step_size;
  }

  return std::unique_ptr<IntegerBucketConfig>(new IntegerBucketConfig(floors));
}

std::unique_ptr<IntegerBucketConfig> IntegerBucketConfig::CreateExponential(
    int64_t floor, uint32_t num_buckets, uint32_t initial_step, uint32_t step_multiplier) {
  if (num_buckets == 0) {
    LOG(ERROR) << "ExponentialIntegerBucket with 0 buckets.";
    return std::unique_ptr<IntegerBucketConfig>();
  }

  if (initial_step == 0) {
    LOG(ERROR) << "ExponentialIntegerBucket with 0 initial_step.";
    return std::unique_ptr<IntegerBucketConfig>();
  }

  if (step_multiplier == 0) {
    LOG(ERROR) << "ExponentialIntegerBucket with 0 step_multiplier.";
    return std::unique_ptr<IntegerBucketConfig>();
  }

  std::vector<int64_t> floors(num_buckets + 1);

  floors[0] = floor;
  uint64_t offset = initial_step;
  for (uint32_t i = 1; i < num_buckets + 1; i++) {
    floors[i] = floor + offset;
    offset *= step_multiplier;
  }
  return std::unique_ptr<IntegerBucketConfig>(new IntegerBucketConfig(floors));
}
}  // namespace cobalt::config

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_REGISTRY_BUCKETS_CONFIG_H_
#define COBALT_SRC_REGISTRY_BUCKETS_CONFIG_H_

#include <memory>
#include <vector>

#include "src/registry/report_definition.pb.h"

namespace cobalt {
namespace config {

// IntegerBucketConfig implements the logic for converting an integer into a
// bucket index according to Cobalt's IntegerBuckets scheme. See the comments in
// metrics.proto for a description of that scheme.
class IntegerBucketConfig {
 public:
  // Constructs and returns an instance of IntegerBucketConfig based on the
  // provided IntegerBuckets proto message.
  // If it fails, it will log an error message and the returned pointer will be
  // null.
  static std::unique_ptr<IntegerBucketConfig> CreateFromProto(const IntegerBuckets& int_buckets);

  // Maps an integer value to a bucket index.
  // Recall that index 0 is the index of the underflow bucket and
  // OverflowBucket() is the index of the overflow bucket.
  [[nodiscard]] uint32_t BucketIndex(int64_t val) const;

  // Returns the index of the underflow bucket: 0.
  [[nodiscard]] uint32_t UnderflowBucket() const { return 0; }

  // Returns the index of the overflow bucket.
  [[nodiscard]] uint32_t OverflowBucket() const { return floors_.size(); }

 private:
  // Constructs an IntegerBucketConfig with the specified floors. See floors_.
  explicit IntegerBucketConfig(std::vector<int64_t> floors) : floors_(std::move(floors)) {}

  // Creates an IntegerBucketConfig with exponentially-sized buckets.
  // There will be num_buckets+2 buckets created with the first bucket being
  // the underflow bucket and the last bucket being the overflow bucket.
  // See ExponentialIntegerBuckets in metrics.proto.
  // If it fails, it will log an error message and the returned pointer will be
  // null.
  static std::unique_ptr<IntegerBucketConfig> CreateExponential(int64_t floor, uint32_t num_buckets,
                                                                uint32_t initial_step,
                                                                uint32_t step_multiplier);

  // Creates an IntegerBucketConfig with identically-sized buckets.
  // There will be num_buckets+2 buckets created with the first bucket being
  // the underflow bucket and the last bucket being the overflow bucket.
  // See LinearIntegerBuckets in metrics.proto.
  // If it fails, it will log an error message and the returned pointer will be
  // null.
  static std::unique_ptr<IntegerBucketConfig> CreateLinear(int64_t floor, uint32_t num_buckets,
                                                           uint32_t step_size);

  // floors_ are the floors of the buckets.
  // Bucket 0 is [min_int64, floors_[0]).
  // Bucket floors_.size() is [floors_[floors_.size()-1], max_int64].
  // Otherwise, bucket i is defined as [floors_[i-1], floors_[i]).
  const std::vector<int64_t> floors_;
};
}  // namespace config
}  // namespace cobalt

#endif  // COBALT_SRC_REGISTRY_BUCKETS_CONFIG_H_

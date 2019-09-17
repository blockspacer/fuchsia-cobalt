// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/registry/buckets_config.h"

#include "gflags/gflags.h"
#include "src/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::config {

// Test the case in which no buckets configuration was set.
TEST(IntegerBucketConfigTest, BucketsNotSetTest) {
  IntegerBuckets int_buckets_proto;

  std::unique_ptr<IntegerBucketConfig> int_bucket_config =
      IntegerBucketConfig::CreateFromProto(int_buckets_proto);

  EXPECT_FALSE(int_bucket_config) << "If no buckets are set, we must return a "
                                  << "nullptr.";
}

// We do not support 0 buckets.
TEST(IntegerBucketConfigTest, LinearZeroBucketsTest) {
  IntegerBuckets int_buckets_proto;

  LinearIntegerBuckets* linear = int_buckets_proto.mutable_linear();
  linear->set_floor(10);
  linear->set_num_buckets(0);
  linear->set_step_size(2);
  std::unique_ptr<IntegerBucketConfig> int_bucket_config =
      IntegerBucketConfig::CreateFromProto(int_buckets_proto);
  EXPECT_FALSE(int_bucket_config) << "Zero buckets is not allowed.";
}

// We do not allow a 0 step size.
TEST(IntegerBucketConfigTest, LinearZeroStepSizeTest) {
  IntegerBuckets int_buckets_proto;

  LinearIntegerBuckets* linear = int_buckets_proto.mutable_linear();
  linear->set_floor(10);
  linear->set_num_buckets(10);
  linear->set_step_size(0);
  std::unique_ptr<IntegerBucketConfig> int_bucket_config =
      IntegerBucketConfig::CreateFromProto(int_buckets_proto);
  EXPECT_FALSE(int_bucket_config) << "Zero step size is not allowed.";
}

// Test the normal linear buckets case.
TEST(IntegerBucketConfigTest, LinearTest) {
  IntegerBuckets int_buckets_proto;
  LinearIntegerBuckets* linear = int_buckets_proto.mutable_linear();
  linear->set_floor(10);
  linear->set_num_buckets(3);
  linear->set_step_size(2);
  std::unique_ptr<IntegerBucketConfig> int_bucket_config =
      IntegerBucketConfig::CreateFromProto(int_buckets_proto);

  // Check the underflow and overflow bucket indices.
  EXPECT_EQ(uint32_t(0), int_bucket_config->UnderflowBucket());
  EXPECT_EQ(uint32_t(4), int_bucket_config->OverflowBucket());

  // Check the underflow bucket.
  EXPECT_EQ(uint32_t(0), int_bucket_config->BucketIndex(-100));
  EXPECT_EQ(uint32_t(0), int_bucket_config->BucketIndex(9));

  // Check the normal buckets.
  EXPECT_EQ(uint32_t(1), int_bucket_config->BucketIndex(10));
  EXPECT_EQ(uint32_t(1), int_bucket_config->BucketIndex(11));
  EXPECT_EQ(uint32_t(2), int_bucket_config->BucketIndex(12));
  EXPECT_EQ(uint32_t(2), int_bucket_config->BucketIndex(13));
  EXPECT_EQ(uint32_t(3), int_bucket_config->BucketIndex(14));
  EXPECT_EQ(uint32_t(3), int_bucket_config->BucketIndex(15));

  // Check the overflow buckets.
  EXPECT_EQ(uint32_t(4), int_bucket_config->BucketIndex(16));
  EXPECT_EQ(uint32_t(4), int_bucket_config->BucketIndex(20));
}

// We do not support 0 buckets.
TEST(IntegerBucketConfigTest, ExponentialZeroBucketsTest) {
  IntegerBuckets int_buckets_proto;
  ExponentialIntegerBuckets* exp = int_buckets_proto.mutable_exponential();
  exp->set_floor(10);
  exp->set_num_buckets(0);
  exp->set_initial_step(5);
  exp->set_step_multiplier(7);

  std::unique_ptr<IntegerBucketConfig> int_bucket_config =
      IntegerBucketConfig::CreateFromProto(int_buckets_proto);
  EXPECT_FALSE(int_bucket_config) << "Zero buckets is not allowed.";
}

// We do not support a 0 initial step.
TEST(IntegerBucketConfigTest, ExponentialZeroInitialStepTest) {
  IntegerBuckets int_buckets_proto;
  ExponentialIntegerBuckets* exp = int_buckets_proto.mutable_exponential();
  exp->set_floor(10);
  exp->set_num_buckets(3);
  exp->set_initial_step(0);
  exp->set_step_multiplier(7);

  std::unique_ptr<IntegerBucketConfig> int_bucket_config =
      IntegerBucketConfig::CreateFromProto(int_buckets_proto);
  EXPECT_FALSE(int_bucket_config) << "Zero initial step is not allowed.";
}

// We do not support a 0 step multiplier.
TEST(IntegerBucketConfigTest, ExponentialZeroStepMultiplierTest) {
  IntegerBuckets int_buckets_proto;
  ExponentialIntegerBuckets* exp = int_buckets_proto.mutable_exponential();
  exp->set_floor(10);
  exp->set_num_buckets(3);
  exp->set_initial_step(10);
  exp->set_step_multiplier(0);

  std::unique_ptr<IntegerBucketConfig> int_bucket_config =
      IntegerBucketConfig::CreateFromProto(int_buckets_proto);
  EXPECT_FALSE(int_bucket_config) << "Zero step multiplier is not allowed.";
}

// Test the normal exponential buckets case.
TEST(IntegerBucketConfigTest, ExponentialTest) {
  IntegerBuckets int_buckets_proto;
  ExponentialIntegerBuckets* exp = int_buckets_proto.mutable_exponential();
  exp->set_floor(10);
  exp->set_num_buckets(3);
  exp->set_initial_step(5);
  exp->set_step_multiplier(7);

  std::unique_ptr<IntegerBucketConfig> int_bucket_config =
      IntegerBucketConfig::CreateFromProto(int_buckets_proto);

  // Check the underflow and overflow bucket indices.
  EXPECT_EQ(uint32_t(0), int_bucket_config->UnderflowBucket());
  EXPECT_EQ(uint32_t(4), int_bucket_config->OverflowBucket());

  // The expected buckets are:
  // (-infinity, 10), [10, 15), [15, 45), [45, 255), [255, infinity)

  // Check the underflow bucket.
  EXPECT_EQ(uint32_t(0), int_bucket_config->BucketIndex(-100));
  EXPECT_EQ(uint32_t(0), int_bucket_config->BucketIndex(9));

  // Check the normal buckets.
  EXPECT_EQ(uint32_t(1), int_bucket_config->BucketIndex(10));
  EXPECT_EQ(uint32_t(1), int_bucket_config->BucketIndex(14));
  EXPECT_EQ(uint32_t(2), int_bucket_config->BucketIndex(15));
  EXPECT_EQ(uint32_t(2), int_bucket_config->BucketIndex(44));
  EXPECT_EQ(uint32_t(3), int_bucket_config->BucketIndex(45));
  EXPECT_EQ(uint32_t(3), int_bucket_config->BucketIndex(244));

  // Check the overflow buckets.
  EXPECT_EQ(uint32_t(4), int_bucket_config->BucketIndex(255));
  EXPECT_EQ(uint32_t(4), int_bucket_config->BucketIndex(256));
  EXPECT_EQ(uint32_t(4), int_bucket_config->BucketIndex(300));
}

// This is a very likely-to-be-used logarithmic scale, so we test it explicitly.
TEST(IntegerBucketConfigTest, ExponentialTestCommon) {
  IntegerBuckets int_buckets_proto;
  ExponentialIntegerBuckets* exp = int_buckets_proto.mutable_exponential();
  exp->set_floor(0);
  exp->set_num_buckets(3);
  exp->set_initial_step(10);
  exp->set_step_multiplier(10);

  std::unique_ptr<IntegerBucketConfig> int_bucket_config =
      IntegerBucketConfig::CreateFromProto(int_buckets_proto);

  // Check the underflow and overflow bucket indices.
  EXPECT_EQ(uint32_t(0), int_bucket_config->UnderflowBucket());
  EXPECT_EQ(uint32_t(4), int_bucket_config->OverflowBucket());

  // The expected buckets are:
  // (-infinity, 0), [0, 10), [10, 100), [100, 1000), [1000, +inifinity)

  // Check the underflow bucket.
  EXPECT_EQ(uint32_t(0), int_bucket_config->BucketIndex(-100));
  EXPECT_EQ(uint32_t(0), int_bucket_config->BucketIndex(-1));

  // Check the normal buckets.
  EXPECT_EQ(uint32_t(1), int_bucket_config->BucketIndex(0));
  EXPECT_EQ(uint32_t(1), int_bucket_config->BucketIndex(9));
  EXPECT_EQ(uint32_t(2), int_bucket_config->BucketIndex(10));
  EXPECT_EQ(uint32_t(2), int_bucket_config->BucketIndex(99));
  EXPECT_EQ(uint32_t(3), int_bucket_config->BucketIndex(100));
  EXPECT_EQ(uint32_t(3), int_bucket_config->BucketIndex(999));

  // Check the overflow buckets.
  EXPECT_EQ(uint32_t(4), int_bucket_config->BucketIndex(1000));
  EXPECT_EQ(uint32_t(4), int_bucket_config->BucketIndex(1001));
  EXPECT_EQ(uint32_t(4), int_bucket_config->BucketIndex(1000000));
}

// Test that bucket sizes that would overflow an int32 but not an int64 works
// correctly.
TEST(IntegerBucketConfigTest, ExponentialTestLarge) {
  IntegerBuckets int_buckets_proto;
  ExponentialIntegerBuckets* exp = int_buckets_proto.mutable_exponential();
  exp->set_floor(0);
  exp->set_num_buckets(17);
  exp->set_initial_step(1000000);
  exp->set_step_multiplier(2);

  std::unique_ptr<IntegerBucketConfig> int_bucket_config =
      IntegerBucketConfig::CreateFromProto(int_buckets_proto);

  // Check the underflow and overflow bucket indices.
  EXPECT_EQ(uint32_t(0), int_bucket_config->UnderflowBucket());
  EXPECT_EQ(uint32_t(18), int_bucket_config->OverflowBucket());

  // The expected buckets are:
  // (-infinity, 0), [0, 1000000), [1000000, 2000000), [2000000, 4000000),
  // [4000000, 8000000), [8000000, 16000000), [16000000, 32000000),
  // [32000000, 64000000), [64000000, 128000000), [128000000, 256000000),
  // [256000000, 512000000), [512000000, 1024000000), [1024000000, 2048000000)
  // [2048000000, 4096000000), [4096000000, 8192000000),
  // [8192000000, 16384000000), [16384000000, 32768000000),
  // [32768000000, 65536000000), [65536000000, +infinity)

  // Check the underflow bucket.
  EXPECT_EQ(uint32_t(0), int_bucket_config->BucketIndex(-100));
  EXPECT_EQ(uint32_t(0), int_bucket_config->BucketIndex(-1));

  // Check the normal buckets.
  EXPECT_EQ(uint32_t(1), int_bucket_config->BucketIndex(0));
  EXPECT_EQ(uint32_t(1), int_bucket_config->BucketIndex(999999));
  EXPECT_EQ(uint32_t(2), int_bucket_config->BucketIndex(1000000));
  EXPECT_EQ(uint32_t(2), int_bucket_config->BucketIndex(1999999));
  EXPECT_EQ(uint32_t(3), int_bucket_config->BucketIndex(2000000));
  EXPECT_EQ(uint32_t(3), int_bucket_config->BucketIndex(3999999));
  EXPECT_EQ(uint32_t(4), int_bucket_config->BucketIndex(4000000));
  EXPECT_EQ(uint32_t(4), int_bucket_config->BucketIndex(7999999));
  EXPECT_EQ(uint32_t(5), int_bucket_config->BucketIndex(8000000));
  EXPECT_EQ(uint32_t(5), int_bucket_config->BucketIndex(15999999));
  EXPECT_EQ(uint32_t(6), int_bucket_config->BucketIndex(16000000));
  EXPECT_EQ(uint32_t(6), int_bucket_config->BucketIndex(31999999));
  EXPECT_EQ(uint32_t(7), int_bucket_config->BucketIndex(32000000));
  EXPECT_EQ(uint32_t(7), int_bucket_config->BucketIndex(63999999));
  EXPECT_EQ(uint32_t(8), int_bucket_config->BucketIndex(64000000));
  EXPECT_EQ(uint32_t(8), int_bucket_config->BucketIndex(127999999));
  EXPECT_EQ(uint32_t(9), int_bucket_config->BucketIndex(128000000));
  EXPECT_EQ(uint32_t(9), int_bucket_config->BucketIndex(255999999));
  EXPECT_EQ(uint32_t(10), int_bucket_config->BucketIndex(256000000));
  EXPECT_EQ(uint32_t(10), int_bucket_config->BucketIndex(511999999));
  EXPECT_EQ(uint32_t(11), int_bucket_config->BucketIndex(512000000));
  EXPECT_EQ(uint32_t(11), int_bucket_config->BucketIndex(1023999999));
  EXPECT_EQ(uint32_t(12), int_bucket_config->BucketIndex(1024000000));
  EXPECT_EQ(uint32_t(12), int_bucket_config->BucketIndex(2047999999));
  EXPECT_EQ(uint32_t(13), int_bucket_config->BucketIndex(2048000000));
  EXPECT_EQ(uint32_t(13), int_bucket_config->BucketIndex(4095999999));
  EXPECT_EQ(uint32_t(14), int_bucket_config->BucketIndex(4096000000));
  EXPECT_EQ(uint32_t(14), int_bucket_config->BucketIndex(8191999999));
  EXPECT_EQ(uint32_t(15), int_bucket_config->BucketIndex(8192000000));
  EXPECT_EQ(uint32_t(15), int_bucket_config->BucketIndex(16383999999));
  EXPECT_EQ(uint32_t(16), int_bucket_config->BucketIndex(16384000000));
  EXPECT_EQ(uint32_t(16), int_bucket_config->BucketIndex(32767999999));
  EXPECT_EQ(uint32_t(17), int_bucket_config->BucketIndex(32768000000));
  EXPECT_EQ(uint32_t(17), int_bucket_config->BucketIndex(65535999999));

  // Check the overflow buckets.
  EXPECT_EQ(uint32_t(18), int_bucket_config->BucketIndex(65536000000));
  EXPECT_EQ(uint32_t(18), int_bucket_config->BucketIndex(65536000001));
  EXPECT_EQ(uint32_t(18), int_bucket_config->BucketIndex(1994356000000));
}

}  // namespace cobalt::config

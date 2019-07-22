// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoder/encoder.h"

#include <utility>

#include "./gtest.h"
#include "./logging.h"
#include "config/client_config.h"
#include "encoder/client_secret.h"
#include "encoder/fake_system_data.h"
// Generated from encoder_test_config.yaml
#include "encoder/encoder_test_config.h"
#include "encoder/project_context.h"
#include "third_party/gflags/include/gflags/gflags.h"

namespace cobalt::encoder {

using config::ClientConfig;

namespace {

// These must match values specified in the build files.
constexpr uint32_t kCustomerId = 1;
constexpr uint32_t kProjectId = 1;

// This unix timestamp corresponds to Friday Dec 2, 2016 in UTC
// and Thursday Dec 1, 2016 in Pacific time.
constexpr time_t kSomeTimestamp = 1480647356;
// This is the day index for Friday Dec 2, 2016
constexpr uint32_t kUtcDayIndex = 17137;
// This is the day index for Thurs Dec 1, 2016
constexpr uint32_t kPacificDayIndex = 17136;

constexpr uint32_t kSecondsPerHour = 3600;
constexpr int32_t kPacificTimezone = 8;

// Returns a ProjectContext obtained by parsing the configuration specified
// in envelope_maker_test_config.yaml
std::shared_ptr<ProjectContext> GetTestProject() {
  // Parse the base64-encoded, serialized CobaltRegistry in
  // encoder_test_config.h. This is generated from encoder_test_config.yaml.
  // Edit encoder_test_config.yaml to make changes. The variable name
  // below, |kCobaltRegistryBase64|, must match what is specified in the
  // invocation of generate_test_config_h() in CMakeLists.txt.
  std::unique_ptr<ClientConfig> client_config =
      ClientConfig::CreateFromCobaltRegistryBase64(kCobaltRegistryBase64);
  EXPECT_NE(nullptr, client_config);

  return std::shared_ptr<ProjectContext>(new ProjectContext(
      kCustomerId, kProjectId,
      std::shared_ptr<ClientConfig>(client_config.release())));
}
}  // namespace

// Checks |result|: Checks that the status is kOK, that the observation and
// metadata are not null, that the observation has a single part named
// "Part1", that it uses the expected encoding and that it is not empty.
// |expect_utc| should be true to indicate that it is expected that the
// day index was computed using UTC.
void CheckSinglePartResult(
    const Encoder::Result& result, uint32_t expected_metric_id,
    uint32_t expected_encoding_config_id, bool expect_utc,
    const ObservationPart::ValueCase& expected_encoding) {
  ASSERT_EQ(Encoder::kOK, result.status);
  ASSERT_NE(nullptr, result.observation);
  ASSERT_NE(nullptr, result.metadata);
  EXPECT_EQ(kCustomerId, result.metadata->customer_id());
  EXPECT_EQ(kProjectId, result.metadata->project_id());
  EXPECT_EQ(expected_metric_id, result.metadata->metric_id());
  if (expect_utc) {
    EXPECT_EQ(kUtcDayIndex, result.metadata->day_index());
  } else {
    // Only perform the following check when running this test in the Pacific
    // timezone. Note that |timezone| is a global variable defined in <ctime>
    // that stores difference between UTC and the latest local standard time, in
    // seconds west of UTC. This value is not adjusted for daylight saving.
    // See https://www.gnu.org/software/libc/manual/html_node/ \
    //                              Time-Zone-Functions.html#Time-Zone-Functions
    if (timezone / kSecondsPerHour == kPacificTimezone) {
      EXPECT_EQ(kPacificDayIndex, result.metadata->day_index());
    }
  }

  const Observation& observation = *result.observation;
  // The Metric specified has only one part named "Part1" so the encoded
  // observation should have one part named "Part1".
  ASSERT_EQ(1, observation.parts_size());
  const ObservationPart& obs_part = observation.parts().at("Part1");

  // The observation part should use the right encoding.
  EXPECT_EQ(expected_encoding_config_id, obs_part.encoding_config_id());
  EXPECT_EQ(expected_encoding, obs_part.value_case());

  // We sanity test the Observation by checking that it is not empty.
  switch (expected_encoding) {
    case ObservationPart::kForculus: {
      EXPECT_NE("", obs_part.forculus().ciphertext());
      break;
    }
    case ObservationPart::kRappor: {
      EXPECT_NE("", obs_part.rappor().data());
      break;
    }
    case ObservationPart::kBasicRappor: {
      EXPECT_NE("", obs_part.basic_rappor().data());
      break;
    }
    case ObservationPart::kUnencoded: {
      EXPECT_TRUE(obs_part.unencoded().has_unencoded_value());
      break;
    }
    default:
      EXPECT_TRUE(false) << " Unexpected case";
  }
}

void CheckSystemProfileValid(const Encoder::Result& result,
                             const Metric* metric) {
  ASSERT_EQ(Encoder::kOK, result.status);
  if (metric->system_profile_field_size() == 0) {
    EXPECT_FALSE(result.metadata->has_system_profile());
    return;
  }

  const auto& fields = metric->system_profile_field();
  if (std::find(fields.begin(), fields.end(), SystemProfileField::OS) !=
      fields.end()) {
    EXPECT_EQ(SystemProfile::FUCHSIA, result.metadata->system_profile().os());
  } else {
    EXPECT_EQ(SystemProfile::UNKNOWN_OS,
              result.metadata->system_profile().os());
  }

  if (std::find(fields.begin(), fields.end(), SystemProfileField::ARCH) !=
      fields.end()) {
    EXPECT_EQ(SystemProfile::ARM_64, result.metadata->system_profile().arch());
  } else {
    EXPECT_EQ(SystemProfile::UNKNOWN_ARCH,
              result.metadata->system_profile().arch());
  }

  if (std::find(fields.begin(), fields.end(), SystemProfileField::BOARD_NAME) !=
      fields.end()) {
    EXPECT_EQ("Testing Board", result.metadata->system_profile().board_name());
  } else {
    EXPECT_EQ("", result.metadata->system_profile().board_name());
  }

  if (std::find(fields.begin(), fields.end(),
                SystemProfileField::PRODUCT_NAME) != fields.end()) {
    EXPECT_EQ("Testing Product",
              result.metadata->system_profile().product_name());
  } else {
    EXPECT_EQ("", result.metadata->system_profile().product_name());
  }
}

// Tests the EncodeString() method using the given |value| and the given
// metric and encoding. The metric is expected to have a single part named
// "Part1". We validate that there are no errors and that the
// produced Observation has the |expected_type| and is non-empty.
// Returns the encoded Observation.
Observation DoEncodeStringTest(
    const std::string& value, uint32_t metric_id, uint32_t encoding_config_id,
    bool expect_utc, const ObservationPart::ValueCase& expected_encoding) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  FakeSystemData system_data;

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret(), &system_data);
  // Set a static current time so we can test the day_index computation.
  encoder.set_current_time(kSomeTimestamp);

  // Encode an observation for the given metric and encoding. The metric is
  // expected to have a single part.
  Encoder::Result result =
      encoder.EncodeString(metric_id, encoding_config_id, value);
  CheckSinglePartResult(result, metric_id, encoding_config_id, expect_utc,
                        expected_encoding);
  CheckSystemProfileValid(result, project->Metric(metric_id));
  // In case the encode operation failed
  // we CHECK fail here because Google Test doesn't allow us to FAIL() from
  // a method that has a return value and if we do nothing the following line
  // will cause Protobuf to CHECK fail in a more mysterious way.
  CHECK(result.status == Encoder::kOK);
  return *result.observation;
}

// Tests the EncodeInt() method using the given |value| and the given
// metric and encoding. The metric is expected to have a single part named
// "Part1". The encoding is expected to be for Basic RAPPOR.
// We validate that there are no errors and that the
// produced Observation has the |expected_type| and is non-empty.
// Returns the encoded Observation.
Observation DoEncodeIntTest(
    int64_t value, uint32_t metric_id, uint32_t encoding_config_id,
    bool expect_utc, const ObservationPart::ValueCase& expected_encoding) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  FakeSystemData system_data;

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret(), &system_data);
  // Set a static current time so we can test the day_index computation.
  encoder.set_current_time(kSomeTimestamp);

  // Encode an observation for the given metric and encoding. The metric is
  // expected to have a single part.
  Encoder::Result result =
      encoder.EncodeInt(metric_id, encoding_config_id, value);

  CheckSinglePartResult(result, metric_id, encoding_config_id, expect_utc,
                        expected_encoding);
  CheckSystemProfileValid(result, project->Metric(metric_id));
  return *result.observation;
}

// Tests the EncodeDouble() method using the given |value| and the given
// metric and encoding. The metric is expected to have a single part named
// "Part1".
//
// If expectOK is true then we verify that there are no errors and that the
// produced Observation has the |expected_type| and is non-empty. Otherwise
// we verify that kInvalidArguments is returned.
Observation DoEncodeDoubleTest(
    bool expectOK, double value, uint32_t metric_id,
    uint32_t encoding_config_id, bool expect_utc,
    const ObservationPart::ValueCase& expected_encoding) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  FakeSystemData system_data;

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret(), &system_data);
  // Set a static current time so we can test the day_index computation.
  encoder.set_current_time(kSomeTimestamp);

  // Encode an observation for the given metric and encoding. The metric is
  // expected to have a single part.
  Encoder::Result result =
      encoder.EncodeDouble(metric_id, encoding_config_id, value);

  if (expectOK) {
    CheckSinglePartResult(result, metric_id, encoding_config_id, expect_utc,
                          expected_encoding);
    CheckSystemProfileValid(result, project->Metric(metric_id));
  } else {
    EXPECT_EQ(Encoder::kInvalidArguments, result.status)
        << "encoding_config_id=" << encoding_config_id;
  }

  return *result.observation;
}

// Tests the EncodeIndex() method using the given |index| and the given
// metric and encoding. The metric is expected to have a single part named
// "Part1".
//
// If expectOK is true then we verify that there are no errors and that the
// produced Observation has the |expected_type| and is non-empty. Otherwise
// we verify that kInvalidArguments is returned.
void DoEncodeIndexTest(bool expectOK, uint32_t index, uint32_t metric_id,
                       uint32_t encoding_config_id, bool expect_utc,
                       const ObservationPart::ValueCase& expected_encoding) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  FakeSystemData system_data;

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret(), &system_data);
  // Set a static current time so we can test the day_index computation.
  encoder.set_current_time(kSomeTimestamp);

  // Encode an observation for the given metric and encoding. The metric is
  // expected to have a single part.
  Encoder::Result result =
      encoder.EncodeIndex(metric_id, encoding_config_id, index);

  if (expectOK) {
    CheckSinglePartResult(result, metric_id, encoding_config_id, expect_utc,
                          expected_encoding);
    CheckSystemProfileValid(result, project->Metric(metric_id));
  } else {
    EXPECT_EQ(Encoder::kInvalidArguments, result.status)
        << "encoding_config_id=" << encoding_config_id;
  }
}

// Tests the EncodeBlob() method using the given |value| and the given
// metric and encoding. The metric is expected to have a single part named
// "Part1". The encoding is expected to be for Forculus.
// We validate that there are no errors and that the
// produced Observation has the |expected_type| and is non-empty.
// Returns the encoded Observation.
Observation DoEncodeBlobTest(
    const void* data, size_t num_bytes, uint32_t metric_id,
    uint32_t encoding_config_id, bool expect_utc,
    const ObservationPart::ValueCase& expected_encoding) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  FakeSystemData system_data;

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret(), &system_data);
  // Set a static current time so we can test the day_index computation.
  encoder.set_current_time(kSomeTimestamp);

  // Encode an observation for the given metric and encoding. The metric is
  // expected to have a single part.
  Encoder::Result result =
      encoder.EncodeBlob(metric_id, encoding_config_id, data, num_bytes);

  CheckSinglePartResult(result, metric_id, encoding_config_id, expect_utc,
                        expected_encoding);
  CheckSystemProfileValid(result, project->Metric(metric_id));
  return *result.observation;
}

// Tests the EncodeIntBucketDistribution() method using the given |distribution|
// and the given metric and encoding. The metric is expected to have a single
// part named "Part1". The encoding is expected to be NoOp.
// Returns the encoded Observation.
//
// If expectOK is true then we verify that there are no errors and that the
// produced Observation has the |expected_type| and is non-empty. Otherwise
// we verify that kInvalidArguments is returned.
Observation DoEncodeIntBucketDistributionTest(
    bool expect_ok, const std::map<uint32_t, uint64_t>& distribution,
    uint32_t metric_id, uint32_t encoding_config_id, bool expect_utc,
    const ObservationPart::ValueCase& expected_encoding) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  FakeSystemData system_data;

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret(), &system_data);
  // Set a static current time so we can test the day_index computation.
  encoder.set_current_time(kSomeTimestamp);

  // Encode an observation for the given metric and encoding. The metric is
  // expected to have a single part.
  Encoder::Result result = encoder.EncodeIntBucketDistribution(
      metric_id, encoding_config_id, distribution);

  if (expect_ok) {
    CheckSinglePartResult(result, metric_id, encoding_config_id, expect_utc,
                          expected_encoding);
    CheckSystemProfileValid(result, project->Metric(metric_id));
  } else {
    EXPECT_EQ(Encoder::kInvalidArguments, result.status)
        << "encoding_config_id=" << encoding_config_id;
  }
  return *result.observation;
}

// Tests EncodeString() with Forculus as the specified encoding.
TEST(EncoderEncoderTest, EncodeStringForculus) {
  // Metric 1 has a single string part.
  // EncodingConfig 1 is Forculus.
  DoEncodeStringTest("some value", kSingleStringMetricId, kForculusEncodingId,
                     false, ObservationPart::kForculus);
}

// Tests EncodeString() with String RAPPOR as the specified encoding.
TEST(EncoderEncoderTest, EncodeStringRappor) {
  // Metric 1 has a single string part.
  // EncodingConfig 2 is String RAPPOR
  DoEncodeStringTest("some value", kSingleStringMetricId, kRapporEncodingId,
                     false, ObservationPart::kRappor);
}

// Tests EncodeString() with Basic RAPPOR as the specified encoding.
TEST(EncoderEncoderTest, EncodeStringBasicRappor) {
  // Metric 1 has a single string part.
  // EncodingConfig 3 is Basic RAPPOR with string values. Here we need the
  // value to be one of the categories.
  DoEncodeStringTest("Apple", kSingleStringMetricId,
                     kBasicStringRapporEncodingId, false,
                     ObservationPart::kBasicRappor);
}

TEST(EncoderEncoderTest, EncodeStringForculusWithSystemProfile) {
  // Metric 9, 10, and 11 have a single string part, with 1, 2, or 3
  // system_profile_fields.
  // EncodingConfig 1 is Forculus.
  DoEncodeStringTest("Apple", kOneSystemProfileFieldMetricId,
                     kForculusEncodingId, false, ObservationPart::kForculus);
  DoEncodeStringTest("Pear", kTwoSystemProfileFieldsMetricId,
                     kForculusEncodingId, false, ObservationPart::kForculus);
  DoEncodeStringTest("Grapefruit", kThreeSystemProfileFieldsMetricId,
                     kForculusEncodingId, false, ObservationPart::kForculus);
  DoEncodeStringTest("Pineapple", kFourSystemProfileFieldsMetricId,
                     kForculusEncodingId, false, ObservationPart::kForculus);
}

// Tests EncodeString() with NoOp as the specified encoding.
TEST(EncoderEncoderTest, EncodeStringNoOp) {
  // Metric 1 has a single string part.
  // EncodingConfig 7 is NoOp.
  auto obs =
      DoEncodeStringTest("some value", kSingleStringMetricId, kNoOpEncodingId,
                         false, ObservationPart::kUnencoded);

  EXPECT_EQ(
      "some value",
      obs.parts().at("Part1").unencoded().unencoded_value().string_value());
}

// Tests EncodeInt() with Basic RAPPOR as the specified encoding.
TEST(EncoderEncoderTest, EncodeIntBasicRappor) {
  // Metric 2 has a single integer part.
  // EncodingConfig 4 is Basic RAPPOR with int values. Here we need the value
  // to be one of the categories.
  // NOLINTNEXTLINE
  DoEncodeIntTest(125, kSingleIntMetricId, kBasicIntRapporEncodingId, true,
                  ObservationPart::kBasicRappor);
}

// Tests the EncodeIndex() method with both valid and invalid inputs.
TEST(EncoderEncoderTest, EncodeIndex) {
  // Metric 6 has a single part of type INDEX.
  // EncodingConfig 8 is Basic RAPPOR with five INDEXed categories.
  bool expect_ok = true;
  uint32_t index = 0;
  bool expect_utc = true;
  DoEncodeIndexTest(expect_ok, index, kIndexPartMetricId,
                    kBasicRappor5CategoriesEncodingId, expect_utc,
                    ObservationPart::kBasicRappor);
  index = 1;
  DoEncodeIndexTest(expect_ok, index, kIndexPartMetricId,
                    kBasicRappor5CategoriesEncodingId, expect_utc,
                    ObservationPart::kBasicRappor);
  index = 4;
  DoEncodeIndexTest(expect_ok, index, kIndexPartMetricId,
                    kBasicRappor5CategoriesEncodingId, expect_utc,
                    ObservationPart::kBasicRappor);

  // Index 5 should yield kInalidArgs.
  expect_ok = false;
  index = 5;  // NOLINT
  DoEncodeIndexTest(expect_ok, index, kIndexPartMetricId,
                    kBasicRappor5CategoriesEncodingId, expect_utc,
                    ObservationPart::kBasicRappor);

  // Reset to index 0 just to confirm it still succeeds.
  expect_ok = true;
  index = 0;
  DoEncodeIndexTest(expect_ok, index, kIndexPartMetricId,
                    kBasicRappor5CategoriesEncodingId, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to metric 1 which has one string part. That should fail.
  expect_ok = false;
  DoEncodeIndexTest(expect_ok, index, kSingleStringMetricId,
                    kBasicRappor5CategoriesEncodingId, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to metric 2 which has one int part. That should fail.
  DoEncodeIndexTest(expect_ok, index, kSingleIntMetricId,
                    kBasicRappor5CategoriesEncodingId, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to metric 3 which has one blob part. That should fail.
  DoEncodeIndexTest(expect_ok, index, kSingleBlobMetricId,
                    kBasicRappor5CategoriesEncodingId, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to metric 7 which has one double part. That should fail.
  // NOLINTNEXTLINE
  DoEncodeIndexTest(expect_ok, index, 7, kBasicRappor5CategoriesEncodingId,
                    expect_utc, ObservationPart::kBasicRappor);

  // Reset to metric 6 just to confirm it still succeeds.
  expect_ok = true;
  DoEncodeIndexTest(expect_ok, index, kIndexPartMetricId,
                    kBasicRappor5CategoriesEncodingId, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to encoding 1 which is Forculus. That should fail.
  expect_ok = false;
  DoEncodeIndexTest(expect_ok, index, kIndexPartMetricId, kForculusEncodingId,
                    expect_utc, ObservationPart::kForculus);

  // Now we switch to encoding 2 which is String RAPPOR. That should fail.
  DoEncodeIndexTest(expect_ok, index, kIndexPartMetricId, kRapporEncodingId,
                    expect_utc, ObservationPart::kRappor);

  // Now we switch to encoding 3 which is Basic RAPPOR with string categories.
  // That should fail.
  DoEncodeIndexTest(expect_ok, index, kIndexPartMetricId,
                    kBasicStringRapporEncodingId, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to encoding 4 which is Basic RAPPOR with int categories.
  // That should fail.
  DoEncodeIndexTest(expect_ok, index, kIndexPartMetricId,
                    kBasicIntRapporEncodingId, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to encoding 7 which is NoOpEncoding. That should be OK.
  expect_ok = true;
  DoEncodeIndexTest(expect_ok, index, kIndexPartMetricId, kNoOpEncodingId,
                    expect_utc, ObservationPart::kUnencoded);
}

// Tests the EncodeDouble() method with both valid and invalid inputs.
TEST(EncoderEncoderTest, EncodeDouble) {
  // Metric 7 has a single part of type DOUBLE.
  // EncodingConfig 7 is NoOp.
  bool expect_ok = true;
  double value = M_PI;
  bool expect_utc = true;
  DoEncodeDoubleTest(expect_ok, value, kDoublePartMetricId, kNoOpEncodingId,
                     expect_utc, ObservationPart::kUnencoded);

  // Now we switch to metric 1 which has one string part. That should fail.
  expect_ok = false;
  DoEncodeDoubleTest(expect_ok, value, kSingleStringMetricId, kNoOpEncodingId,
                     expect_utc, ObservationPart::kUnencoded);

  // Now we switch to metric 2 which has one int part. That should fail.
  DoEncodeDoubleTest(expect_ok, value, kSingleIntMetricId, kNoOpEncodingId,
                     expect_utc, ObservationPart::kUnencoded);

  // Now we switch to metric 3 which has one blob part. That should fail.
  DoEncodeDoubleTest(expect_ok, value, kSingleBlobMetricId, kNoOpEncodingId,
                     expect_utc, ObservationPart::kUnencoded);

  // Reset to metric 7 just to confirm it still succeeds.
  expect_ok = true;
  DoEncodeDoubleTest(expect_ok, value, kDoublePartMetricId, kNoOpEncodingId,
                     expect_utc, ObservationPart::kUnencoded);

  // Now we switch to encoding 1 which is Forculus. That should fail.
  expect_ok = false;
  DoEncodeDoubleTest(expect_ok, value, kDoublePartMetricId, kForculusEncodingId,
                     expect_utc, ObservationPart::kForculus);

  // Now we switch to encoding 2 which is String RAPPOR. That should fail.
  DoEncodeDoubleTest(expect_ok, value, kDoublePartMetricId, kRapporEncodingId,
                     expect_utc, ObservationPart::kRappor);

  // Now we switch to encoding 3 which is Basic RAPPOR with string categories.
  // That should fail.
  DoEncodeDoubleTest(expect_ok, value, kDoublePartMetricId,
                     kBasicStringRapporEncodingId, expect_utc,
                     ObservationPart::kBasicRappor);

  // Now we switch to encoding 4 which is Basic RAPPOR with int categories.
  // That should fail.
  DoEncodeDoubleTest(expect_ok, value, kDoublePartMetricId,
                     kBasicIntRapporEncodingId, expect_utc,
                     ObservationPart::kBasicRappor);
}

// Tests EncodeInt() with NoOp encoding as the specified encoding.
TEST(EncoderEncoderTest, EncodeIntNoOp) {
  // Metric 2 has a single integer part.
  // EncodingConfig 7 is NoOp
  // NOLINTNEXTLINE
  auto obs = DoEncodeIntTest(42, kSingleIntMetricId, kNoOpEncodingId, true,
                             ObservationPart::kUnencoded);
  EXPECT_EQ(42u,
            obs.parts().at("Part1").unencoded().unencoded_value().int_value());
}

// Tests EncodeBlob() with Forculus as the specified encoding.
TEST(EncoderEncoderTest, EncodeBlobForculus) {
  // Metric 3 has a single blob part.
  // EncodingConfig 1 is Forculus.
  std::string a_blob("This is a blob");
  DoEncodeBlobTest(reinterpret_cast<const void*>(a_blob.data()), a_blob.size(),
                   kSingleBlobMetricId, kForculusEncodingId, false,
                   ObservationPart::kForculus);
}

// Tests EncodeBlob() with NoOp encoding as the specified encoding.
TEST(EncoderEncoderTest, EncodeBlobNoOp) {
  // Metric 3 has a single blob part.
  // EncodingConfig 7 is NoOp.
  std::string a_blob("This is a blob");
  auto obs = DoEncodeBlobTest(
      reinterpret_cast<const void*>(a_blob.data()), a_blob.size(),
      kSingleBlobMetricId, kNoOpEncodingId, false, ObservationPart::kUnencoded);
  EXPECT_EQ("This is a blob",
            obs.parts().at("Part1").unencoded().unencoded_value().blob_value());
}

// Tests EncodeIntBucketDistribution() with NoOp encoding.
TEST(EncoderEncoderTest, EncodeIntBucketDistributionNoOp) {
  // Metric 9 has a single int bucket distribution part.
  // EncodingConfig 7 is NoOp.
  std::map<uint32_t, uint64_t> distribution = {
      {0, 10}, {2, 6}, {11, 1}};  // NOLINT
  bool expect_ok = true;
  bool expect_utc = true;
  auto obs = DoEncodeIntBucketDistributionTest(
      expect_ok, distribution, kIntDistributionMetricId, kNoOpEncodingId,
      expect_utc, ObservationPart::kUnencoded);

  EXPECT_EQ(uint64_t(3), obs.parts()
                             .at("Part1")
                             .unencoded()
                             .unencoded_value()
                             .int_bucket_distribution()
                             .counts()
                             .size());

  for (auto it = distribution.begin(); it != distribution.end(); it++) {
    EXPECT_EQ(it->second, obs.parts()
                              .at("Part1")
                              .unencoded()
                              .unencoded_value()
                              .int_bucket_distribution()
                              .counts()
                              .at(it->first));
  }

  expect_ok = false;
  // Metric 1 has a single string part. That should fail.
  DoEncodeIntBucketDistributionTest(expect_ok, distribution,
                                    kSingleStringMetricId, kNoOpEncodingId,
                                    expect_utc, ObservationPart::kUnencoded);

  // Metric 2 has an integer part, but no int_buckets set. That should fail.
  DoEncodeIntBucketDistributionTest(expect_ok, distribution, kSingleIntMetricId,
                                    kNoOpEncodingId, expect_utc,
                                    ObservationPart::kUnencoded);

  // There are only 10 buckets + the overflow buckets configured.
  // This should fail.
  distribution[12] = 10;  // NOLINT
  DoEncodeIntBucketDistributionTest(expect_ok, distribution,
                                    kIntDistributionMetricId, kNoOpEncodingId,
                                    expect_utc, ObservationPart::kUnencoded);
}

TEST(EncoderEncoderTest, MetricId) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  FakeSystemData system_data;

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret(), &system_data);
  EXPECT_EQ(kSingleStringMetricId, encoder.MetricId("SingleString"));
}

TEST(EncoderEncoderTest, DefaultEncodingsForMetric) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  FakeSystemData system_data;

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret(), &system_data);
  auto encodings = encoder.DefaultEncodingsForMetric(kSingleStringMetricId);
  EXPECT_EQ(1u, encodings.size());
  EXPECT_EQ(kForculusEncodingId, encodings.begin()->second);
}

TEST(EncoderEncoderTest, GetMetric) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  FakeSystemData system_data;

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret(), &system_data);
  auto metric = encoder.GetMetric(kSingleStringMetricId);
  EXPECT_NE(nullptr, metric);
}

// Tests the advanced API, when used corretly.
TEST(EncoderEncoderTest, AdvancedApiNoErrors) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  FakeSystemData system_data;
  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret(), &system_data);

  Observation observation;
  Encoder::Value value;

  // EncodingConfig 2 is String RAPPOR
  value.AddStringPart(kRapporEncodingId, "city", "San Francisco");
  // EncodingConfig 4 is Basic RAPPOR with integer categories.
  value.AddIntPart(kBasicIntRapporEncodingId, "rating", 125);  // NOLINT
  // Metric 4 has a "city" part of type STRING and
  // a "rating" part of type INT.
  Encoder::Result result = encoder.Encode(kCityRatingMetricId, value);

  // Check the result
  ASSERT_EQ(Encoder::kOK, result.status);
  ASSERT_NE(nullptr, result.observation);
  ASSERT_NE(nullptr, result.metadata);
  EXPECT_EQ(kCustomerId, result.metadata->customer_id());
  EXPECT_EQ(kProjectId, result.metadata->project_id());
  EXPECT_EQ(4u, result.metadata->metric_id());
  // We did not set the current time to a static value but rather used the
  // real time that the test was run. Sanity test the day index: It should
  // be at least the day on which this test was written and less than
  // 20 years in the future from that.
  EXPECT_TRUE(result.metadata->day_index() >= kPacificDayIndex);
  EXPECT_TRUE(result.metadata->day_index() < kPacificDayIndex + 365 * 20);

  EXPECT_NE("", result.observation->parts().at("city").rappor().data());
  EXPECT_NE("", result.observation->parts().at("rating").basic_rappor().data());
}

// Tests the advanced API, when used incorretly.
TEST(EncoderEncoderTest, AdvancedApiWithErrors) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  FakeSystemData system_data;
  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret(), &system_data);

  std::unique_ptr<Encoder::Value> value(new Encoder::Value());

  // EncodingConfig 2 is String RAPPOR
  value->AddStringPart(kRapporEncodingId, "city", "San Francisco");

  // There is no metric 99.
  EXPECT_EQ(Encoder::kInvalidArguments, encoder.Encode(99, *value).status);

  // Metric 4 has two parts but value has only one part.
  EXPECT_EQ(Encoder::kInvalidArguments,
            encoder.Encode(kCityRatingMetricId, *value).status);

  // EncodingConfig 4 is Basic RAPPOR with integer categories.
  value->AddIntPart(kBasicStringRapporEncodingId, "rating", 1234);  // NOLINT
  value->AddIntPart(kBasicStringRapporEncodingId, "dummy", 1234);   // NOLINT

  // Metric 4 has two parts but value has three parts.
  EXPECT_EQ(Encoder::kInvalidArguments,
            encoder.Encode(kCityRatingMetricId, *value).status);

  value = std::make_unique<Encoder::Value>();
  value->AddStringPart(kRapporEncodingId, "city", "San Francisco");
  // "rating" is spelled wrong
  value->AddIntPart(kBasicIntRapporEncodingId, "ratingx", 1234);  // NOLINT
  EXPECT_EQ(Encoder::kInvalidArguments,
            encoder.Encode(kCityRatingMetricId, *value).status);

  value = std::make_unique<Encoder::Value>();
  value->AddStringPart(kRapporEncodingId, "city", "San Francisco");
  // "rating" has the wrong type
  value->AddStringPart(kBasicIntRapporEncodingId, "rating", "1234");
  EXPECT_EQ(Encoder::kInvalidArguments,
            encoder.Encode(kCityRatingMetricId, *value).status);

  value = std::make_unique<Encoder::Value>();
  value->AddStringPart(kRapporEncodingId, "city", "San Francisco");
  // There is no encoding_config 99.
  value->AddIntPart(99, "rating", 1234);  // NOLINT
  EXPECT_EQ(Encoder::kInvalidArguments,
            encoder.Encode(kCityRatingMetricId, *value).status);

  // Forculus does not accept integer values.
  value = std::make_unique<Encoder::Value>();
  value->AddIntPart(kForculusEncodingId, "Part1", 42);  // NOLINT
  EXPECT_EQ(Encoder::kInvalidArguments,
            encoder.Encode(kSingleIntMetricId, *value).status);

  // String RAPPOR does not accept integer values.
  value = std::make_unique<Encoder::Value>();
  value->AddIntPart(kRapporEncodingId, "Part1", 42);  // NOLINT
  EXPECT_EQ(Encoder::kInvalidArguments,
            encoder.Encode(kSingleIntMetricId, *value).status);

  // String RAPPOR does not accept blob values.
  value = std::make_unique<Encoder::Value>();
  value->AddBlobPart(kRapporEncodingId, "Part1",
                     reinterpret_cast<const void*>("1234"), 4);
  EXPECT_EQ(Encoder::kInvalidArguments,
            encoder.Encode(kSingleBlobMetricId, *value).status);

  // Basic RAPPOR does not accept blob values.
  value = std::make_unique<Encoder::Value>();
  value->AddBlobPart(kBasicStringRapporEncodingId, "Part1",
                     reinterpret_cast<const void*>("1234"), 4);
  EXPECT_EQ(Encoder::kInvalidArguments,
            encoder.Encode(kSingleBlobMetricId, *value).status);

  // Basic RAPPOR requires the value to be one of the candidates.
  value = std::make_unique<Encoder::Value>();
  value->AddStringPart(kBasicStringRapporEncodingId, "Part1", "San Francisco");
  EXPECT_EQ(Encoder::kInvalidArguments,
            encoder.Encode(kSingleStringMetricId, *value).status);

  // EncodingConfig 5 is an invalid Forculus config.
  value = std::make_unique<Encoder::Value>();
  value->AddStringPart(kInvalidForculusEncodingId, "Part1", "dummy");
  EXPECT_EQ(Encoder::kInvalidConfig,
            encoder.Encode(kSingleStringMetricId, *value).status);

  // EncodingConfig 6 is an invalid String RAPPOR config.
  value = std::make_unique<Encoder::Value>();
  value->AddStringPart(kInvalidRapporEncodingId, "Part1", "dummy");
  EXPECT_EQ(Encoder::kInvalidConfig,
            encoder.Encode(kSingleStringMetricId, *value).status);

  // Metric 5 is missing a time_zone_policy.
  value = std::make_unique<Encoder::Value>();
  value->AddStringPart(kForculusEncodingId, "Part1", "dummy");
  EXPECT_EQ(Encoder::kInvalidConfig,
            encoder.Encode(kNoTimeZoneMetricId, *value).status);
}

}  // namespace cobalt::encoder

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  INIT_LOGGING(argv[0]);
  return RUN_ALL_TESTS();
}

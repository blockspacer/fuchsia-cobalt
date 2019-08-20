// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/test_app2/test_app.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <google/protobuf/text_format.h>

#include "gflags/gflags.h"
#include "glog/logging.h"
#include "src/logger/logger_test_utils.h"
#include "src/logger/project_context.h"
#include "src/logger/project_context_factory.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

using logger::LoggerInterface;
using logger::ProjectContext;
using logger::ProjectContextFactory;
using logger::testing::FakeObservationStore;

DECLARE_uint32(num_clients);
DECLARE_string(values);

namespace {
constexpr char kErrorOccurredMetricName[] = "ErrorOccurred";

constexpr char kCobaltRegistry[] = R"(
customers {
  customer_name: "Fuchsia"
  customer_id: 1

  projects: {
    project_name: "Cobalt"
    project_id: 1

    metrics: {
      metric_name: "ErrorOccurred"
      metric_type: EVENT_OCCURRED
      customer_id: 1
      project_id: 1
      id: 1
      metric_dimensions: {
        dimension: "Event"
        max_event_code: 100
      }
      reports: {
        report_name: "ErrorCountsByType"
        id: 123
        report_type: SIMPLE_OCCURRENCE_COUNT
        local_privacy_noise_level: SMALL
      }
    }

    metrics: {
      metric_name: "CacheMiss"
      metric_type: EVENT_COUNT
      customer_id: 1
      project_id: 1
      id: 2
      reports: {
        report_name: "CacheMissCounts"
        id: 111
        report_type: EVENT_COMPONENT_OCCURRENCE_COUNT
      }
    }

    metrics: {
      metric_name: "update_duration"
      metric_type: ELAPSED_TIME
      customer_id: 1
      project_id: 1
      id: 3
      reports: {
        report_name: "update_duration_report"
        report_type: INT_RANGE_HISTOGRAM
        int_buckets: {
          exponential: {
            floor: 0
            num_buckets: 10
            initial_step: 1
            step_multiplier: 2
          }
        }
      }
    }

    metrics: {
      metric_name: "game_frame_rate"
      metric_type: FRAME_RATE
      customer_id: 1
      project_id: 1
      id: 4
      reports: {
        report_name: "game_frame_rate_histograms"
        report_type: INT_RANGE_HISTOGRAM
        int_buckets: {
          exponential: {
            floor: 0
            num_buckets: 10
            initial_step: 1000
            step_multiplier: 2
          }
        }
      }
    }

    metrics: {
      metric_name: "application_memory"
      metric_type: MEMORY_USAGE
      customer_id: 1
      project_id: 1
      id: 5
      reports: {
        report_name: "application_memory_histograms"
        report_type: INT_RANGE_HISTOGRAM
        int_buckets: {
          exponential: {
            floor: 0
            num_buckets: 10
            initial_step: 1000
            step_multiplier: 2
          }
        }
      }
    }

    metrics: {
      metric_name: "power_usage"
      metric_type: INT_HISTOGRAM
      customer_id: 1
      project_id: 1
      id: 6
      int_buckets: {
        linear: {
          floor: 0
          num_buckets: 50
          step_size: 2
        }
      }
      reports: {
        report_name: "power_usage_histograms"
        report_type: INT_RANGE_HISTOGRAM
      }
    }

    metrics: {
      metric_name: "FeaturesActive"
      metric_type: EVENT_OCCURRED
      customer_id: 1
      project_id: 1
      id: 7
      metric_dimensions: {
        dimension: "Feature"
        max_event_code: 9
      }
      reports: {
        report_name: "FeaturesActiveUniqueDevices"
        id: 301
        report_type: UNIQUE_N_DAY_ACTIVES
        local_privacy_noise_level: SMALL
        window_size: 1
        window_size: 7
      }
    }
  }
}

)";

// The number of locally aggregated Observations that should be generated for
// each day. Since Observation generation is faked here, this number does not
// need to correspond to a test Metric registry. It just needs to be a positive
// number.
constexpr int kNumAggregatedObservations = 20;

bool PopulateCobaltRegistry(CobaltRegistry* cobalt_registry) {
  google::protobuf::TextFormat::Parser parser;
  return parser.ParseFromString(kCobaltRegistry, cobalt_registry);
}

class TestLoggerFactory : public LoggerFactory {
 public:
  explicit TestLoggerFactory(const ProjectContext* project_context);

  std::unique_ptr<LoggerInterface> NewLogger(uint32_t day_index) override;

  const ProjectContext* project_context() override;

  size_t ObservationCount() override;

  void ResetObservationCount() override;

  void ResetLocalAggregation() override;

  bool GenerateAggregatedObservations(uint32_t day_index) override;

  bool SendAccumulatedObservations() override;

 private:
  const ProjectContext* project_context_;
  std::unique_ptr<FakeObservationStore> observation_store_;
  uint32_t last_obs_generation_;
};

TestLoggerFactory::TestLoggerFactory(const ProjectContext* project_context)
    : project_context_(project_context),
      observation_store_(new FakeObservationStore),
      last_obs_generation_(0) {}

std::unique_ptr<LoggerInterface> TestLoggerFactory::NewLogger(uint32_t day_index) {
  return nullptr;
}

size_t TestLoggerFactory::ObservationCount() {
  return observation_store_->num_observations_added();
}

void TestLoggerFactory::ResetObservationCount() {
  return observation_store_->ResetObservationCounter();
}

void TestLoggerFactory::ResetLocalAggregation() { last_obs_generation_ = 0; }

bool TestLoggerFactory::GenerateAggregatedObservations(uint32_t day_index) {
  if (day_index > last_obs_generation_) {
    for (int i = 0; i < kNumAggregatedObservations; i++) {
      if (observation_store::ObservationStore::kOk !=
          observation_store_->AddEncryptedObservation(std::make_unique<EncryptedMessage>(),
                                                      std::make_unique<ObservationMetadata>())) {
        return false;
      }
    }
    last_obs_generation_ = day_index;
  }
  return true;
}

bool TestLoggerFactory::SendAccumulatedObservations() { return true; }

const ProjectContext* TestLoggerFactory::project_context() { return project_context_; }

}  // namespace

// Tests of the TestApp class.
class TestAppTest : public ::testing::Test {
 public:
  void SetUp() {
    auto cobalt_registry = std::make_unique<CobaltRegistry>();
    ASSERT_TRUE(PopulateCobaltRegistry(cobalt_registry.get()));
    ProjectContextFactory project_context_factory(std::move(cobalt_registry));
    ASSERT_TRUE(project_context_factory.is_single_project());
    project_context_ = project_context_factory.TakeSingleProjectContext();
    std::unique_ptr<LoggerFactory> logger_factory(new TestLoggerFactory(project_context_.get()));
    test_app_.reset(new TestApp(std::move(logger_factory), kErrorOccurredMetricName,
                                TestApp::kInteractive, &output_stream_));
  }

 protected:
  // Clears the contents of the TestApp's output stream and returns the
  // contents prior to clearing.
  std::string ClearOutput() {
    std::string s = output_stream_.str();
    output_stream_.str("");
    return s;
  }

  // Does the current contents of the TestApp's output stream contain the
  // given text.
  bool OutputContains(const std::string text) {
    return std::string::npos != output_stream_.str().find(text);
  }

  // Is the TestApp's output stream curently empty?
  bool NoOutput() { return output_stream_.str().empty(); }

  std::unique_ptr<ProjectContext> project_context_;

  // The output stream that the TestApp has been given.
  std::ostringstream output_stream_;

  // The TestApp under test.
  std::unique_ptr<TestApp> test_app_;
};

//////////////////////////////////////
// Tests of interactive mode.
/////////////////////////////////////

// Tests ParseInt utility function.
TEST_F(TestAppTest, ParseInt) {
  int64_t x;
  // Test basic valid inputs.
  EXPECT_TRUE(test_app_->ParseInt("1", true, &x));
  EXPECT_EQ(x, 1);
  EXPECT_TRUE(test_app_->ParseInt("-3", true, &x));
  EXPECT_EQ(x, -3);
  EXPECT_TRUE(test_app_->ParseInt("503", true, &x));
  EXPECT_EQ(x, 503);
  EXPECT_TRUE(test_app_->ParseInt("1534", true, &x));
  EXPECT_EQ(x, 1534);
  ClearOutput();

  // Input should only contain one number.
  EXPECT_FALSE(test_app_->ParseInt("1 2", true, &x));
  EXPECT_TRUE(OutputContains("Expected positive integer instead of 1 2"));
  ClearOutput();

  // Input shouldn't contain non-numeric characters or floats.
  EXPECT_FALSE(test_app_->ParseInt("1.5asdf", true, &x));
  EXPECT_TRUE(OutputContains("Expected positive integer instead of 1.5asdf"));
  ClearOutput();
  EXPECT_FALSE(test_app_->ParseInt("$10#%", true, &x));
  EXPECT_TRUE(OutputContains("Expected positive integer instead of $10#%"));
  ClearOutput();
  EXPECT_FALSE(test_app_->ParseInt("10.0", true, &x));
  EXPECT_TRUE(OutputContains("Expected positive integer instead of 10.0"));
  ClearOutput();
}

// Tests ParseNonNegativeInt utility function.
TEST_F(TestAppTest, ParseNonNegativeInt) {
  int64_t x;
  // Test basic valid inputs.
  EXPECT_TRUE(test_app_->ParseNonNegativeInt("1", true, &x));
  EXPECT_EQ(x, 1);
  EXPECT_TRUE(test_app_->ParseNonNegativeInt("503", true, &x));
  EXPECT_EQ(x, 503);
  EXPECT_TRUE(test_app_->ParseNonNegativeInt("1534", true, &x));
  EXPECT_EQ(x, 1534);
  EXPECT_TRUE(test_app_->ParseNonNegativeInt("0", true, &x));
  EXPECT_EQ(x, 0);
  ClearOutput();

  // Negative numbers should return an error.
  EXPECT_FALSE(test_app_->ParseNonNegativeInt("-3", true, &x));
  EXPECT_TRUE(OutputContains("Expected non-negative integer instead of -3"));
  ClearOutput();

  // Input should only contain one number.
  EXPECT_FALSE(test_app_->ParseNonNegativeInt("1 2", true, &x));
  EXPECT_TRUE(OutputContains("Expected non-negative integer instead of 1 2"));
  ClearOutput();

  // Input shouldn't contain non-numeric characters or floats.
  EXPECT_FALSE(test_app_->ParseNonNegativeInt("1.5asdf", true, &x));
  EXPECT_TRUE(OutputContains("Expected non-negative integer instead of 1.5asdf"));
  ClearOutput();
  EXPECT_FALSE(test_app_->ParseNonNegativeInt("$10#%", true, &x));
  EXPECT_TRUE(OutputContains("Expected non-negative integer instead of $10#%"));
  ClearOutput();
  EXPECT_FALSE(test_app_->ParseNonNegativeInt("10.0", true, &x));
  EXPECT_TRUE(OutputContains("Expected non-negative integer instead of 10.0"));
  ClearOutput();
}

// Tests ParseFloat utility function.
TEST_F(TestAppTest, ParseFloat) {
  float f;
  // Test basic valid inputs.
  EXPECT_TRUE(test_app_->ParseFloat("1.5", true, &f));
  EXPECT_EQ(f, static_cast<float>(1.5));
  EXPECT_TRUE(test_app_->ParseFloat("-2.3", true, &f));
  EXPECT_EQ(f, static_cast<float>(-2.3));
  EXPECT_TRUE(test_app_->ParseFloat("503.9", true, &f));
  EXPECT_EQ(f, static_cast<float>(503.9));
  EXPECT_TRUE(test_app_->ParseFloat("1534.0", true, &f));
  EXPECT_EQ(f, static_cast<float>(1534.0));
  EXPECT_TRUE(test_app_->ParseFloat("0", true, &f));
  EXPECT_EQ(f, static_cast<float>(0));
  EXPECT_TRUE(test_app_->ParseFloat("100", true, &f));
  EXPECT_EQ(f, static_cast<float>(100));
  ClearOutput();

  // Input should only contain one number.
  EXPECT_FALSE(test_app_->ParseFloat("1.5 2.0", true, &f));
  EXPECT_TRUE(OutputContains("Expected float instead of 1.5 2.0"));
  ClearOutput();

  // Input shouldn't contain non-numeric characters.
  EXPECT_FALSE(test_app_->ParseFloat("1.5asdf", true, &f));
  EXPECT_TRUE(OutputContains("Expected float instead of 1.5asdf"));
  ClearOutput();
  EXPECT_FALSE(test_app_->ParseFloat("$10#%", true, &f));
  EXPECT_TRUE(OutputContains("Expected float instead of $10#%"));
  ClearOutput();
}

// Tests ParseIndex utility function.
TEST_F(TestAppTest, ParseIndex) {
  uint32_t x;
  // Test basic valid inputs.
  EXPECT_TRUE(test_app_->ParseIndex("index=1", &x));
  EXPECT_EQ(x, (uint32_t)1);
  EXPECT_TRUE(test_app_->ParseIndex("index=503", &x));
  EXPECT_EQ(x, (uint32_t)503);
  EXPECT_TRUE(test_app_->ParseIndex("index=1534", &x));
  EXPECT_EQ(x, (uint32_t)1534);
  ClearOutput();

  // Input should contain 'index='.
  EXPECT_FALSE(test_app_->ParseIndex("1", &x));
  ClearOutput();

  // Input should only contain one element.
  EXPECT_FALSE(test_app_->ParseIndex("index=1 2", &x));
  EXPECT_TRUE(OutputContains("Expected small non-negative integer instead of 1 2"));
  ClearOutput();

  // Input shouldn't contain non-numeric characters or floats.
  EXPECT_FALSE(test_app_->ParseIndex("index=1.5asdf", &x));
  EXPECT_TRUE(OutputContains("Expected small non-negative integer instead of 1.5asdf"));
  ClearOutput();
  EXPECT_FALSE(test_app_->ParseIndex("index=$10#%", &x));
  EXPECT_TRUE(OutputContains("Expected small non-negative integer instead of $10#%"));
  ClearOutput();
  EXPECT_FALSE(test_app_->ParseIndex("index=10.0", &x));
  EXPECT_TRUE(OutputContains("Expected small non-negative integer instead of 10.0"));
  ClearOutput();
}

// Tests ParseDay utility function.
TEST_F(TestAppTest, ParseDay) {
  uint32_t d;
  uint32_t today = test_app_->CurrentDayIndex();
  // Test basic valid inputs.
  EXPECT_TRUE(test_app_->ParseDay("day=42", &d));
  EXPECT_EQ(d, (uint32_t)42);
  EXPECT_TRUE(test_app_->ParseDay("day=today", &d));
  EXPECT_EQ(d, today);
  EXPECT_TRUE(test_app_->ParseDay("day=today-1", &d));
  EXPECT_EQ(d, today - 1);
  EXPECT_TRUE(test_app_->ParseDay("day=today+1", &d));
  EXPECT_EQ(d, today + 1);
  ClearOutput();

  // Input should start with 'day='.
  EXPECT_FALSE(test_app_->ParseDay("1", &d));
  EXPECT_TRUE(OutputContains("Expected prefix 'day='."));
  ClearOutput();

  // Input should contain only one element.
  EXPECT_FALSE(test_app_->ParseDay("day=1 2", &d));
  EXPECT_FALSE(test_app_->ParseDay("day=today 2", &d));
  ClearOutput();

  // Input shouldn't contain non-numerical characters other than prefixes
  // "day=", "day=today", "day=today+", or "day=today-"
  EXPECT_FALSE(test_app_->ParseDay("day=yesterday", &d));
  EXPECT_TRUE(OutputContains("Expected small non-negative integer instead of yesterday."));
  ClearOutput();
  EXPECT_FALSE(test_app_->ParseDay("day=today+two", &d));
  EXPECT_TRUE(OutputContains("Expected non-negative integer instead of two."));
  ClearOutput();

  // Disallow input of the form "day=today-N" with N greater than today's day
  // index.
  EXPECT_FALSE(test_app_->ParseDay("day=today-20000000000", &d));
  EXPECT_TRUE(OutputContains("Negative offset cannot be larger than the current day index."));
  ClearOutput();
}

// Tests processing a bad command line.
TEST_F(TestAppTest, ProcessCommandLineBad) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("this is not a command"));
  EXPECT_TRUE(OutputContains("Unrecognized command: this"));
}

// Tests processing the "help" command
TEST_F(TestAppTest, ProcessCommandLineHelp) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("help"));
  // We don't want to test the actual output too rigorously because that would
  // be a very fragile test. Just doing a sanity test.
  EXPECT_TRUE(OutputContains("Print this help message."));
}

// Tests processing a bad set command line.
TEST_F(TestAppTest, ProcessCommandLineSetBad) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("set"));
  EXPECT_TRUE(OutputContains("Malformed set command."));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set a b c"));
  EXPECT_TRUE(OutputContains("Malformed set command."));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set a b"));
  EXPECT_TRUE(OutputContains("a is not a settable parameter"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric b"));
  EXPECT_TRUE(OutputContains("There is no metric named 'b'"));
  EXPECT_TRUE(OutputContains("Current metric unchanged."));
  ClearOutput();
}

// Tests processing the set and ls commands
TEST_F(TestAppTest, ProcessCommandLineSetAndLs) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("ls"));
  EXPECT_TRUE(OutputContains("Metric: 'ErrorOccurred'"));
  EXPECT_TRUE(OutputContains("Customer: Fuchsia"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric CacheMiss"));
  EXPECT_TRUE(OutputContains("Metric set."));

  EXPECT_TRUE(test_app_->ProcessCommandLine("ls"));
  EXPECT_TRUE(OutputContains("Metric: 'CacheMiss'"));
  EXPECT_TRUE(OutputContains("Customer: Fuchsia"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric update_duration"));
  EXPECT_TRUE(OutputContains("Metric set."));

  EXPECT_TRUE(test_app_->ProcessCommandLine("ls"));
  EXPECT_TRUE(OutputContains("Metric: 'update_duration'"));
  EXPECT_TRUE(OutputContains("Customer: Fuchsia"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric game_frame_rate"));
  EXPECT_TRUE(OutputContains("Metric set."));

  EXPECT_TRUE(test_app_->ProcessCommandLine("ls"));
  EXPECT_TRUE(OutputContains("Metric: 'game_frame_rate'"));
  EXPECT_TRUE(OutputContains("Customer: Fuchsia"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric application_memory"));
  EXPECT_TRUE(OutputContains("Metric set."));

  EXPECT_TRUE(test_app_->ProcessCommandLine("ls"));
  EXPECT_TRUE(OutputContains("Metric: 'application_memory'"));
  EXPECT_TRUE(OutputContains("Customer: Fuchsia"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric power_usage"));
  EXPECT_TRUE(OutputContains("Metric set."));

  EXPECT_TRUE(test_app_->ProcessCommandLine("ls"));
  EXPECT_TRUE(OutputContains("Metric: 'power_usage'"));
  EXPECT_TRUE(OutputContains("Customer: Fuchsia"));
  ClearOutput();
}

// Tests processing a bad show command line.
TEST_F(TestAppTest, ProcessCommandLineShowBad) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("show"));
  EXPECT_TRUE(OutputContains("Expected 'show config'."));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("show confi"));
  EXPECT_TRUE(OutputContains("Expected 'show config'."));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("show config foo"));
  EXPECT_TRUE(OutputContains("Expected 'show config'."));
  ClearOutput();
}

// Tests processing the set and show config commands
TEST_F(TestAppTest, ProcessCommandLineSetAndShowConfig) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("show config"));
  EXPECT_TRUE(OutputContains("metric_name: \"ErrorOccurred\""));
  EXPECT_TRUE(OutputContains("metric_type: EVENT_OCCURRED"));
  EXPECT_TRUE(OutputContains("report_name: \"ErrorCountsByType\""));
  EXPECT_TRUE(OutputContains("report_type: SIMPLE_OCCURRENCE_COUNT"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric CacheMiss"));
  EXPECT_TRUE(OutputContains("Metric set."));

  EXPECT_TRUE(test_app_->ProcessCommandLine("show config"));
  EXPECT_TRUE(OutputContains("metric_name: \"CacheMiss\""));
  EXPECT_TRUE(OutputContains("metric_type: EVENT_COUNT"));
  EXPECT_TRUE(OutputContains("report_name: \"CacheMissCounts\""));
  EXPECT_TRUE(OutputContains("report_type: EVENT_COMPONENT_OCCURRENCE_COUNT"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric update_duration"));
  EXPECT_TRUE(OutputContains("Metric set."));

  EXPECT_TRUE(test_app_->ProcessCommandLine("show config"));
  EXPECT_TRUE(OutputContains("metric_name: \"update_duration\""));
  EXPECT_TRUE(OutputContains("metric_type: ELAPSED_TIME"));
  EXPECT_TRUE(OutputContains("report_name: \"update_duration_report\""));
  EXPECT_TRUE(OutputContains("report_type: INT_RANGE_HISTOGRAM"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric game_frame_rate"));
  EXPECT_TRUE(OutputContains("Metric set."));

  EXPECT_TRUE(test_app_->ProcessCommandLine("show config"));
  EXPECT_TRUE(OutputContains("metric_name: \"game_frame_rate\""));
  EXPECT_TRUE(OutputContains("metric_type: FRAME_RATE"));
  EXPECT_TRUE(OutputContains("report_name: \"game_frame_rate_histograms\""));
  EXPECT_TRUE(OutputContains("report_type: INT_RANGE_HISTOGRAM"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric application_memory"));
  EXPECT_TRUE(OutputContains("Metric set."));

  EXPECT_TRUE(test_app_->ProcessCommandLine("show config"));
  EXPECT_TRUE(OutputContains("metric_name: \"application_memory\""));
  EXPECT_TRUE(OutputContains("metric_type: MEMORY_USAGE"));
  EXPECT_TRUE(OutputContains("report_name: \"application_memory_histograms\""));
  EXPECT_TRUE(OutputContains("report_type: INT_RANGE_HISTOGRAM"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric power_usage"));
  EXPECT_TRUE(OutputContains("Metric set."));

  EXPECT_TRUE(test_app_->ProcessCommandLine("show config"));
  EXPECT_TRUE(OutputContains("metric_name: \"power_usage\""));
  EXPECT_TRUE(OutputContains("metric_type: INT_HISTOGRAM"));
  EXPECT_TRUE(OutputContains("report_name: \"power_usage_histograms\""));
  EXPECT_TRUE(OutputContains("report_type: INT_RANGE_HISTOGRAM"));
  ClearOutput();
}

// Tests processing a bad log command line.
TEST_F(TestAppTest, ProcessCommandLineEncodeBad) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("log"));
  EXPECT_TRUE(OutputContains("Malformed log command."));
  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log foo"));
  EXPECT_TRUE(OutputContains("Expected non-negative integer instead of foo."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log -1"));
  EXPECT_TRUE(OutputContains("Expected non-negative integer instead of -1."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 0"));
  EXPECT_TRUE(OutputContains("Malformed log command. <num> must be positive: 0"));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 3.14 bar"));
  EXPECT_TRUE(OutputContains("Expected non-negative integer instead of 3.14."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 100"));
  EXPECT_TRUE(
      OutputContains("Malformed log command. Expected log method to be specified "
                     "after <num>."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 100 foo"));
  EXPECT_TRUE(OutputContains("Unrecognized log method specified: foo"));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 100 event"));
  EXPECT_TRUE(
      OutputContains("Malformed log event command. Expected one more "
                     "argument for <event_code>."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 100 event foo bar baz"));
  EXPECT_TRUE(OutputContains("Malformed log event command: too many arguments."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 100 event_count"));
  EXPECT_TRUE(
      OutputContains("Malformed log event_count command: missing at least one "
                     "required argument."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 100 event_count foo bar baz two three four"));
  EXPECT_TRUE(OutputContains("Malformed log event_count command: too many arguments."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 100 event foo"));
  EXPECT_TRUE(OutputContains("Expected non-negative integer instead of foo."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric update_duration"));
  EXPECT_TRUE(OutputContains("Metric set."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 100 elapsed_time foo bar"));
  EXPECT_TRUE(
      OutputContains("Malformed log elapsed_time command. Expected 3 "
                     "additional parameters."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric game_frame_rate"));
  EXPECT_TRUE(OutputContains("Metric set."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 100 frame_rate foo bar"));
  EXPECT_TRUE(
      OutputContains("Malformed log frame_rate command. Expected 3 "
                     "additional parameters."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric application_memory"));
  EXPECT_TRUE(OutputContains("Metric set."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 100 memory_usage foo bar"));
  EXPECT_TRUE(
      OutputContains("Malformed log memory_usage command. Expected 3 "
                     "additional parameters."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric power_usage"));
  EXPECT_TRUE(OutputContains("Metric set."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("log 100 int_histogram foo bar"));
  EXPECT_TRUE(
      OutputContains("Malformed log int_histogram command. Expected 4 "
                     "additional parameters."));
}

// Tests processing of valid generate command lines.
TEST_F(TestAppTest, ProcessCommandLineGenerate) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("generate"));
  EXPECT_TRUE(OutputContains("Generated"));
  EXPECT_TRUE(OutputContains("locally aggregated observations for day index"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("generate day=today"));
  EXPECT_TRUE(OutputContains("Generated"));
  EXPECT_TRUE(OutputContains("locally aggregated observations for day index"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("generate day=today-5"));
  EXPECT_TRUE(OutputContains("Generated"));
  EXPECT_TRUE(OutputContains("locally aggregated observations for day index"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("generate day=today+5"));
  EXPECT_TRUE(OutputContains("Generated"));
  EXPECT_TRUE(OutputContains("locally aggregated observations for day index"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("generate day=150000"));
  EXPECT_TRUE(OutputContains("Generated"));
  EXPECT_TRUE(OutputContains("locally aggregated observations for day index 150000"));
  ClearOutput();
}

// Tests processing a bad generate command line.
TEST_F(TestAppTest, ProcessCommandLineGenerateBad) {
  // generate takes at most 1 argument.
  EXPECT_TRUE(test_app_->ProcessCommandLine("generate foo bar"));
  EXPECT_TRUE(OutputContains("Malformed generate command: too many arguments."));
  ClearOutput();
}

// Tests processing of a valid reset-aggregation command line.
TEST_F(TestAppTest, ProcessCommandLineResetAggregation) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("reset-aggregation"));
  EXPECT_TRUE(OutputContains("Reset local aggregation."));
  ClearOutput();
}

// Tests processing a bad send command line.
TEST_F(TestAppTest, ProcessCommandLineSendBad) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("send foo"));
  EXPECT_TRUE(OutputContains("The send command doesn't take any arguments."));
}

// Tests some minimal behavior related to local aggregation:
// (1) The response to a valid generate command should include the number of
// generated observations and the day index
// (2) generate should not produce observations twice for a given day index
// unless reset-aggregation is run before the second attempt
TEST_F(TestAppTest, GenerateAndReset) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("generate day=15000"));
  std::ostringstream stream;
  stream << "Generated " << kNumAggregatedObservations
         << " locally aggregated observations for day index 15000";
  EXPECT_TRUE(OutputContains(stream.str()));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("generate day=15000"));
  EXPECT_TRUE(OutputContains("Generated 0 locally aggregated observations for day index 15000"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("generate day=14999"));
  EXPECT_TRUE(OutputContains("Generated 0 locally aggregated observations for day index 14999"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("reset-aggregation"));
  EXPECT_TRUE(OutputContains("Reset local aggregation."));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("generate day=15000"));
  EXPECT_TRUE(OutputContains(stream.str()));
  ClearOutput();
}

}  // namespace cobalt

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  return RUN_ALL_TESTS();
}

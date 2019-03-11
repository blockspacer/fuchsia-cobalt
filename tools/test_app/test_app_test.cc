// Copyright 2017 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#import "tools/test_app/test_app.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "config/config_text_parser.h"
#include "encoder/project_context.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

DECLARE_uint32(num_clients);
DECLARE_string(values);

using config::EncodingRegistry;
using config::MetricRegistry;
using encoder::EnvelopeMaker;
using encoder::ProjectContext;
using encoder::ShufflerClientInterface;
using encoder::SystemData;

namespace {
const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;

const char* const kMetricConfigText = R"(
# Metric one string part named url.
element {
  customer_id: 1
  project_id: 1
  id: 1
  name: "Fuchsia Popular URLs"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "url"
    value {
      description: "A URL."
      data_type: STRING
    }
  }
}

# Metric 2 has one integer part named hour.
element {
  customer_id: 1
  project_id: 1
  id: 2
  name: "Fuchsia Usage by Hour"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "hour"
    value {
      description: "An integer from 0 to 23 representing the hour of the day."
      data_type: INT
    }
  }
}

# Metric 3 has one string part named "fruit" and one int part named "rating".
element {
  customer_id: 1
  project_id: 1
  id: 3
  name: "Fuchsia Fruit Consumption and Rating"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "fruit"
    value {
      description: "The name of a fruit that was consumed."
    }
  }
  parts {
    key: "rating"
    value {
      description: "An integer from 0 to 10"
      data_type: INT
    }
  }
}

# Metric 4 has one INDEX part named event.
element {
  customer_id: 1
  project_id: 1
  id: 4
  name: "Fuchsia Rare Events"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "event"
    value {
      description: "The index of the event type."
      data_type: INDEX
    }
  }
}

)";

const char* const kEncodingConfigText = R"(
# EncodingConfig 1 is Forculus, 20.
element {
  customer_id: 1
  project_id: 1
  id: 1
  forculus {
    threshold: 20
    epoch_type: DAY
  }
}

# EncodingConfig 2 is Basic RAPPOR with integer categories [0, 23]
element {
  customer_id: 1
  project_id: 1
  id: 2
  basic_rappor {
    prob_0_becomes_1: 0.1
    prob_1_stays_1: 0.9
    int_range_categories: {
      first: 0
      last:  23
    }
  }
}

# EncodingConfig 3 is Basic RAPPOR with String categories for fruit types.
element {
  customer_id: 1
  project_id: 1
  id: 3
  basic_rappor {
    prob_0_becomes_1: 0.01
    prob_1_stays_1: 0.99
    string_categories: {
      category: "apple"
      category: "banana"
      category: "cantaloupe"
    }
  }
}

# EncodingConfig 4 is Basic RAPPOR with integer categories in [0, 10]
element {
  customer_id: 1
  project_id: 1
  id: 4
  basic_rappor {
    prob_0_becomes_1: 0.2
    prob_1_stays_1: 0.8
    int_range_categories: {
      first: 0
      last:  10
    }
  }
}

# EncodingConfig 5 is Basic RAPPOR with 100 indexed categories
element {
  customer_id: 1
  project_id: 1
  id: 5
  basic_rappor {
    prob_0_becomes_1: 0.2
    prob_1_stays_1: 0.8
    indexed_categories: {
      num_categories: 100
    }
  }
}

)";

// Returns a ProjectContext obtained by parsing the above configuration text
// strings.
std::shared_ptr<ProjectContext> GetTestProject() {
  // Parse the metric config string
  auto metric_parse_result =
      config::FromString<RegisteredMetrics>(kMetricConfigText, nullptr);
  EXPECT_EQ(config::kOK, metric_parse_result.second);
  std::shared_ptr<MetricRegistry> metric_registry(
      metric_parse_result.first.release());

  // Parse the encoding config string
  auto encoding_parse_result =
      config::FromString<RegisteredEncodings>(kEncodingConfigText, nullptr);
  EXPECT_EQ(config::kOK, encoding_parse_result.second);
  std::shared_ptr<EncodingRegistry> encoding_registry(
      encoding_parse_result.first.release());

  return std::shared_ptr<ProjectContext>(new ProjectContext(
      kCustomerId, kProjectId, metric_registry, encoding_registry));
}

// An implementation of ShufflerClientInterface that just stores the
// Envelope for inspection by a test.
class FakeShufflerClient : public ShufflerClientInterface {
 public:
  FakeShufflerClient() : decrypter_("") {}

  grpc::Status SendToShuffler(const EncryptedMessage& encrypted_message,
                              grpc::ClientContext* context = nullptr) override {
    EXPECT_TRUE(decrypter_.DecryptMessage(encrypted_message, &envelope));
    return grpc::Status::OK;
  }
  Envelope envelope;

 private:
  cobalt::util::MessageDecrypter decrypter_;
};

// Parses the |ciphertext| field of the given EncryptedMessage assuming
// it contains the unencrypted serialized bytes of an Observation.
Observation ParseUnencryptedObservation(const EncryptedMessage& em) {
  Observation observation;
  EXPECT_EQ(EncryptedMessage::NONE, em.scheme());
  observation.ParseFromString(em.ciphertext());
  return observation;
}

}  // namespace

// Tests of the TestApp class.
class TestAppTest : public ::testing::Test {
 public:
  TestAppTest()
      : fake_shuffler_client_(new FakeShufflerClient()),
        test_app_(new TestApp(
            GetTestProject(), fake_shuffler_client_,
            std::unique_ptr<SystemData>(), TestApp::kInteractive,
            util::EncryptedMessageMaker::MakeUnencrypted(),
            util::EncryptedMessageMaker::MakeUnencrypted(), &output_stream_)) {}

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

  std::shared_ptr<FakeShufflerClient> fake_shuffler_client_;

  // The output stream that the TestApp has been given.
  std::ostringstream output_stream_;

  // The TestApp under test.
  std::unique_ptr<TestApp> test_app_;
};

//////////////////////////////////////
// Tests of interactive mode.
/////////////////////////////////////

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
  EXPECT_TRUE(
      OutputContains("Encode <num> independent copies of the string "
                     "or integer value <val>, or index <n> if"));
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
  EXPECT_TRUE(OutputContains("Expected positive integer instead of b."));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set encoding b"));
  EXPECT_TRUE(OutputContains("Expected positive integer instead of b."));
  ClearOutput();
}

// Tests processing the set and ls commands
TEST_F(TestAppTest, ProcessCommandLineSetAndLs) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("ls"));
  EXPECT_TRUE(OutputContains("Metric ID: 1"));
  EXPECT_TRUE(OutputContains("Encoding Config ID: 1"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric 2"));
  EXPECT_TRUE(NoOutput());

  EXPECT_TRUE(test_app_->ProcessCommandLine("ls"));
  EXPECT_TRUE(OutputContains("Metric ID: 2"));
  EXPECT_TRUE(OutputContains("Encoding Config ID: 1"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set encoding 2"));
  EXPECT_TRUE(NoOutput());

  EXPECT_TRUE(test_app_->ProcessCommandLine("ls"));
  EXPECT_TRUE(OutputContains("Metric ID: 2"));
  EXPECT_TRUE(OutputContains("Encoding Config ID: 2"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("ls"));
  EXPECT_TRUE(OutputContains("Metric ID: 2"));
  EXPECT_TRUE(OutputContains("Encoding Config ID: 2"));
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
  EXPECT_TRUE(OutputContains("Fuchsia Popular URLs"));
  EXPECT_TRUE(OutputContains("One string part named \"url\": A URL."));
  EXPECT_TRUE(OutputContains("Forculus threshold=20"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric 2"));
  EXPECT_TRUE(NoOutput());

  EXPECT_TRUE(test_app_->ProcessCommandLine("show config"));
  EXPECT_TRUE(OutputContains("Fuchsia Usage by Hour"));
  EXPECT_TRUE(
      OutputContains("One int part named \"hour\": An integer from 0 to 23 "
                     "representing the hour of the day."));
  EXPECT_TRUE(OutputContains("Forculus threshold=20"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set encoding 2"));
  EXPECT_TRUE(NoOutput());

  EXPECT_TRUE(test_app_->ProcessCommandLine("show config"));
  EXPECT_TRUE(OutputContains("Fuchsia Usage by Hour"));
  EXPECT_TRUE(
      OutputContains("One int part named \"hour\": An integer from 0 to 23 "
                     "representing the hour of the day."));
  EXPECT_TRUE(OutputContains("Basic Rappor"));
  EXPECT_TRUE(OutputContains("p=0.1, q=0.9"));
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric 3"));
  EXPECT_TRUE(test_app_->ProcessCommandLine("set encoding 3"));
  EXPECT_TRUE(NoOutput());

  EXPECT_TRUE(test_app_->ProcessCommandLine("show config"));
  EXPECT_TRUE(OutputContains("Fuchsia Fruit Consumption and Rating"));
  EXPECT_TRUE(
      OutputContains("One int part named \"rating\": An integer from 0 to 10"));
  EXPECT_TRUE(
      OutputContains("One string part named \"fruit\": The name of a fruit "
                     "that was consumed."));
  EXPECT_TRUE(OutputContains("Basic Rappor"));
  EXPECT_TRUE(OutputContains("p=0.01, q=0.99")) << output_stream_.str();
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric 4"));
  EXPECT_TRUE(test_app_->ProcessCommandLine("set encoding 5"));
  EXPECT_TRUE(NoOutput());

  EXPECT_TRUE(test_app_->ProcessCommandLine("show config"));
  EXPECT_TRUE(OutputContains("Fuchsia Rare Events")) << output_stream_.str();
  EXPECT_TRUE(OutputContains(
      "One indexed part named \"event\": The index of the event type"));
  EXPECT_TRUE(OutputContains("Basic Rappor"));
  EXPECT_TRUE(OutputContains("p=0.2, q=0.8"));
  EXPECT_TRUE(OutputContains("Categories:"));
  EXPECT_TRUE(OutputContains("num_categories: 100")) << output_stream_.str();
  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric 5"));
  EXPECT_TRUE(test_app_->ProcessCommandLine("set encoding 6"));
  EXPECT_TRUE(NoOutput());

  EXPECT_TRUE(test_app_->ProcessCommandLine("show config"));
  EXPECT_TRUE(OutputContains("There is no metric with id=5."));
  EXPECT_TRUE(OutputContains("There is no encoding config with id=6."));
  ClearOutput();
}

// Tests processing a bad encode command line.
TEST_F(TestAppTest, ProcessCommandLineEncodeBad) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("encode"));
  EXPECT_TRUE(OutputContains("Malformed encode command."));
  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("encode foo"));
  EXPECT_TRUE(OutputContains("Malformed encode command."));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("encode foo bar"));
  EXPECT_TRUE(OutputContains("Expected positive integer instead of foo."));

  ClearOutput();

  EXPECT_TRUE(test_app_->ProcessCommandLine("encode -1 bar"));
  EXPECT_TRUE(OutputContains("<num> must be a positive integer: -1"));

  ClearOutput();
  EXPECT_TRUE(test_app_->ProcessCommandLine("encode 3.14 bar"));
  EXPECT_TRUE(OutputContains("Expected positive integer instead of 3.14."));
}

// Tests processing a bad send command line.
TEST_F(TestAppTest, ProcessCommandLineSendBad) {
  EXPECT_TRUE(test_app_->ProcessCommandLine("send foo"));
  EXPECT_TRUE(OutputContains("The send command doesn't take any arguments."));
}

// Tests processing an encode and send operation.
TEST_F(TestAppTest, ProcessCommandLineEncodeAndSend) {
  // The default is metric 1 encoding 1 which is Forculus with
  // URLs.
  EXPECT_TRUE(test_app_->ProcessCommandLine("encode 19 www.AAAA"));
  EXPECT_TRUE(test_app_->ProcessCommandLine("encode 20 www.BBBB"));
  EXPECT_TRUE(test_app_->ProcessCommandLine("send"));
  EXPECT_TRUE(NoOutput());
  // The received envelope should contain 1 batch.
  Envelope& envelope = fake_shuffler_client_->envelope;
  ASSERT_EQ(1, envelope.batch_size());
  // That batch should contain 39 messages.
  const ObservationBatch& batch = envelope.batch(0);
  EXPECT_EQ(39, batch.encrypted_observation_size());
  // The metric ID should be the default value of 1.
  EXPECT_EQ(1u, batch.meta_data().metric_id());
  // All of the Observations should have a single part named "url" that has an
  // encoding config ID of the default value of 1.
  for (const auto& encrypted_message : batch.encrypted_observation()) {
    auto observation = ParseUnencryptedObservation(encrypted_message);
    EXPECT_EQ(1, observation.parts_size());
    auto part = observation.parts().at("url");
    EXPECT_EQ(1u, part.encoding_config_id());
  }

  // Switch to metric 2 encoding 2 which is Basic RAPPOR with
  // hours-of-the-day.
  EXPECT_TRUE(test_app_->ProcessCommandLine("set encoding 2"));
  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric 2"));
}

// Tests processing a multi-encode and send operation.
TEST_F(TestAppTest, ProcessCommandLineMultiEncodeAndSend) {
  // The default is metric is 1.
  EXPECT_TRUE(test_app_->ProcessCommandLine("encode 19 url:www.AAAA:1"));
  EXPECT_TRUE(test_app_->ProcessCommandLine("encode 20 url:www.BBBB:1"));
  EXPECT_TRUE(test_app_->ProcessCommandLine("send"));
  EXPECT_TRUE(NoOutput());
  // The received envelope should contain 1 batch.
  Envelope& envelope = fake_shuffler_client_->envelope;
  ASSERT_EQ(1, envelope.batch_size());
  // That batch should contain 39 messages.
  const ObservationBatch& batch = envelope.batch(0);
  EXPECT_EQ(39, batch.encrypted_observation_size());
  // The metric ID should be the default value of 1.
  EXPECT_EQ(1u, batch.meta_data().metric_id());
  // All of the Observations should have a single part named "url" that has an
  // encoding config ID of the default value of 1.
  for (const auto& encrypted_message : batch.encrypted_observation()) {
    auto observation = ParseUnencryptedObservation(encrypted_message);
    EXPECT_EQ(1, observation.parts_size());
    auto part = observation.parts().at("url");
    EXPECT_EQ(1u, part.encoding_config_id());
  }

  // Switch to metric 3 which is fruit rating.
  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric 3"));

  // Encode 100 instances of rating apple as 10 using encoding configs
  // 3 and 4 respectively.
  EXPECT_TRUE(
      test_app_->ProcessCommandLine("encode 100 fruit:apple:3 rating:10:4"));
  // Encode 200 instances of rating banana as 7 using encoding configs
  // 3 and 4 respectively.
  EXPECT_TRUE(
      test_app_->ProcessCommandLine("encode 200 fruit:banana:3 rating:7:4"));
  // Send
  EXPECT_TRUE(test_app_->ProcessCommandLine("send"));
  EXPECT_TRUE(NoOutput());
  envelope = fake_shuffler_client_->envelope;
  ASSERT_EQ(1, envelope.batch_size());
  // That batch should contain 300 messages.
  const ObservationBatch& batch2 = envelope.batch(0);
  EXPECT_EQ(300, batch2.encrypted_observation_size());
  // The metric ID should be 3.
  EXPECT_EQ(3u, batch2.meta_data().metric_id());
  // All of the Observations should have two parts named fruit and rating.
  for (const auto& encrypted_message : batch2.encrypted_observation()) {
    auto observation = ParseUnencryptedObservation(encrypted_message);
    EXPECT_EQ(2, observation.parts_size());
    auto fruit_part = observation.parts().at("fruit");
    auto rating_part = observation.parts().at("rating");
    EXPECT_EQ(3u, fruit_part.encoding_config_id());
    EXPECT_EQ(4u, rating_part.encoding_config_id());
  }
}

// Tests processing an encode and send operation with more than one
// batch in the envelope.
TEST_F(TestAppTest, ProcessCommandLineEncodeAndSendMulti) {
  // The default is metric 1 encoding 1 which is Forculus with
  // URLs.
  EXPECT_TRUE(test_app_->ProcessCommandLine("encode 19 www.AAAA"));
  EXPECT_TRUE(test_app_->ProcessCommandLine("encode 20 www.BBBB"));
  EXPECT_TRUE(NoOutput());

  // Notice we do not send!

  // Switch to metric 2 encoding 2 which is Basic RAPPOR with
  // hours-of-the-day.
  EXPECT_TRUE(test_app_->ProcessCommandLine("set encoding 2"));
  EXPECT_TRUE(test_app_->ProcessCommandLine("set metric 2"));

  EXPECT_TRUE(test_app_->ProcessCommandLine("encode 100 8"));
  EXPECT_TRUE(test_app_->ProcessCommandLine("encode 200 9"));

  // Now we send.
  EXPECT_TRUE(test_app_->ProcessCommandLine("send"));
  EXPECT_TRUE(NoOutput());

  // The received envelope should contain 2 batches.
  Envelope& envelope = fake_shuffler_client_->envelope;
  ASSERT_EQ(2, envelope.batch_size());

  std::map<int32_t, int32_t> metric_batch_map;
  int32_t i = 0;
  for (const auto& env : envelope.batch()) {
    metric_batch_map[env.meta_data().metric_id()] = i++;
  }
  // The first batch should contain 39 messages.
  const ObservationBatch& batch = envelope.batch(metric_batch_map[1]);
  EXPECT_EQ(39, batch.encrypted_observation_size());
  // The metric ID should be the default value of 1.
  EXPECT_EQ(1u, batch.meta_data().metric_id());
  // All of the Observations should have a single part named "url" that has an
  // encoding config ID of the default value of 1.
  for (const auto& encrypted_message : batch.encrypted_observation()) {
    auto observation = ParseUnencryptedObservation(encrypted_message);
    EXPECT_EQ(1, observation.parts_size());
    auto part = observation.parts().at("url");
    EXPECT_EQ(1u, part.encoding_config_id());
  }

  // The second batch should contain 300 messages.
  const ObservationBatch& batch2 = envelope.batch(metric_batch_map[2]);
  EXPECT_EQ(300, batch2.encrypted_observation_size());
  // The metric ID should be 2.
  EXPECT_EQ(2u, batch2.meta_data().metric_id());
  // All of the Observations should have a single part named "hour" that has an
  // encoding config ID of 2
  for (const auto& encrypted_message : batch2.encrypted_observation()) {
    auto observation = ParseUnencryptedObservation(encrypted_message);
    EXPECT_EQ(1, observation.parts_size());
    auto part = observation.parts().at("hour");
    EXPECT_EQ(2u, part.encoding_config_id());
  }
}

// Tests processing the "quit" command
TEST_F(TestAppTest, ProcessCommandLineQuit) {
  EXPECT_FALSE(test_app_->ProcessCommandLine("quit"));
  EXPECT_TRUE(NoOutput());
}

//////////////////////////////////////
// Tests of send-once mode.
/////////////////////////////////////

// Tests the Run() method in send-once mode.
TEST_F(TestAppTest, RunSendAndQuit) {
  // Reconstruct TestApp in send-once mode.
  test_app_.reset(new TestApp(
      GetTestProject(), fake_shuffler_client_, std::unique_ptr<SystemData>(),
      TestApp::kSendOnce, util::EncryptedMessageMaker::MakeUnencrypted(),
      util::EncryptedMessageMaker::MakeUnencrypted(), &output_stream_));
  test_app_->set_metric(3);
  FLAGS_num_clients = 31;
  FLAGS_values = "fruit:apple:3,rating:10:4";
  test_app_->Run();
  EXPECT_TRUE(NoOutput());
  // The envelope should contain a single batch.
  Envelope& envelope = fake_shuffler_client_->envelope;
  ASSERT_EQ(1, envelope.batch_size());

  // That batch should contain 31 messages.
  const ObservationBatch& batch = envelope.batch(0);
  EXPECT_EQ(31, batch.encrypted_observation_size());
  // The metric ID should be 3.
  EXPECT_EQ(3u, batch.meta_data().metric_id());

  // All of the Observations should have two parts named fruit and rating.
  for (const auto& encrypted_message : batch.encrypted_observation()) {
    auto observation = ParseUnencryptedObservation(encrypted_message);
    EXPECT_EQ(2, observation.parts_size());
    auto fruit_part = observation.parts().at("fruit");
    auto rating_part = observation.parts().at("rating");
    EXPECT_EQ(3u, fruit_part.encoding_config_id());
    EXPECT_EQ(4u, rating_part.encoding_config_id());
  }
}

// Tests the Run() method in send-once mode with invalid flags.
TEST_F(TestAppTest, RunSendAndQuitBad) {
  // Reconstruct TestApp in send-once mode.
  test_app_.reset(new TestApp(
      GetTestProject(), fake_shuffler_client_, std::unique_ptr<SystemData>(),
      TestApp::kSendOnce, util::EncryptedMessageMaker::MakeUnencrypted(),
      util::EncryptedMessageMaker::MakeUnencrypted(), &output_stream_));
  test_app_->set_metric(3);

  // Misspell "fruit"
  FLAGS_values = "fruits:apple:3,rating:10:4";
  test_app_->Run();
  // The envelope should be empty.
  Envelope& envelope = fake_shuffler_client_->envelope;
  ASSERT_EQ(0, envelope.batch_size());

  // Misspell "apple"
  FLAGS_values = "fruit:apples:3,rating:10:4";
  test_app_->Run();
  // The envelope should be empty.
  envelope = fake_shuffler_client_->envelope;
  ASSERT_EQ(0, envelope.batch_size());

  // Write "x" in place of "3"
  FLAGS_values = "fruit:apple:x,rating:10:4";
  test_app_->Run();
  // The envelope should be empty.
  envelope = fake_shuffler_client_->envelope;
  ASSERT_EQ(0, envelope.batch_size());

  // Write "-3" in place of "3"
  FLAGS_values = "fruit:apple:-3,rating:10:4";
  test_app_->Run();
  // The envelope should be empty.
  envelope = fake_shuffler_client_->envelope;
  ASSERT_EQ(0, envelope.batch_size());

  // Miss the comma.
  FLAGS_values = "fruit:apple:3 rating:10:4";
  test_app_->Run();
  // The envelope should be empty.
  envelope = fake_shuffler_client_->envelope;
  ASSERT_EQ(0, envelope.batch_size());

  // Miss the third part of the second triple
  FLAGS_values = "fruit:apple:3,rating:10:";
  test_app_->Run();
  // The envelope should be empty.
  envelope = fake_shuffler_client_->envelope;
  ASSERT_EQ(0, envelope.batch_size());
}

}  // namespace cobalt

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  return RUN_ALL_TESTS();
}

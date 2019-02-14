// Copyright 2016 The Fuchsia Authors
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
#include "analyzer/analyzer_service/analyzer_service.h"

#include <map>
#include <string>
#include <vector>

#include "analyzer/store/bigtable_store.h"
#include "analyzer/store/memory_store.h"
#include "config/metric_config.h"
#include "util/encrypted_message_util.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

using google::protobuf::Empty;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

namespace cobalt {
namespace analyzer {

using config::SystemProfileFields;
using store::DataStore;
using store::MemoryStore;
using store::MemoryStoreSingleton;
using store::ObservationStore;

const int kAnalyzerPort = 8080;

// Fixture to start and stop the Analyzer service.
class AnalyzerServiceTest : public ::testing::Test {
 public:
  AnalyzerServiceTest()
      : data_store_(new MemoryStore()),
        observation_store_(new ObservationStore(data_store_)),
        analyzer_(observation_store_, kAnalyzerPort,
                  grpc::InsecureServerCredentials(), "") {}

 protected:
  virtual void SetUp() {
    data_store_->DeleteAllRows(store::DataStore::kObservations);
    analyzer_.Start();
  }

  virtual void TearDown() {
    analyzer_.Shutdown();
    analyzer_.Wait();
  }

  std::shared_ptr<DataStore> data_store_;
  std::shared_ptr<ObservationStore> observation_store_;
  AnalyzerServiceImpl analyzer_;
};

// We connect to the analyzer service and send a test RPC and assert the result
TEST_F(AnalyzerServiceTest, TestGRPC) {
  char dst[1024];
  ClientContext context;

  // Connect to the analyzer
  snprintf(dst, sizeof(dst), "localhost:%d", kAnalyzerPort);

  std::shared_ptr<Channel> chan =
      grpc::CreateChannel(dst, grpc::InsecureChannelCredentials());

  std::unique_ptr<Analyzer::Stub> analyzer(Analyzer::NewStub(chan));

  static const uint32_t kCustomerId = 1;
  static const uint32_t kProjectId = 1;
  static const uint32_t kMetricId = 1;
  static const char kPartName[] = "part1";

  // Build an Observation.
  ObservationPart observation_part;
  observation_part.set_encoding_config_id(12345);
  Observation observation;
  (*observation.mutable_parts())[kPartName] = std::move(observation_part);

  // Build an ObservationBatch to hold the Observation.
  ObservationBatch observation_batch;
  ObservationMetadata* meta_data = observation_batch.mutable_meta_data();
  meta_data->set_customer_id(kCustomerId);
  meta_data->set_project_id(kProjectId);
  meta_data->set_metric_id(kMetricId);
  meta_data->set_day_index(1);

  // Write the Observation into an EncryptedMessage and add it to the batch.
  EncryptedMessage* encrypted_observation =
      observation_batch.add_encrypted_observation();
  auto maker = util::EncryptedMessageMaker::MakeUnencrypted();
  maker->Encrypt(observation, encrypted_observation);

  // Execute the RPC
  Empty resp;
  grpc::Status status =
      analyzer->AddObservations(&context, observation_batch, &resp);
  ASSERT_TRUE(status.ok());

  // Query the ObservationStore
  std::vector<std::string> parts;
  SystemProfileFields included_system_profile_fields = {};
  auto query_response = observation_store_->QueryObservations(
      kCustomerId, kProjectId, kMetricId, 0, UINT32_MAX, parts,
      included_system_profile_fields, UINT32_MAX, "");
  ASSERT_EQ(store::kOK, query_response.status);

  // There should be one Observation in the response.
  ASSERT_EQ(1u, query_response.results.size());

  // It should have day_index = 1;
  ASSERT_EQ(1u, query_response.results[0].metadata.day_index());

  // It should have one part
  ASSERT_EQ(1u, query_response.results[0].observation.parts().size());

  // That part should have the correct encoding_config_id
  ASSERT_EQ(12345u, query_response.results[0]
                        .observation.parts()
                        .at("part1")
                        .encoding_config_id());
}

}  // namespace analyzer
}  // namespace cobalt

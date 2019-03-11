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

// An application that acts as a Cobalt client for the purposes of testing,
// debugging and demonstration.
//
// It embeds the Encoder library, encodes values, forms Envelopes, and sends the
// Envelopes to the Shuffler. It can also skip the Shuffler and send
// ObservationBatches directly to he Analyzer.
//
// The application can be used in three modes controlled by the -mode flag:
// - interactive: The program runs an interactive command-loop.
// - send-once: The program sends a single Envelope described by flags.
// - automatic: The program runs forever sending many Envelopes with randomly
//              generated values.

#ifndef COBALT_TOOLS_TEST_APP_TEST_APP_H_
#define COBALT_TOOLS_TEST_APP_TEST_APP_H_

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "analyzer/analyzer_service/analyzer.grpc.pb.h"
#include "encoder/envelope_maker.h"
#include "encoder/memory_observation_store.h"
#include "encoder/project_context.h"
#include "encoder/send_retryer.h"
#include "encoder/shipping_manager.h"
#include "encoder/shuffler_client.h"
#include "encoder/system_data.h"

namespace cobalt {

// The Cobalt testing client application.
class TestApp {
 public:
  static std::unique_ptr<TestApp> CreateFromFlagsOrDie(int argc, char* argv[]);

  // Modes of operation of the Cobalt test application. An instance of
  // TestApp is in interactive mode unless set_mode() is invoked. set_mode()
  // is invoked from CreateFromFlagsOrDie() in order to set the mode to the
  // one specified by the -mode flag.
  enum Mode {
    // In this mode the TestApp is controlled via an interactive command-line
    // loop.
    kInteractive = 0,

    // In this mode the TestApp sends a single RPC to the Shuffler.
    kSendOnce = 1,

    // In this mode the TestApp loops forever generating random Observations and
    // sending many RPCs to the Shuffler.
    kAutomatic = 2
  };

  // Constructor. The |ostream| is used for emitting output in interactive mode.
  TestApp(std::shared_ptr<encoder::ProjectContext> project_context,
          std::shared_ptr<encoder::ShufflerClientInterface> shuffler_client,
          std::unique_ptr<encoder::SystemData> system_data, Mode mode,
          std::unique_ptr<util::EncryptedMessageMaker> analyzer_encrypter,
          std::unique_ptr<util::EncryptedMessageMaker> shuffler_encrypter,
          std::ostream* ostream);

  void set_metric(uint32_t metric_id) { metric_ = metric_id; }

  // Run() is invoked by main(). It invokes either CommandLoop(),
  // SendAndQuit(), or RunAutomatic() depending on the mode.
  void Run();

  // Processes a single command. This is used in interactive mode. The
  // method is public so an instance of TestApp may be used as a library
  // and driven from another program. We use this in unit tests.
  // Returns false if an only if the specified command is "quit".
  bool ProcessCommandLine(const std::string command_line);

 private:
  // Implements interactive mode.
  void CommandLoop();

  // Implements send-once mode.
  void SendAndQuit();

  // Implements automatic mode.
  void RunAutomatic();

  // Generates FLAGS_num_clients independent Observations by encoding the
  // multi-part value specified by the arguments and adds the Observations
  // to the EnvelopeMaker.
  void Encode(const std::vector<uint32_t> encoding_config_ids,
              const std::vector<std::string>& metric_parts,
              const std::vector<std::string>& values);

  // Generates a new ClientSecret, constructs a new Encoder using that secret,
  // uses this Encoder to encode the multi-part value specified by the
  // arguments, and adds the resulting Observation to the EnvelopeMaker.
  bool EncodeAsNewClient(const std::vector<uint32_t> encoding_config_ids,
                         const std::vector<std::string>& metric_parts,
                         const std::vector<std::string>& values);

  // Generates FLAGS_num_clients independent Observations by encoding the
  // string value specified by the argument and adds the Observations
  // to the EnvelopeMaker.
  void EncodeString(const std::string value);

  // Generates a new ClientSecret, constructs a new Encoder using that secret,
  // uses this Encoder to encode the string value specified by the
  // argument, and adds the resulting Observation to the EnvelopeMaker.
  bool EncodeStringAsNewClient(const std::string value);

  // Generates FLAGS_num_clients independent Observations by encoding the
  // int value specified by the argument and adds the Observations
  // to the EnvelopeMaker.
  void EncodeInt(int64_t value);

  // Generates a new ClientSecret, constructs a new Encoder using that secret,
  // uses this Encoder to encode the int value specified by the
  // argument, and adds the resulting Observation to the EnvelopeMaker.
  bool EncodeIntAsNewClient(int64_t value);

  // Generates FLAGS_num_clients independent Observations by encoding the
  // given |index| and adds the Observations to the EnvelopeMaker.
  void EncodeIndex(uint32_t index);

  // Generates a new ClientSecret, constructs a new Encoder using that secret,
  // uses this Encoder to encode the given |index|, and adds the resulting
  // Observation to the EnvelopeMaker.
  bool EncodeIndexAsNewClient(uint32_t index);

  void SendAccumulatedObservations();
  void SendToShuffler();

  bool ProcessCommand(const std::vector<std::string>& command);

  void Encode(const std::vector<std::string>& command);

  void EncodeMulti(const std::vector<std::string>& command);

  void ListParameters();

  void SetParameter(const std::vector<std::string>& command);

  void Send(const std::vector<std::string>& command);

  void Show(const std::vector<std::string>& command);

  void ShowMetric(const Metric& metric);

  void ShowEncodingConfig(const EncodingConfig& encoding);

  void ShowForculusConfig(const ForculusConfig& config);

  void ShowRapporConfig(const RapporConfig& config);

  void ShowBasicRapporConfig(const BasicRapporConfig& config);

  bool ParseInt(const std::string& str, bool complain, int64_t* x);

  bool ParseIndex(const std::string& str, uint32_t* index);

  // Parses a string of the form <part>:<value>:<encoding> and writes <part>
  // into |part_name| and <value> into |value| and <encoding> into
  // encoding_config_id. Returns true if and only if this succeeds.
  bool ParsePartValueEncodingTriple(const std::string& triple,
                                    std::string* part_name, std::string* value,
                                    uint32_t* encoding_config_id);

  // Determines whether or not |str| is a triple of the kind that may be
  // parsed by ParsePartValueEncodingTriple.
  bool IsTriple(const std::string str);

  uint32_t customer_id_ = 1;
  uint32_t project_id_ = 1;
  uint32_t encoding_config_id_ = 1;
  uint32_t metric_ = 1;
  // The TestApp is in interactive mode unless set_mode() is invoked.
  Mode mode_ = kInteractive;
  std::shared_ptr<encoder::ProjectContext> project_context_;
  std::shared_ptr<encoder::ShufflerClientInterface> shuffler_client_;
  std::unique_ptr<encoder::send_retryer::SendRetryer> send_retryer_;
  std::unique_ptr<encoder::SystemData> system_data_;
  std::unique_ptr<util::EncryptedMessageMaker> encrypt_to_shuffler_;
  std::unique_ptr<util::EncryptedMessageMaker> encrypt_to_analyzer_;
  std::unique_ptr<encoder::MemoryObservationStore> observation_store_;
  std::unique_ptr<encoder::LegacyShippingManager> shipping_manager_;
  std::ostream* ostream_;
};

}  // namespace cobalt

#endif  // COBALT_TOOLS_TEST_APP_TEST_APP_H_

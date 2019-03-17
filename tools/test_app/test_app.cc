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

#include "tools/test_app/test_app.h"

#include <libgen.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "./observation.pb.h"
#include "analyzer/analyzer_service/analyzer.grpc.pb.h"
#include "config/cobalt_registry.pb.h"
#include "config/encoding_config.h"
#include "encoder/encoder.h"
#include "encoder/envelope_maker.h"
#include "encoder/memory_observation_store.h"
#include "encoder/project_context.h"
#include "encoder/send_retryer.h"
#include "encoder/shuffler_client.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "grpc++/grpc++.h"
#include "util/clearcut/curl_http_client.h"
#include "util/file_util.h"

namespace cobalt {

using analyzer::Analyzer;
using config::ClientConfig;
using config::EncodingRegistry;
using config::MetricRegistry;
using encoder::ClearcutV1ShippingManager;
using encoder::ClientSecret;
using encoder::Encoder;
using encoder::EnvelopeMaker;
using encoder::LegacyShippingManager;
using encoder::MemoryObservationStore;
using encoder::ObservationStore;
using encoder::ProjectContext;
using encoder::send_retryer::SendRetryer;
using encoder::ShippingManager;
using encoder::ShufflerClient;
using encoder::ShufflerClientInterface;
using encoder::SystemData;
using google::protobuf::Empty;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using shuffler::Shuffler;
using util::EncryptedMessageMaker;

// There are three modes of operation of the Cobalt TestClient program
// determined by the value of this flag.
// - interactive: The program runs an interactive command-loop.
// - send-once: The program sends a single Envelope described by flags.
// - automatic: The program runs forever sending many Envelopes with randomly
//              generated values.
//
// This flag is read in CreateFromFlagsOrDie() and used to invoke
// set_mode().
DEFINE_string(mode, "interactive",
              "This program may be used in 3 modes: 'interactive', "
              "'send-once', 'automatic'");

// The remainder of the flags fall into three categories.

// Category 1: Flags read by CreateFromFlagsOrDie() that set immutable
// values used in all three modes.
DEFINE_uint32(customer, 1, "Customer ID");
DEFINE_uint32(project, 1, "Project ID");
DEFINE_string(analyzer_uri, "",
              "The URI of the Analyzer. Necessary only if sending observations "
              "to the Analyzer.");
DEFINE_string(shuffler_uri, "",
              "The URI of the Shuffler. Necessary only if sending observations "
              "to the Shuffler.");
DEFINE_string(analyzer_pk_pem_file, "",
              "Path to a file containing a PEM encoding of the public key of "
              "the Analyzer used for Cobalt's internal encryption scheme. If "
              "not specified then no encryption will be used.");
DEFINE_string(shuffler_pk_pem_file, "",
              "Path to a file containing a PEM encoding of the public key of "
              "the Shuffler used for Cobalt's internal encryption scheme. If "
              "not specified then no encryption will be used.");
DEFINE_bool(
    use_tls, false,
    "Should tls be used for the connection to the shuffler or the analyzer?");
DEFINE_string(root_certs_pem_file, "",
              "Full path to a file containing a PEM encoding of the TLS root "
              "certificates to be used by the gRPC client.");
DEFINE_uint32(deadline_seconds, 10, "RPC deadline.");
DEFINE_string(config_bin_proto_path, "",
              "Path to the serialized CobaltRegistry proto from which the "
              "configuration is to be read. (Optional)");

// Category 2: Flags consumed by CreateFromFlagsOrDie() that set values that
// may be overidden by a set command in interactive mode.
DEFINE_uint32(metric, 1, "Metric ID to use.");

// Category 3: Flags used only in send-once or automatic modes. These are not
// consumed by CreateFromFlagsOrDie().
DEFINE_uint32(num_clients, 1,
              "Number of clients to simulate in the non-interactive modes.");
DEFINE_string(
    values, "",
    "A comma-separated list of colon-delimited triples of the form"
    " <part>:<val>:<encoding> where <part> is the name of a metric part and"
    " <val> is a string or int value and <encoding> is an EncodingConfig id."
    " Used only in send-once mode to specify the multi-part value to"
    " encode and the encodings to use.");

DEFINE_uint32(repeat, 1,
              "Number of times to repeat the add-send cycle in the "
              "non-interactive mode.");

DEFINE_uint32(num_adds_per_observation, 1,
              "Number of times each Observation should be added to the "
              "envelope. Setting this to more than 1 allows us to test "
              "idempotency.");

DEFINE_string(override_board_name, "",
              "A board name to override the default one");
DEFINE_string(clearcut_endpoint, "https://jmt17.google.com/log",
              "The URL to send clearcut requests to.");

namespace {

const size_t kMaxBytesPerObservation = 100 * 1024;
const size_t kMaxBytesPerEnvelope = 1024 * 1024;
const size_t kMaxBytesTotal = 10 * 1024 * 1024;
const std::chrono::seconds kInitialRpcDeadline(FLAGS_deadline_seconds);
const std::chrono::seconds kDeadlinePerSendAttempt(60);

// Prints help for the interactive mode.
void PrintHelp(std::ostream* ostream) {
  *ostream << std::endl;
  *ostream << "Cobalt command-line testing client" << std::endl;
  *ostream << "----------------------------------" << std::endl;
  *ostream << "help                     \tPrint this help message."
           << std::endl;
  *ostream << "encode <num> <val>       \tEncode <num> independent copies "
              "of the string or integer value <val>, or index <n> if "
              "<val>='index=<n>'"
           << std::endl;
  *ostream << std::endl;
  *ostream << "encode <num> <part>:<val>:<encoding> "
              "<part>:<val>:<encoding>..."
           << std::endl;
  *ostream << "                         \tEncode <num> independent copies of "
              "a multi-part value. Each <part> is a part name."
           << std::endl;
  *ostream << "                         \tEach <val> is an int or string "
              "value or an index <n> if <val>='index=<n>'."
           << std::endl;
  *ostream << "                         \tEach <encoding> is an EncodingConfig "
              "id."
           << std::endl;
  *ostream << std::endl;
  *ostream << "ls                       \tList current values of "
              "parameters."
           << std::endl;
  *ostream << "send                     \tSend all previously encoded "
              "observations and clear the observation cache."
           << std::endl;
  *ostream << "set encoding <id>        \tSet encoding config id." << std::endl;
  *ostream << "set metric <id>          \tSet metric id." << std::endl;
  *ostream << "show config              \tDisplay the current Metric and "
              "Encoding configurations."
           << std::endl;
  *ostream << "quit                     \tQuit." << std::endl;
  *ostream << std::endl;
}

// Returns the path to the standard Cobalt configuration based on the presumed
// location of this binary.
std::string FindCobaltRegistryProto(char* argv[]) {
  char path[PATH_MAX], path2[PATH_MAX];

  // Get the directory of this binary.
  if (!realpath(argv[0], path)) {
    LOG(FATAL) << "realpath(): " << argv[0];
  }
  char* dir = dirname(path);
  // Set the relative path to the registry.
  snprintf(path2, sizeof(path2),
           "%s/../../config/third_party/config/cobalt_config.binproto", dir);

  // Get the absolute path to the registry.
  if (!realpath(path2, path)) {
    LOG(FATAL) << "Computed path to serialized CobaltRegistry is invalid: "
               << path;
  }

  return path;
}

// Parses the mode flag.
TestApp::Mode ParseMode() {
  if (FLAGS_mode == "interactive") {
    return TestApp::kInteractive;
  }
  if (FLAGS_mode == "send-once") {
    return TestApp::kSendOnce;
  }
  if (FLAGS_mode == "automatic") {
    return TestApp::kAutomatic;
  }
  LOG(FATAL) << "Unrecognized mode: " << FLAGS_mode;
}

// Reads the specified serialized CobaltRegistry proto. Returns a ProjectContext
// containing the read config and the values of the -customer and
// -project flags.
std::shared_ptr<ProjectContext> LoadProjectContext(
    const std::string& config_bin_proto_path) {
  VLOG(2) << "Loading Cobalt configuration from " << config_bin_proto_path;

  std::ifstream config_file_stream;
  config_file_stream.open(config_bin_proto_path);
  CHECK(config_file_stream)
      << "Could not open cobalt config proto file: " << config_bin_proto_path;

  // Parse the cobalt config file.
  cobalt::CobaltRegistry cobalt_config;
  CHECK(cobalt_config.ParseFromIstream(&config_file_stream))
      << "Could not parse the cobalt config proto file: "
      << config_bin_proto_path;

  // Load the encoding registry.
  cobalt::RegisteredEncodings registered_encodings;
  registered_encodings.mutable_element()->Swap(
      cobalt_config.mutable_encoding_configs());
  auto encodings = EncodingRegistry::TakeFrom(&registered_encodings, nullptr);
  if (encodings.second != config::kOK) {
    LOG(FATAL) << "Can't load encodings configuration";
  }
  std::shared_ptr<EncodingRegistry> encoding_registry(
      encodings.first.release());

  // Load the metrics registry.
  cobalt::RegisteredMetrics registered_metrics;
  registered_metrics.mutable_element()->Swap(
      cobalt_config.mutable_metric_configs());
  auto metrics = MetricRegistry::TakeFrom(&registered_metrics, nullptr);
  if (metrics.second != config::kOK) {
    LOG(FATAL) << "Can't load metrics configuration";
  }
  std::shared_ptr<MetricRegistry> metric_registry(metrics.first.release());

  CHECK(FLAGS_project < 100) << "-project=" << FLAGS_project
                             << " not allowed. Project ID must be less than "
                                "100 because this tool is not "
                                "intended to mutate real customer projects.";

  return std::shared_ptr<ProjectContext>(new ProjectContext(
      FLAGS_customer, FLAGS_project,
      std::make_shared<ClientConfig>(encoding_registry, metric_registry)));
}

// Given a |line| of text, breaks it into tokens separated by white space.
std::vector<std::string> Tokenize(const std::string& line) {
  std::istringstream line_stream(line);
  std::vector<std::string> tokens;
  do {
    std::string token;
    line_stream >> token;
    std::remove(token.begin(), token.end(), ' ');
    if (!token.empty()) {
      tokens.push_back(token);
    }
  } while (line_stream);
  return tokens;
}

// Given a |line| of text, breaks it into cells separated by commas.
std::vector<std::string> ParseCSV(const std::string& line) {
  std::stringstream line_stream(line);
  std::vector<std::string> cells;

  std::string cell;
  while (std::getline(line_stream, cell, ',')) {
    if (!cell.empty()) {
      cells.push_back(cell);
    }
  }
  return cells;
}

template <class T>
std::string ToString(std::vector<T> v) {
  std::ostringstream stream;
  for (const T& i : v) {
    stream << i << " ";
  }
  return stream.str();
}

}  // namespace

void TestApp::SendToShuffler() {
  if (!shuffler_client_) {
    if (mode_ == TestApp::kInteractive) {
      std::cout << "The flag -shuffler_uri must be specified." << std::endl;
    } else {
      LOG(ERROR) << "-shuffler_uri was not specified.";
    }
    return;
  }

  if (mode_ != TestApp::kInteractive) {
    VLOG(2) << "Sending to shuffler with deadline = " << FLAGS_deadline_seconds
            << " seconds...";
  }
  if (mode_ == TestApp::kAutomatic) {
    // In automatic mode, let the ShippingManager send to the Shuffler
    // asynchronously.
    return;
  }
  shipping_manager_->RequestSendSoon();
  shipping_manager_->WaitUntilIdle(kDeadlinePerSendAttempt);
  auto status = shipping_manager_->last_send_status();
  if (status.ok()) {
    if (mode_ == TestApp::kInteractive) {
      std::cout << "Sent to Shuffler." << std::endl;
    } else {
      VLOG(2) << "Sent to Shuffler";
    }
  } else {
    if (mode_ == TestApp::kInteractive) {
      std::cout << "Send to shuffler failed with status=" << status.error_code()
                << " " << status.error_message() << std::endl;
    } else {
      LOG(ERROR) << "Send to shuffler failed with status="
                 << status.error_code() << " " << status.error_message();
    }
  }
}

std::unique_ptr<TestApp> TestApp::CreateFromFlagsOrDie(int argc, char* argv[]) {
  std::string config_bin_proto_path = FLAGS_config_bin_proto_path;
  // If no path is given, try to deduce it from the binary location.
  if (config_bin_proto_path == "") {
    config_bin_proto_path = FindCobaltRegistryProto(argv);
  }

  std::shared_ptr<encoder::ProjectContext> project_context =
      LoadProjectContext(config_bin_proto_path);

  CHECK(!FLAGS_shuffler_uri.empty()) << "You must specify -shuffler_uri";

  auto mode = ParseMode();
  std::shared_ptr<encoder::ShufflerClient> shuffler_client;
  if (!FLAGS_shuffler_uri.empty()) {
    VLOG(2) << "Connecting to Shuffler at " << FLAGS_shuffler_uri;
    const char* pem_root_certs = nullptr;
    std::string pem_root_certs_str;
    if (FLAGS_use_tls) {
      VLOG(2) << "Using TLS.";
      if (!FLAGS_root_certs_pem_file.empty()) {
        VLOG(2) << "Reading root certs from " << FLAGS_root_certs_pem_file;
        auto read_result =
            cobalt::util::ReadNonEmptyTextFile(FLAGS_root_certs_pem_file);
        CHECK(read_result.ok());
        pem_root_certs_str = read_result.ValueOrDie();
        pem_root_certs = pem_root_certs_str.c_str();
      }
    } else {
      VLOG(2) << "NOT using TLS.";
    }
    shuffler_client.reset(
        new ShufflerClient(FLAGS_shuffler_uri, FLAGS_use_tls, pem_root_certs));
  }

  std::unique_ptr<EncryptedMessageMaker> analyzer_encrypter;
  if (FLAGS_analyzer_pk_pem_file.empty()) {
    VLOG(2) << "WARNING: Encryption of Observations to the Analzyer not being "
               "used. Pass the flag -analyzer_pk_pem_file";
    analyzer_encrypter = EncryptedMessageMaker::MakeUnencrypted();
  } else {
    auto key_value =
        cobalt::util::ReadNonEmptyTextFile(FLAGS_analyzer_pk_pem_file)
            .ValueOrDie();
    analyzer_encrypter =
        EncryptedMessageMaker::MakeHybridEcdh(key_value).ValueOrDie();
  }

  std::unique_ptr<EncryptedMessageMaker> shuffler_encrypter;
  if (FLAGS_shuffler_pk_pem_file.empty()) {
    VLOG(2) << "WARNING: Encryption of Envelopes to the Shuffler not being "
               "used. Pass the flag -shuffler_pk_pem_file";
    shuffler_encrypter = EncryptedMessageMaker::MakeUnencrypted();
  } else {
    auto key_value =
        cobalt::util::ReadNonEmptyTextFile(FLAGS_shuffler_pk_pem_file)
            .ValueOrDie();
    shuffler_encrypter =
        EncryptedMessageMaker::MakeHybridEcdh(key_value).ValueOrDie();
  }

  std::unique_ptr<SystemData> system_data(new SystemData("test_app"));
  if (!FLAGS_override_board_name.empty()) {
    SystemProfile profile;
    profile.set_os(SystemProfile::FUCHSIA);
    profile.set_arch(SystemProfile::X86_64);
    profile.set_board_name(FLAGS_override_board_name);
    system_data->OverrideSystemProfile(profile);
  }

  auto test_app = std::unique_ptr<TestApp>(
      new TestApp(project_context, shuffler_client, std::move(system_data),
                  mode, std::move(analyzer_encrypter),
                  std::move(shuffler_encrypter), &std::cout));
  test_app->set_metric(FLAGS_metric);
  return test_app;
}

TestApp::TestApp(
    std::shared_ptr<ProjectContext> project_context,
    std::shared_ptr<encoder::ShufflerClientInterface> shuffler_client,
    std::unique_ptr<encoder::SystemData> system_data, Mode mode,
    std::unique_ptr<util::EncryptedMessageMaker> analyzer_encrypter,
    std::unique_ptr<util::EncryptedMessageMaker> shuffler_encrypter,
    std::ostream* ostream)
    : customer_id_(project_context->customer_id()),
      project_id_(project_context->project_id()),
      mode_(mode),
      project_context_(project_context),
      shuffler_client_(shuffler_client),
      send_retryer_(new SendRetryer(shuffler_client_.get())),
      system_data_(std::move(system_data)),
      encrypt_to_shuffler_(std::move(shuffler_encrypter)),
      encrypt_to_analyzer_(std::move(analyzer_encrypter)),
      observation_store_(new MemoryObservationStore(
          kMaxBytesPerObservation, kMaxBytesPerEnvelope, kMaxBytesTotal)),
      ostream_(ostream) {
  // By using (kMaxSeconds, 0) here we are effectively putting the
  // ShippingManager in manual mode. It will never send
  // automatically and it will send immediately in response to
  // RequestSendSoon().
  auto schedule_params = encoder::UploadScheduler(
      encoder::UploadScheduler::kMaxSeconds, std::chrono::seconds(0));
  if (mode_ == TestApp::kAutomatic) {
    // In automatic mode, let the ShippingManager send to the Shuffler
    // every 10 seconds.
    schedule_params = encoder::UploadScheduler(std::chrono::seconds(10),
                                               std::chrono::seconds(1));
  }
  shipping_manager_.reset(new LegacyShippingManager(
      schedule_params, observation_store_.get(), encrypt_to_shuffler_.get(),
      LegacyShippingManager::SendRetryerParams(kInitialRpcDeadline,
                                               kDeadlinePerSendAttempt),
      send_retryer_.get()));
  shipping_manager_->Start();
}

void TestApp::Run() {
  switch (mode_) {
    case kInteractive:
      CommandLoop();
      break;
    case kSendOnce:
      SendAndQuit();
      break;
    case kAutomatic:
      RunAutomatic();
      break;
  }
}

void TestApp::RunAutomatic() {
  while (true) {
    ProcessCommandLine("encode 100 www.google.com");
    ProcessCommandLine("send");
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

void TestApp::SendAndQuit() {
  VLOG(1) << "--values=" << FLAGS_values;
  auto value_triples = ParseCSV(FLAGS_values);
  if (value_triples.empty()) {
    LOG(ERROR) << "--values was not set.";
    return;
  }

  std::vector<std::string> part_names;
  std::vector<std::string> values;
  std::vector<uint32_t> encoding_config_ids;
  for (const auto& triple : value_triples) {
    part_names.emplace_back();
    values.emplace_back();
    encoding_config_ids.emplace_back();
    if (!ParsePartValueEncodingTriple(triple, &part_names.back(),
                                      &values.back(),
                                      &encoding_config_ids.back())) {
      LOG(ERROR)
          << "Malformed <part>:<value>:<encoding> triple in --values flag: "
          << triple;
      return;
    }
  }

  for (uint32_t i = 0; i < FLAGS_repeat; i++) {
    VLOG(2) << "encoding_config_ids=" << ToString(encoding_config_ids)
            << " part_names=" << ToString(part_names)
            << " values=" << ToString(values);
    Encode(encoding_config_ids, part_names, values);

    SendAccumulatedObservations();
  }
}

void TestApp::SendAccumulatedObservations() { SendToShuffler(); }

void TestApp::CommandLoop() {
  std::string command_line;
  while (true) {
    *ostream_ << "Command or 'help': ";
    getline(std::cin, command_line);
    if (!ProcessCommandLine(command_line)) {
      break;
    }
  }
}

// Generates FLAGS_num_clients independent Observations by encoding the
// multi-part value specified by the arguments and adds the Observations
// to the EnvelopeMaker.
void TestApp::Encode(const std::vector<uint32_t> encoding_config_ids,
                     const std::vector<std::string>& metric_parts,
                     const std::vector<std::string>& values) {
  for (size_t i = 0; i < FLAGS_num_clients; i++) {
    if (!EncodeAsNewClient(encoding_config_ids, metric_parts, values)) {
      break;
    }
  }
}

// Generates a new ClientSecret, constructs a new Encoder using that secret,
// uses this Encoder to encode the multi-part value specified by the
// arguments, and adds the resulting Observation to the EnvelopeMaker.
bool TestApp::EncodeAsNewClient(const std::vector<uint32_t> encoding_config_ids,
                                const std::vector<std::string>& metric_parts,
                                const std::vector<std::string>& values) {
  size_t num_parts = metric_parts.size();
  CHECK_EQ(num_parts, values.size());
  CHECK_EQ(num_parts, encoding_config_ids.size());

  // Build the |Value|.
  Encoder::Value value;
  for (size_t i = 0; i < num_parts; i++) {
    int64_t int_val;
    uint32_t index;
    if (ParseInt(values[i], false, &int_val)) {
      value.AddIntPart(encoding_config_ids[i], metric_parts[i], int_val);
    } else if (ParseIndex(values[i], &index)) {
      value.AddIndexPart(encoding_config_ids[i], metric_parts[i], index);
    } else {
      value.AddStringPart(encoding_config_ids[i], metric_parts[i], values[i]);
    }
  }

  // Construct a new Encoder.
  std::unique_ptr<Encoder> encoder(new Encoder(
      project_context_, ClientSecret::GenerateNewSecret(), system_data_.get()));

  // Use the Encoder to encode the Value.
  auto result = encoder->Encode(metric_, value);

  if (result.status != Encoder::kOK) {
    LOG(ERROR) << "Encode() failed with status " << result.status
               << ". metric_id=" << metric_ << ". Multi-part value:";
    for (size_t i = 0; i < num_parts; i++) {
      LOG(ERROR) << metric_parts[i] << ":" << values[i]
                 << " encoding=" << encoding_config_ids[i];
    }
    return false;
  }

  // Add the observation to the EnvelopeMaker. For the sake of testing
  // idempotency of the AddObservation() operation, we add the same Observation
  // multiple times.
  ObservationStore::StoreStatus status;
  for (size_t i = 0; i < FLAGS_num_adds_per_observation; i++) {
    uint64_t random_id;
    std::memcpy(&random_id, result.observation->random_id().data(),
                sizeof(random_id));
    VLOG(5) << "Adding observation with random_id=" << random_id;
    auto message = std::make_unique<EncryptedMessage>();
    if (!encrypt_to_analyzer_->Encrypt(*result.observation, message.get())) {
      LOG(ERROR) << "AddObservation() unable to encrypt message. metric_id="
                 << metric_;
      return false;
    }
    status = observation_store_->AddEncryptedObservation(
        std::move(message), std::unique_ptr<ObservationMetadata>(
                                new ObservationMetadata(*result.metadata)));
    shipping_manager_->NotifyObservationsAdded();
  }

  if (status != ObservationStore::kOk) {
    LOG(ERROR) << "AddObservation() failed with status "
               << MemoryObservationStore::StatusDebugString(status)
               << ". metric_id=" << metric_;
    return false;
  }
  return true;
}

// Generates FLAGS_num_clients independent Observations by encoding the
// string value specified by the argument and adds the Observations
// to the ShippingManager.
void TestApp::EncodeString(const std::string value) {
  for (size_t i = 0; i < FLAGS_num_clients; i++) {
    if (!EncodeStringAsNewClient(value)) {
      break;
    }
  }
}

// Generates a new ClientSecret, constructs a new Encoder using that secret,
// uses this Encoder to encode the string value specified by the
// argument, and adds the resulting Observation to the ShippingManager.
bool TestApp::EncodeStringAsNewClient(const std::string value) {
  std::unique_ptr<Encoder> encoder(new Encoder(
      project_context_, ClientSecret::GenerateNewSecret(), system_data_.get()));
  auto result = encoder->EncodeString(metric_, encoding_config_id_, value);
  if (result.status != Encoder::kOK) {
    LOG(ERROR) << "EncodeString() failed with status " << result.status
               << ". metric_id=" << metric_
               << ". encoding_config_id=" << encoding_config_id_
               << ". value=" << value;
    return false;
  }

  auto message = std::make_unique<EncryptedMessage>();
  if (!encrypt_to_analyzer_->Encrypt(*result.observation, message.get())) {
    LOG(ERROR) << "AddObservation() unable to encrypt message. metric_id="
               << metric_;
    return false;
  }

  // Add the observation to the ShippingManager.
  auto status = observation_store_->AddEncryptedObservation(
      std::move(message), std::unique_ptr<ObservationMetadata>(
                              new ObservationMetadata(*result.metadata)));
  shipping_manager_->NotifyObservationsAdded();
  if (status != ObservationStore::kOk) {
    LOG(ERROR) << "AddObservation() failed with status "
               << MemoryObservationStore::StatusDebugString(status)
               << ". metric_id=" << metric_;
    return false;
  }
  return true;
}

// Generates FLAGS_num_clients independent Observations by encoding the
// int value specified by the argument and adds the Observations
// to the ShippingManager.
void TestApp::EncodeInt(int64_t value) {
  for (size_t i = 0; i < FLAGS_num_clients; i++) {
    if (!EncodeIntAsNewClient(value)) {
      break;
    }
  }
}

// Generates a new ClientSecret, constructs a new Encoder using that secret,
// uses this Encoder to encode the int value specified by the
// argument, and adds the resulting Observation to the ShippingManager.
bool TestApp::EncodeIntAsNewClient(int64_t value) {
  std::unique_ptr<Encoder> encoder(new Encoder(
      project_context_, ClientSecret::GenerateNewSecret(), system_data_.get()));
  auto result = encoder->EncodeInt(metric_, encoding_config_id_, value);
  if (result.status != Encoder::kOK) {
    LOG(ERROR) << "EncodeInt() failed with status " << result.status
               << ". metric_id=" << metric_
               << ". encoding_config_id=" << encoding_config_id_
               << ". value=" << value;
    return false;
  }

  auto message = std::make_unique<EncryptedMessage>();
  if (!encrypt_to_analyzer_->Encrypt(*result.observation, message.get())) {
    LOG(ERROR) << "AddObservation() unable to encrypt message. metric_id="
               << metric_;
    return false;
  }

  // Add the observation to the ShippingManager.
  auto status = observation_store_->AddEncryptedObservation(
      std::move(message), std::unique_ptr<ObservationMetadata>(
                              new ObservationMetadata(*result.metadata)));
  shipping_manager_->NotifyObservationsAdded();
  if (status != ObservationStore::kOk) {
    LOG(ERROR) << "AddObservation() failed with status "
               << MemoryObservationStore::StatusDebugString(status)
               << ". metric_id=" << metric_;
    return false;
  }
  return true;
}

void TestApp::EncodeIndex(uint32_t index) {
  for (size_t i = 0; i < FLAGS_num_clients; i++) {
    if (!EncodeIndexAsNewClient(index)) {
      break;
    }
  }
}

bool TestApp::EncodeIndexAsNewClient(uint32_t index) {
  std::unique_ptr<Encoder> encoder(new Encoder(
      project_context_, ClientSecret::GenerateNewSecret(), system_data_.get()));
  auto result = encoder->EncodeIndex(metric_, encoding_config_id_, index);
  if (result.status != Encoder::kOK) {
    LOG(ERROR) << "EncodeIndex() failed with status " << result.status
               << ". metric_id=" << metric_
               << ". encoding_config_id=" << encoding_config_id_
               << ". index=" << index;
    return false;
  }

  auto message = std::make_unique<EncryptedMessage>();
  if (!encrypt_to_analyzer_->Encrypt(*result.observation, message.get())) {
    LOG(ERROR) << "AddObservation() unable to encrypt message. metric_id="
               << metric_;
    return false;
  }

  // Add the observation to the ShippingManager.
  auto status = observation_store_->AddEncryptedObservation(
      std::move(message), std::unique_ptr<ObservationMetadata>(
                              new ObservationMetadata(*result.metadata)));
  shipping_manager_->NotifyObservationsAdded();
  if (status != ObservationStore::kOk) {
    LOG(ERROR) << "AddObservation() failed with status "
               << MemoryObservationStore::StatusDebugString(status)
               << ". metric_id=" << metric_;
    return false;
  }
  return true;
}

bool TestApp::ProcessCommandLine(const std::string command_line) {
  return ProcessCommand(Tokenize(command_line));
}

bool TestApp::ProcessCommand(const std::vector<std::string>& command) {
  if (command.empty()) {
    return true;
  }

  if (command[0] == "help") {
    PrintHelp(ostream_);
    return true;
  }

  if (command[0] == "encode") {
    Encode(command);
    return true;
  }

  if (command[0] == "ls") {
    ListParameters();
    return true;
  }

  if (command[0] == "send") {
    Send(command);
    return true;
  }

  if (command[0] == "set") {
    SetParameter(command);
    return true;
  }

  if (command[0] == "show") {
    Show(command);
    return true;
  }

  if (command[0] == "quit") {
    return false;
  }

  *ostream_ << "Unrecognized command: " << command[0] << std::endl;

  return true;
}

void TestApp::Encode(const std::vector<std::string>& command) {
  if (command.size() < 3) {
    *ostream_ << "Malformed encode command. Expected 2 additional arguments."
              << std::endl;
    return;
  }

  if (command.size() > 3 || IsTriple(command[2])) {
    EncodeMulti(command);
    return;
  }

  int64_t num_clients;
  if (!ParseInt(command[1], true, &num_clients)) {
    return;
  }
  if (num_clients <= 0) {
    *ostream_ << "<num> must be a positive integer: " << num_clients
              << std::endl;
    return;
  }
  FLAGS_num_clients = num_clients;

  int64_t int_val;
  uint32_t index;
  if (ParseInt(command[2], false, &int_val)) {
    EncodeInt(int_val);
  } else if (ParseIndex(command[2], &index)) {
    EncodeIndex(index);
  } else {
    EncodeString(command[2]);
  }
}

void TestApp::EncodeMulti(const std::vector<std::string>& command) {
  CHECK_GE(command.size(), 3u);

  int64_t num_clients;
  if (!ParseInt(command[1], true, &num_clients)) {
    return;
  }
  if (num_clients <= 0) {
    *ostream_ << "<num> must be a positive integer: " << num_clients
              << std::endl;
  }
  FLAGS_num_clients = num_clients;

  std::vector<std::string> part_names;
  std::vector<std::string> values;
  std::vector<uint32_t> encoding_config_ids;
  for (size_t i = 2; i < command.size(); i++) {
    part_names.emplace_back();
    values.emplace_back();
    encoding_config_ids.emplace_back();
    if (!ParsePartValueEncodingTriple(command[i], &part_names.back(),
                                      &values.back(),
                                      &encoding_config_ids.back())) {
      *ostream_
          << "Malformed <part>:<value>:<encoding> triple in encode command: "
          << command[i] << std::endl;
      return;
    }
  }

  Encode(encoding_config_ids, part_names, values);
}

void TestApp::ListParameters() {
  *ostream_ << std::endl;
  *ostream_ << "Settable values" << std::endl;
  *ostream_ << "---------------" << std::endl;
  *ostream_ << "Metric ID: " << metric_ << std::endl;
  *ostream_ << "Encoding Config ID: " << encoding_config_id_ << std::endl;
  *ostream_ << std::endl;
  *ostream_ << "Values set by flag at startup." << std::endl;
  *ostream_ << "-----------------------------" << std::endl;
  *ostream_ << "Customer ID: " << customer_id_ << std::endl;
  *ostream_ << "Project ID: " << project_id_ << std::endl;
  *ostream_ << "Shuffler URI: " << FLAGS_shuffler_uri << std::endl;
  *ostream_ << std::endl;
}

void TestApp::SetParameter(const std::vector<std::string>& command) {
  if (command.size() != 3) {
    *ostream_ << "Malformed set command. Expected 2 additional arguments."
              << std::endl;
    return;
  }

  if (command[1] == "metric") {
    int64_t id;
    if (!ParseInt(command[2], true, &id)) {
      return;
    }
    if (id <= 0) {
      *ostream_ << "<id> must be a positive integer";
      return;
    }
    metric_ = id;
  } else if (command[1] == "encoding") {
    int64_t id;
    if (!ParseInt(command[2], true, &id)) {
      return;
    }
    if (id <= 0) {
      *ostream_ << "<id> must be a positive integer";
      return;
    }
    encoding_config_id_ = id;
  } else {
    *ostream_ << command[1] << " is not a settable parameter." << std::endl;
  }
}

void TestApp::Send(const std::vector<std::string>& command) {
  if (command.size() != 1) {
    *ostream_ << "The send command doesn't take any arguments." << std::endl;
    return;
  }
  SendAccumulatedObservations();
}

void TestApp::Show(const std::vector<std::string>& command) {
  // show config is currently the only show command.
  if (command.size() != 2 || command[1] != "config") {
    *ostream_ << "Expected 'show config'." << std::endl;
    return;
  }

  auto* metric = project_context_->Metric(metric_);
  if (!metric) {
    *ostream_ << "There is no metric with id=" << metric_ << "." << std::endl;
  } else {
    *ostream_ << "Metric " << metric->id() << std::endl;
    *ostream_ << "-----------" << std::endl;
    ShowMetric(*metric);
    *ostream_ << std::endl;
  }

  auto* encoding = project_context_->EncodingConfig(encoding_config_id_);
  if (!encoding) {
    *ostream_ << "There is no encoding config with id=" << encoding_config_id_
              << "." << std::endl;
  } else {
    *ostream_ << "Encoding Config " << encoding->id() << std::endl;
    *ostream_ << "--------------------" << std::endl;
    ShowEncodingConfig(*encoding);
    *ostream_ << std::endl;
  }
}

void TestApp::ShowMetric(const Metric& metric) {
  *ostream_ << metric.name() << std::endl;
  *ostream_ << metric.description() << std::endl;
  for (const auto& pair : metric.parts()) {
    const std::string& name = pair.first;
    const MetricPart& part = pair.second;
    std::string data_type;
    switch (part.data_type()) {
      case MetricPart::STRING:
        data_type = "string";
        break;

      case MetricPart::INT:
        data_type = "int";
        break;

      case MetricPart::INDEX:
        data_type = "indexed";
        break;

      case MetricPart::BLOB:
        data_type = "blob";
        break;

      default:
        data_type = "<missing case>";
        break;
    }
    *ostream_ << "One " << data_type << " part named \"" << name
              << "\": " << part.description() << std::endl;
  }
}

void TestApp::ShowEncodingConfig(const EncodingConfig& encoding) {
  switch (encoding.config_case()) {
    case EncodingConfig::kForculus:
      ShowForculusConfig(encoding.forculus());
      return;

    case EncodingConfig::kRappor:
      ShowRapporConfig(encoding.rappor());
      return;

    case EncodingConfig::kBasicRappor:
      ShowBasicRapporConfig(encoding.basic_rappor());
      return;

    case EncodingConfig::kNoOpEncoding:
      *ostream_ << "NoOp encoding";
      return;

    case EncodingConfig::CONFIG_NOT_SET:
      *ostream_ << "Invalid Encoding!";
      return;
  }
}

void TestApp::ShowForculusConfig(const ForculusConfig& config) {
  *ostream_ << "Forculus threshold=" << config.threshold() << std::endl;
}

void TestApp::ShowRapporConfig(const RapporConfig& config) {
  *ostream_ << "String Rappor" << std::endl;
}

void TestApp::ShowBasicRapporConfig(const BasicRapporConfig& config) {
  *ostream_ << "Basic Rappor " << std::endl;
  *ostream_ << "p=" << config.prob_0_becomes_1()
            << ", q=" << config.prob_1_stays_1() << std::endl;
  *ostream_ << "Categories:" << std::endl;
  switch (config.categories_case()) {
    case BasicRapporConfig::kStringCategories: {
      for (const std::string& s : config.string_categories().category()) {
        *ostream_ << s << std::endl;
      }
      return;
    }
    case BasicRapporConfig::kIntRangeCategories: {
      *ostream_ << config.int_range_categories().first() << " - "
                << config.int_range_categories().last();
      return;
    }
    case BasicRapporConfig::kIndexedCategories: {
      *ostream_ << "num_categories: "
                << config.indexed_categories().num_categories();
      return;
    }
    case BasicRapporConfig::CATEGORIES_NOT_SET:
      *ostream_ << "Invalid Encoding!";
      return;
  }
}

bool TestApp::ParseInt(const std::string& str, bool complain, int64_t* x) {
  CHECK(x);
  std::istringstream iss(str);
  *x = 0;
  iss >> *x;
  char c;
  if (*x == 0 || iss.fail() || iss.get(c)) {
    if (complain) {
      if (mode_ == kInteractive) {
        *ostream_ << "Expected positive integer instead of " << str << "."
                  << std::endl;
      } else {
        LOG(ERROR) << "Expected positive integer instead of " << str;
      }
    }
    return false;
  }
  return true;
}

bool TestApp::ParseIndex(const std::string& str, uint32_t* index) {
  CHECK(index);
  if (str.size() < 7) {
    return false;
  }
  if (str.substr(0, 6) != "index=") {
    return false;
  }
  auto index_string = str.substr(6);
  std::istringstream iss(index_string);
  int64_t possible_index;
  iss >> possible_index;
  char c;
  if (iss.fail() || iss.get(c) || possible_index < 0 ||
      possible_index > UINT32_MAX) {
    if (mode_ == kInteractive) {
      *ostream_ << "Expected small non-negative integer instead of "
                << index_string << "." << std::endl;
    } else {
      LOG(ERROR) << "Expected small non-negative integer instead of  "
                 << index_string;
    }
    return false;
  }
  *index = possible_index;
  return true;
}

// Parses a string of the form <part>:<value>:<encoding> and writes <part> into
// |part_name| and <value> into |value| and <encoding> into encoding_config_id.
// Returns true if and only if this succeeds.
bool TestApp::ParsePartValueEncodingTriple(const std::string& triple,
                                           std::string* part_name,
                                           std::string* value,
                                           uint32_t* encoding_config_id) {
  CHECK(part_name);
  CHECK(value);
  if (triple.size() < 5) {
    return false;
  }
  auto last_pos = triple.size() - 1;

  auto index1 = triple.find(':');
  if (index1 == std::string::npos || index1 == 0 || index1 > last_pos - 3) {
    return false;
  }
  auto index2 = triple.find(':', index1 + 2);
  if (index2 == std::string::npos || index2 > last_pos - 1) {
    return false;
  }
  *part_name = std::string(triple, 0, index1);
  *value = std::string(triple, index1 + 1, index2 - index1 - 1);
  std::string int_string = std::string(triple, index2 + 1);
  int64_t id;
  if (!ParseInt(int_string, true, &id)) {
    return false;
  }
  if (id < 0) {
    if (mode_ == kInteractive) {
      *ostream_ << "<encoding> must be positive: " << id << std::endl;
    } else {
      LOG(ERROR) << "<encoding> must be positive: " << id;
    }
    return false;
  }
  *encoding_config_id = id;
  return true;
}

// Determines whether or not |str| is a triple of the kind that may be
// parsed by ParsePartValueEncodingTriple.
bool TestApp::IsTriple(const std::string str) {
  std::string part_name;
  std::string value;
  uint32_t encoding_config_id;
  return ParsePartValueEncodingTriple(str, &part_name, &value,
                                      &encoding_config_id);
}

}  // namespace cobalt

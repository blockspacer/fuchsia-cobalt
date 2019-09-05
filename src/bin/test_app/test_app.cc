// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bin/test_app/test_app.h"

#include <libgen.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gflags/gflags.h"
#include "src/lib/clearcut/curl_http_client.h"
#include "src/lib/util/clock.h"
#include "src/lib/util/consistent_proto_store.h"
#include "src/lib/util/datetime_util.h"
#include "src/lib/util/file_util.h"
#include "src/lib/util/posix_file_system.h"
#include "src/lib/util/status.h"
#include "src/logger/encoder.h"
#include "src/logger/event_aggregator.h"
#include "src/logger/local_aggregation.pb.h"
#include "src/logger/project_context.h"
#include "src/logger/project_context_factory.h"
#include "src/logger/status.h"
#include "src/logging.h"
#include "src/observation_store/memory_observation_store.h"
#include "src/pb/observation2.pb.h"
#include "src/registry/cobalt_registry.pb.h"
#include "src/registry/metric_definition.pb.h"
#include "src/registry/project_configs.h"
#include "src/registry/report_definition.pb.h"
#include "src/system_data/system_data.h"
#include "src/uploader/shipping_manager.h"
#include "third_party/protobuf/src/google/protobuf/io/zero_copy_stream_impl.h"
#include "third_party/statusor/statusor.h"

namespace cobalt {

using encoder::ClearcutV1ShippingManager;
using encoder::ClientSecret;
using encoder::SystemData;
using encoder::SystemDataInterface;
using google::protobuf::RepeatedPtrField;
using logger::Encoder;
using logger::EventAggregator;
using logger::EventValuesPtr;
using logger::HistogramPtr;
using logger::kOK;
using logger::Logger;
using logger::LoggerInterface;
using logger::ObservationWriter;
using logger::ProjectContext;
using logger::ProjectContextFactory;
using logger::Status;
using observation_store::MemoryObservationStore;
using util::ConsistentProtoStore;
using util::EncryptedMessageMaker;
using util::IncrementingSystemClock;
using util::PosixFileSystem;
using util::SystemClock;
using util::TimeToDayIndex;

// There are three modes of operation of the Cobalt TestClient program
// determined by the value of this flag.
// - interactive: The program runs an interactive command-loop.
// - send-once: The program sends a single Envelope described by flags.
// - automatic: The program runs forever sending many Envelopes with randomly
//              generated values.
DEFINE_string(mode, "interactive",
              "This program may be used in 3 modes: 'interactive', "
              "'send-once', 'automatic'");

DEFINE_string(customer_name, "fuchsia", "Customer name");
DEFINE_string(project_name, "test_app2", "Project name");
DEFINE_string(metric_name, "error_occurred", "Initial Metric name");

DEFINE_string(analyzer_tink_keyset_file, "",
              "Path to a file containing a PEM encoding of the public key of "
              "the Analyzer used for Cobalt's internal encryption scheme. If "
              "not specified then no encryption will be used.");
DEFINE_string(shuffler_tink_keyset_file, "",
              "Path to a file containing a PEM encoding of the public key of "
              "the Shuffler used for Cobalt's internal encryption scheme. If "
              "not specified then no encryption will be used.");

DEFINE_string(config_bin_proto_path, "",
              "Path to the serialized CobaltRegistry proto from which the "
              "configuration is to be read. (Optional)");

DEFINE_string(clearcut_endpoint, "https://jmt17.google.com/log",
              "The URL to send clearcut requests to.");

DEFINE_string(local_aggregate_backup_file, "", "Back up local aggregates of events to this file.");

DEFINE_string(aggregated_obs_history_backup_file, "",
              "Back up the history of sent aggregated Observations to this file.");

namespace {

constexpr size_t kMaxBytesPerObservation = 100 * 1024;
constexpr size_t kMaxBytesPerEnvelope = 1024 * 1024;
constexpr size_t kMaxBytesTotal = 10 * 1024 * 1024;
const std::chrono::seconds kDeadlinePerSendAttempt(60);

// Prints help for the interactive mode.
void PrintHelp(std::ostream* ostream) {
  *ostream << "Cobalt command-line testing client" << std::endl;
  *ostream << "----------------------------------" << std::endl;
  *ostream << "help                     \tPrint this help message." << std::endl;
  *ostream << "log <num> event <index> <day> \tLog <num> independent copies "
              "of an EVENT_OCCURRED event."
           << std::endl
           << "                         \t- The <index> is the event_code of "
              "the EVENT_OCCURRED event."
           << std::endl
           << "                         \t- The optional argument <day> is the "
              "day for which the event should be logged."
           << std::endl
           << "                         \t  If provided, it "
              "should be of the form \"day=<day index>\", \"day=today\", "
              "\"day=today+<number of days>\", or \"day=today-<number of "
              "days>\"."
           << std::endl
           << "                         \t  The default day is the current day." << std::endl;

  *ostream << "log <num> event_count <index> <component> <duration> <count> <day>" << std::endl
           << "                         \tLog <num> independent copies of an "
              "EVENT_COUNT event."
           << std::endl
           << "                         \t- The <index> is the event_code of "
              "the EVENT_COUNT event."
           << std::endl
           << "                         \t- The <component> is the component "
              "name.  Pass in \"\" if your metric does not use this field."
           << std::endl
           << "                         \t- The <duration> specifies the "
              "period of time over which <count> EVENT_COUNT events occurred.  "
           << "Pass in 0 if your metric does not use this field." << std::endl
           << "                         \t- The <count> specifies the number "
              "of times an EVENT_COUNT event occurred."
           << std::endl
           << "                         \t- The optional argument <day> is the "
              "day for which the event should be logged."
           << std::endl
           << "                         \t  If provided, it "
              "should be of the form \"day=<day index>\", \"day=today\", "
              "\"day=today+<number of days>\", or \"day=today-<number of "
              "days>\"."
           << std::endl
           << "                         \t  The default day is the current day." << std::endl;
  *ostream << "log <num> elapsed_time <index> <component> <elapsed_micros>" << std::endl
           << "                         \tLog <num> independent copies of an "
              "ELAPSED_TIME event."
           << std::endl
           << "                         \t- The <index> is the event_code of "
              "the ELAPSED_TIME event."
           << std::endl
           << "                         \t- The <component> is the component "
              "name.  Pass in \"\" if your metric does not use this field."
           << std::endl
           << "                         \t- The <elapsed_micros> specifies how "
              "many microseconds have elapsed for the given ELAPSED_TIME event."
           << std::endl;
  *ostream << "log <num> frame_rate <index> <component> <fps>" << std::endl
           << "                         \tLog <num> independent copies of a "
              "FRAME_RATE event."
           << std::endl
           << "                         \t- The <index> is the event_code of "
              "the FRAME_RATE event."
           << std::endl
           << "                         \t- The <component> is the component "
              "name.  Pass in \"\" if your metric does not use this field."
           << std::endl
           << "                         \t- The <fps> specifies the frame rate." << std::endl;
  *ostream << "log <num> memory_usage <index> <component> <bytes>" << std::endl
           << "                         \tLog <num> independent copies of a "
              "MEMORY_USAGE event."
           << std::endl
           << "                         \t- The <index> is the event_code of "
              "the MEMORY_USAGE event."
           << std::endl
           << "                         \t- The <component> is the component "
              "name.  Pass in \"\" if your metric does not use this field."
           << std::endl
           << "                         \t- The <bytes> specifies the memory "
              "usage in bytes."
           << std::endl;
  *ostream << "log <num> int_histogram <index> <component> <bucket> <count>" << std::endl
           << "                         \tLog <num> independent copies of an "
              "INT_HISTOGRAM event."
           << std::endl
           << "                         \t- The <index> is the event_code of "
              "the INT_HISTOGRAM event."
           << std::endl
           << "                         \t- The <component> is the component "
              "name.  Pass in \"\" if your metric does not use this field."
           << std::endl
           << "                         \t- The <bucket> specifies the bucket "
              "index for this sample."
           << std::endl
           << "                         \t- The <count> specifies the count "
              "for this specific bucket."
           << std::endl;
  *ostream << "log <num> custom <part>:<val> <part>:<val>..." << std::endl;
  *ostream << "                         \tLog <num> independent copies of a "
           << "custom event." << std::endl;
  *ostream << "                         \t- Each <part> is an event dimension "
           << "name." << std::endl;
  *ostream << "                         \t- Each <val> is an int or string "
              "value or an index <n> if <val>='index=<n>'."
           << std::endl;
  *ostream << "generate <day>                 \tGenerate and send observations "
              "for <day> for all locally aggregated reports. <day> may be a "
              "day index, \'today\', \'today+N\', or \'today-N\'."
           << std::endl;
  *ostream << "reset-aggregation              \tDelete all state related to local "
              "aggregation."
           << std::endl;
  *ostream << "ls                       \tList current values of "
              "parameters."
           << std::endl;
  *ostream << "send                     \tSend all previously encoded "
              "observations and clear the observation cache."
           << std::endl;
  *ostream << "set metric <name>        \tSet metric." << std::endl;
  *ostream << "show config              \tDisplay the current Metric definition." << std::endl;
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
  snprintf(path2, sizeof(path2), "%s/gen/registry.pb", dir);

  // Get the absolute path to the registry.
  if (!realpath(path2, path)) {
    LOG(FATAL) << "Computed path to serialized CobaltRegistry is invalid: " << path;
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
  std::exit(-1);
}

// Reads the specified serialized CobaltRegistry pb file. Returns a
// ProjectContextFactory containing the CobaltRegistry read from the file.
std::unique_ptr<ProjectContextFactory> LoadCobaltRegistry(const std::string& registry_pb_path) {
  VLOG(2) << "Loading Cobalt configuration from " << registry_pb_path;

  std::ifstream registry_file_stream;
  registry_file_stream.open(registry_pb_path);
  CHECK(registry_file_stream) << "Could not open cobalt registry pb file: " << registry_pb_path;

  // Parse the CobaltRegistry.
  auto cobalt_registry = std::make_unique<cobalt::CobaltRegistry>();
  google::protobuf::io::IstreamInputStream registry_file_input(&registry_file_stream);
  CHECK(cobalt_registry->ParseFromZeroCopyStream(&registry_file_input))
      << "Could not parse the cobalt registry pb file: " << registry_pb_path;
  return std::make_unique<ProjectContextFactory>(std::move(cobalt_registry));
}

// Given a |line| of text, breaks it into tokens separated by white space.
std::vector<std::string> Tokenize(const std::string& line) {
  std::istringstream line_stream(line);
  std::vector<std::string> tokens;
  do {
    std::string token;
    line_stream >> token;
    token.erase(std::remove(token.begin(), token.end(), ' '), token.end());
    if (!token.empty()) {
      tokens.push_back(token);
    }
  } while (line_stream);
  return tokens;
}

}  // namespace

namespace internal {

// The number of seconds in a day.
static const int kDay = 86400;

class RealLoggerFactory : public LoggerFactory {
 public:
  ~RealLoggerFactory() override = default;

  RealLoggerFactory(std::unique_ptr<EncryptedMessageMaker> observation_encrypter,
                    std::unique_ptr<EncryptedMessageMaker> envelope_encrypter,
                    std::unique_ptr<ProjectContextFactory> project_context_factory,
                    std::string customer_name, std::string project_name,
                    std::unique_ptr<MemoryObservationStore> observation_store,
                    std::unique_ptr<ClearcutV1ShippingManager> shipping_manager,
                    std::unique_ptr<ConsistentProtoStore> local_aggregate_proto_store,
                    std::unique_ptr<ConsistentProtoStore> obs_history_proto_store,
                    std::unique_ptr<SystemDataInterface> system_data);

  std::unique_ptr<LoggerInterface> NewLogger(uint32_t day_index) override;
  size_t ObservationCount() override;
  void ResetObservationCount() override;
  void ResetLocalAggregation() override;
  bool GenerateAggregatedObservations(uint32_t day_index) override;
  bool SendAccumulatedObservations() override;
  const ProjectContext* project_context() override { return project_context_.get(); }

 private:
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;
  std::unique_ptr<EncryptedMessageMaker> envelope_encrypter_;
  std::unique_ptr<ProjectContextFactory> project_context_factory_;
  std::string customer_name_;
  std::string project_name_;
  std::unique_ptr<ProjectContext> project_context_;
  std::unique_ptr<MemoryObservationStore> observation_store_;
  std::unique_ptr<ClearcutV1ShippingManager> shipping_manager_;
  std::unique_ptr<ConsistentProtoStore> local_aggregate_proto_store_;
  std::unique_ptr<ConsistentProtoStore> obs_history_proto_store_;
  std::unique_ptr<SystemDataInterface> system_data_;
  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<ObservationWriter> observation_writer_;
  std::unique_ptr<EventAggregator> event_aggregator_;
};

RealLoggerFactory::RealLoggerFactory(
    std::unique_ptr<EncryptedMessageMaker> observation_encrypter,
    std::unique_ptr<EncryptedMessageMaker> envelope_encrypter,
    std::unique_ptr<ProjectContextFactory> project_context_factory, std::string customer_name,
    std::string project_name, std::unique_ptr<MemoryObservationStore> observation_store,
    std::unique_ptr<ClearcutV1ShippingManager> shipping_manager,
    std::unique_ptr<ConsistentProtoStore> local_aggregate_proto_store,
    std::unique_ptr<ConsistentProtoStore> obs_history_proto_store,
    std::unique_ptr<SystemDataInterface> system_data)
    : observation_encrypter_(std::move(observation_encrypter)),
      envelope_encrypter_(std::move(envelope_encrypter)),
      project_context_factory_(std::move(project_context_factory)),
      customer_name_(std::move(customer_name)),
      project_name_(std::move(project_name)),
      project_context_(project_context_factory_->NewProjectContext(customer_name_, project_name_)),
      observation_store_(std::move(observation_store)),
      shipping_manager_(std::move(shipping_manager)),
      local_aggregate_proto_store_(std::move(local_aggregate_proto_store)),
      obs_history_proto_store_(std::move(obs_history_proto_store)),
      system_data_(std::move(system_data)) {
  encoder_ = std::make_unique<Encoder>(ClientSecret::GenerateNewSecret(), system_data_.get());
  observation_writer_ = std::make_unique<ObservationWriter>(
      observation_store_.get(), shipping_manager_.get(), observation_encrypter_.get());
  event_aggregator_ = std::make_unique<EventAggregator>(encoder_.get(), observation_writer_.get(),
                                                        local_aggregate_proto_store_.get(),
                                                        obs_history_proto_store_.get());
}

std::unique_ptr<LoggerInterface> RealLoggerFactory::NewLogger(uint32_t day_index) {
  std::unique_ptr<Logger> logger = std::make_unique<Logger>(
      project_context_factory_->NewProjectContext(customer_name_, project_name_), encoder_.get(),
      event_aggregator_.get(), observation_writer_.get(), system_data_.get());
  if (day_index != 0u) {
    auto mock_clock = new IncrementingSystemClock();
    mock_clock->set_time(
        std::chrono::system_clock::time_point(std::chrono::seconds(kDay * day_index)));
    logger->SetClock(mock_clock);
  }
  return std::move(logger);
}

size_t RealLoggerFactory::ObservationCount() {
  return observation_store_->num_observations_added();
}

void RealLoggerFactory::ResetObservationCount() { observation_store_->ResetObservationCounter(); }

// TODO(pesk): also clear the contents of the ConsistentProtoStores if we
// implement a mode which uses them.
void RealLoggerFactory::ResetLocalAggregation() {
  event_aggregator_ = std::make_unique<EventAggregator>(encoder_.get(), observation_writer_.get(),
                                                        local_aggregate_proto_store_.get(),
                                                        obs_history_proto_store_.get());
}

bool RealLoggerFactory::GenerateAggregatedObservations(uint32_t day_index) {
  return kOK == event_aggregator_->GenerateObservationsNoWorker(day_index);
}

bool RealLoggerFactory::SendAccumulatedObservations() {
  shipping_manager_->RequestSendSoon();
  shipping_manager_->WaitUntilIdle(kDeadlinePerSendAttempt);
  auto status = shipping_manager_->last_send_status();
  return status.ok();
}

}  // namespace internal

std::unique_ptr<TestApp> TestApp::CreateFromFlagsOrDie(char* argv[]) {
  std::string registry_pb_path = FLAGS_config_bin_proto_path;
  // If no path is given, try to deduce it from the binary location.
  if (registry_pb_path.empty()) {
    registry_pb_path = FindCobaltRegistryProto(argv);
  }

  auto project_context_factory = LoadCobaltRegistry(registry_pb_path);
  // Attempt to create a new ProjetContext here even though we don't need one
  // yet in order to confirm that the customer and project names are good.
  CHECK(project_context_factory->NewProjectContext(FLAGS_customer_name, FLAGS_project_name))
      << "The Cobalt Registry does not contain a project named: " << FLAGS_customer_name << "."
      << FLAGS_project_name;

  auto mode = ParseMode();

  std::unique_ptr<EncryptedMessageMaker> observation_encrypter;
  if (FLAGS_analyzer_tink_keyset_file.empty()) {
    VLOG(2) << "WARNING: Observations will not be encrypted to the Analyzer. "
               "Rerun with the flag -analyzer_tink_keyset_file.";
    observation_encrypter = EncryptedMessageMaker::MakeUnencrypted();
  } else {
    VLOG(2) << "Reading analyzer keyset file at " << FLAGS_analyzer_tink_keyset_file;
    auto key_value =
        cobalt::util::ReadNonEmptyTextFile(FLAGS_analyzer_tink_keyset_file).ValueOrDie();
    observation_encrypter = EncryptedMessageMaker::MakeForObservations(key_value).ValueOrDie();
  }

  std::unique_ptr<EncryptedMessageMaker> envelope_encrypter;
  if (FLAGS_shuffler_tink_keyset_file.empty()) {
    VLOG(2) << "WARNING: Envelopes will not be encrypted to the Shuffler. "
               "Rerun with the flag -shuffler_tink_keyset_file.";
    observation_encrypter = EncryptedMessageMaker::MakeUnencrypted();
    envelope_encrypter = EncryptedMessageMaker::MakeUnencrypted();
  } else {
    VLOG(2) << "Reading shuffler keyset file at " << FLAGS_shuffler_tink_keyset_file;
    auto key_value =
        cobalt::util::ReadNonEmptyTextFile(FLAGS_shuffler_tink_keyset_file).ValueOrDie();
    envelope_encrypter = EncryptedMessageMaker::MakeForObservations(key_value).ValueOrDie();
  }

  std::unique_ptr<SystemDataInterface> system_data(
      new SystemData("test_app", "", ReleaseStage::DEBUG));

  auto observation_store = std::make_unique<MemoryObservationStore>(
      kMaxBytesPerObservation, kMaxBytesPerEnvelope, kMaxBytesTotal);
  auto local_aggregate_proto_store = std::make_unique<ConsistentProtoStore>(
      FLAGS_local_aggregate_backup_file, std::make_unique<PosixFileSystem>());
  auto obs_history_proto_store = std::make_unique<ConsistentProtoStore>(
      FLAGS_aggregated_obs_history_backup_file, std::make_unique<PosixFileSystem>());

  // By using (kMaxSeconds, 0) here we are effectively putting the
  // ShippingDispatcher in manual mode. It will never send
  // automatically and it will send immediately in response to
  // RequestSendSoon().
  auto upload_scheduler =
      encoder::UploadScheduler(encoder::UploadScheduler::kMaxSeconds, std::chrono::seconds(0));
  if (mode == TestApp::kAutomatic) {
    // In automatic mode, let the ShippingManager send to the Shuffler
    // every 10 seconds.
    const auto upload_interval = std::chrono::seconds(10);
    upload_scheduler = encoder::UploadScheduler(upload_interval, std::chrono::seconds(1));
  }
  auto shipping_manager = std::make_unique<ClearcutV1ShippingManager>(
      upload_scheduler, observation_store.get(), envelope_encrypter.get(),
      std::make_unique<clearcut::ClearcutUploader>(
          FLAGS_clearcut_endpoint, std::make_unique<util::clearcut::CurlHTTPClient>()));
  shipping_manager->Start();

  std::unique_ptr<LoggerFactory> logger_factory(new internal::RealLoggerFactory(
      std::move(observation_encrypter), std::move(envelope_encrypter),
      std::move(project_context_factory), FLAGS_customer_name, FLAGS_project_name,
      std::move(observation_store), std::move(shipping_manager),
      std::move(local_aggregate_proto_store), std::move(obs_history_proto_store),
      std::move(system_data)));

  std::unique_ptr<TestApp> test_app(
      new TestApp(std::move(logger_factory), FLAGS_metric_name, mode, &std::cout));
  return test_app;
}

TestApp::TestApp(std::unique_ptr<LoggerFactory> logger_factory,
                 const std::string& initial_metric_name, Mode mode, std::ostream* ostream)
    : mode_(mode), logger_factory_(std::move(logger_factory)), ostream_(ostream) {
  CHECK(logger_factory_);
  CHECK(logger_factory_->project_context());
  CHECK(ostream_);
  CHECK(SetMetric(initial_metric_name));
  clock_ = std::make_unique<SystemClock>();
}

bool TestApp::SetMetric(const std::string& metric_name) {
  auto metric = logger_factory_->project_context()->GetMetric(metric_name);
  if (!metric) {
    (*ostream_) << "There is no metric named '" << metric_name << "' in  project "
                << logger_factory_->project_context()->DebugString() << "." << std::endl
                << "You may need to run `./cobaltb.py update_config`." << std::endl;
    return false;
  }
  current_metric_ = metric;
  return true;
}

void TestApp::Run() {
  switch (mode_) {
    case kInteractive:
      CommandLoop();
      break;
    default:
      CHECK(false) << "Only interactive mode is coded so far.";
  }
}

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

bool TestApp::ProcessCommandLine(const std::string& command_line) {
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

  if (command[0] == "log") {
    Log(command);
    return true;
  }

  if (command[0] == "generate") {
    GenerateAggregatedObservations(command);
    return true;
  }

  if (command[0] == "reset-aggregation") {
    ResetLocalAggregation();
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

// We know that command[0] = "log"
void TestApp::Log(const std::vector<std::string>& command) {
  if (command.size() < 2) {
    *ostream_ << "Malformed log command. Expected <num> argument after 'log'." << std::endl;
    return;
  }

  int64_t num_clients;
  if (!ParseNonNegativeInt(command[1], true, &num_clients)) {
    return;
  }
  if (num_clients <= 0) {
    *ostream_ << "Malformed log command. <num> must be positive: " << num_clients << std::endl;
    return;
  }

  if (command.size() < 3) {
    *ostream_ << "Malformed log command. Expected log method to be specified "
                 "after <num>."
              << std::endl;
    return;
  }

  if (command[2] == "event") {
    LogEvent(num_clients, command);
    return;
  }

  if (command[2] == "event_count") {
    LogEventCount(num_clients, command);
    return;
  }

  if (command[2] == "elapsed_time") {
    LogElapsedTime(num_clients, command);
    return;
  }

  if (command[2] == "frame_rate") {
    LogFrameRate(num_clients, command);
    return;
  }

  if (command[2] == "memory_usage") {
    LogMemoryUsage(num_clients, command);
    return;
  }

  if (command[2] == "int_histogram") {
    LogIntHistogram(num_clients, command);
    return;
  }

  if (command[2] == "custom") {
    LogCustomEvent(num_clients, command);
    return;
  }

  *ostream_ << "Unrecognized log method specified: " << command[2] << std::endl;
}

uint32_t TestApp::CurrentDayIndex() {
  return TimeToDayIndex(std::chrono::system_clock::to_time_t(clock_->now()), MetricDefinition::UTC);
}

// We know that command[0] = "log", command[1] = <num_clients>
void TestApp::LogEvent(uint64_t num_clients, const std::vector<std::string>& command) {
  auto command_size = command.size();
  const auto minimum_command_size = 4;
  const auto maximum_command_size = 5;
  if (command_size < minimum_command_size) {
    *ostream_ << "Malformed log event command. Expected one more "
                 "argument for <event_code>."
              << std::endl;
    return;
  }

  if (command_size > maximum_command_size) {
    *ostream_ << "Malformed log event command: too many arguments." << std::endl;
    return;
  }

  int64_t event_code;
  if (!ParseNonNegativeInt(command[3], true, &event_code)) {
    return;
  }

  uint32_t day_index = 0;
  if (command_size == maximum_command_size &&
      !ParseDay(command[maximum_command_size - 1], &day_index)) {
    *ostream_ << "Unable to parse <day> from log command: " << command[maximum_command_size - 1]
              << std::endl;
    return;
  }
  LogEvent(num_clients, event_code, day_index);
}

void TestApp::LogEvent(size_t num_clients, uint32_t event_code, uint32_t day_index) {
  if (!current_metric_) {
    *ostream_ << "Cannot LogEvent. There is no current metric set." << std::endl;
    return;
  }
  VLOG(6) << "TestApp::LogEvents(" << num_clients << ", " << event_code << ", " << day_index
          << ").";
  for (size_t i = 0; i < num_clients; i++) {
    auto logger = logger_factory_->NewLogger(day_index);
    auto status = logger->LogEvent(current_metric_->id(), event_code);
    if (status != logger::kOK) {
      LOG(ERROR) << "LogEvent() failed with status " << status
                 << ". metric=" << current_metric_->metric_name() << ". event_code=" << event_code
                 << ". day_index=" << day_index;
      break;
    }
  }
  *ostream_ << "Done." << std::endl;
}

// We know that command[0] = "log", command[1] = <num_clients>,
// command[2] = "event_count".
void TestApp::LogEventCount(uint64_t num_clients, const std::vector<std::string>& command) {
  auto command_size = command.size();
  const auto minimum_command_size = 7;
  const auto maximum_command_size = 8;
  enum { INDEX = 3, COMPONENT, DURATION, COUNT, DAY };
  if (command_size < minimum_command_size) {
    *ostream_ << "Malformed log event_count command: missing at least one "
                 "required argument."
              << std::endl;
    return;
  }
  if (command_size > maximum_command_size) {
    *ostream_ << "Malformed log event_count command: too many arguments." << std::endl;
  }

  int64_t event_code;
  if (!ParseNonNegativeInt(command[INDEX], true, &event_code)) {
    *ostream_ << "Unable to parse <index> from log command: " << command[INDEX] << std::endl;
    return;
  }

  int64_t duration;
  if (!ParseNonNegativeInt(command[DURATION], true, &duration)) {
    *ostream_ << "Unable to parse <duration> from log command: " << command[DURATION] << std::endl;
    return;
  }

  int64_t count;
  if (!ParseNonNegativeInt(command[COUNT], true, &count)) {
    *ostream_ << "Unable to parse <count> from log command: " << command[COUNT] << std::endl;
    return;
  }
  uint32_t day_index = 0u;
  if (command_size == maximum_command_size && !ParseDay(command[DAY], &day_index)) {
    *ostream_ << "Unable to parse <day> from log command: " << command[DAY] << std::endl;
    return;
  }

  LogEventCount(num_clients, event_code, command[COMPONENT], duration, count, day_index);
}

void TestApp::LogEventCount(size_t num_clients, uint32_t event_code, const std::string& component,
                            int64_t duration, int64_t count, uint32_t day_index) {
  if (!current_metric_) {
    *ostream_ << "Cannot LogEventCount. There is no current metric set." << std::endl;
    return;
  }
  VLOG(6) << "TestApp::LogEventCount(" << num_clients << ", " << event_code << ", " << component
          << ", " << duration << ", " << count << ", " << day_index << ").";
  for (size_t i = 0; i < num_clients; i++) {
    auto logger = logger_factory_->NewLogger(day_index);
    auto status =
        logger->LogEventCount(current_metric_->id(), event_code, component, duration, count);
    if (status != logger::kOK) {
      LOG(ERROR) << "LogEventCount() failed with status " << status
                 << ". metric=" << current_metric_->metric_name() << ". event_code=" << event_code
                 << ". component=" << component << ". duration=" << duration << ". count=" << count
                 << ". day_index=" << day_index;
      break;
    }
  }
  *ostream_ << "Done." << std::endl;
}

// We know that command[0] = "log", command[1] = <num_clients>,
// command[2] = "elapsed_time"
void TestApp::LogElapsedTime(uint64_t num_clients, const std::vector<std::string>& command) {
  enum { INDEX = 3, COMPONENT, ELAPSED_MICROS };
  const auto expected_size = 6;
  if (command.size() != expected_size) {
    *ostream_ << "Malformed log elapsed_time command. Expected 3 additional "
              << "parameters." << std::endl;
    return;
  }

  int64_t event_code;
  if (!ParseNonNegativeInt(command[INDEX], true, &event_code)) {
    *ostream_ << "Unable to parse <index> from log command: " << command[INDEX] << std::endl;
    return;
  }

  int64_t elapsed_micros;
  if (!ParseNonNegativeInt(command[ELAPSED_MICROS], true, &elapsed_micros)) {
    *ostream_ << "Unable to parse <elapsed_micros> from log command: " << command[ELAPSED_MICROS]
              << std::endl;
    return;
  }

  LogElapsedTime(num_clients, event_code, command[COMPONENT], elapsed_micros);
}

void TestApp::LogElapsedTime(uint64_t num_clients, uint32_t event_code,
                             const std::string& component, int64_t elapsed_micros) {
  if (!current_metric_) {
    *ostream_ << "Cannot LogElapsedTime. There is no current metric set." << std::endl;
    return;
  }

  VLOG(6) << "TestApp::LogElapsedTime(" << num_clients << ", " << event_code << ", " << component
          << ", " << elapsed_micros << ").";
  for (size_t i = 0; i < num_clients; i++) {
    auto logger = logger_factory_->NewLogger();
    auto status =
        logger->LogElapsedTime(current_metric_->id(), event_code, component, elapsed_micros);
    if (status != logger::kOK) {
      LOG(ERROR) << "LogElapsedTime() failed with status " << status
                 << ". metric=" << current_metric_->metric_name() << ". event_code=" << event_code
                 << ". component=" << component << ". elapsed_micros=" << elapsed_micros;
      break;
    }
  }
  *ostream_ << "Done." << std::endl;
}

// We know that command[0] = "log", command[1] = <num_clients>,
// command[2] = "frame_rate"
void TestApp::LogFrameRate(uint64_t num_clients, const std::vector<std::string>& command) {
  enum { INDEX = 3, COMPONENT, FPS };
  const auto expected_size = 6;
  if (command.size() != expected_size) {
    *ostream_ << "Malformed log frame_rate command. Expected 3 additional "
              << "parameters." << std::endl;
    return;
  }

  int64_t event_code;
  if (!ParseNonNegativeInt(command[INDEX], true, &event_code)) {
    *ostream_ << "Unable to parse <index> from log command: " << command[INDEX] << std::endl;
    return;
  }

  float fps;
  if (!ParseFloat(command[FPS], true, &fps)) {
    *ostream_ << "Unable to parse <fps> from log command: " << command[FPS] << std::endl;
    return;
  }

  LogFrameRate(num_clients, event_code, command[COMPONENT], fps);
}

void TestApp::LogFrameRate(uint64_t num_clients, uint32_t event_code, const std::string& component,
                           float fps) {
  if (!current_metric_) {
    *ostream_ << "Cannot LogFrameRate. There is no current metric set." << std::endl;
    return;
  }

  VLOG(6) << "TestApp::LogFrameRate(" << num_clients << ", " << event_code << ", " << component
          << ", " << fps << ").";
  for (size_t i = 0; i < num_clients; i++) {
    auto logger = logger_factory_->NewLogger();
    auto status = logger->LogFrameRate(current_metric_->id(), event_code, component, fps);
    if (status != logger::kOK) {
      LOG(ERROR) << "LogFrameRate() failed with status " << status
                 << ". metric=" << current_metric_->metric_name() << ". event_code=" << event_code
                 << ". component=" << component << ". fps=" << fps;
      break;
    }
  }
  *ostream_ << "Done." << std::endl;
}

// We know that command[0] = "log", command[1] = <num_clients>,
// command[2] = "memory_usage"
void TestApp::LogMemoryUsage(uint64_t num_clients, const std::vector<std::string>& command) {
  enum { INDEX = 3, COMPONENT, BYTES };
  const auto expected_size = 6;
  if (command.size() != expected_size) {
    *ostream_ << "Malformed log memory_usage command. Expected 3 additional "
              << "parameters." << std::endl;
    return;
  }

  int64_t event_code;
  if (!ParseNonNegativeInt(command[INDEX], true, &event_code)) {
    *ostream_ << "Unable to parse <index> from log command: " << command[INDEX] << std::endl;
    return;
  }

  int64_t bytes;
  if (!ParseNonNegativeInt(command[BYTES], true, &bytes)) {
    *ostream_ << "Unable to parse <bytes> from log command: " << command[BYTES] << std::endl;
    return;
  }

  LogMemoryUsage(num_clients, event_code, command[COMPONENT], bytes);
}

void TestApp::LogMemoryUsage(uint64_t num_clients, uint32_t event_code,
                             const std::string& component, int64_t bytes) {
  if (!current_metric_) {
    *ostream_ << "Cannot LogMemoryUsage. There is no current metric set." << std::endl;
    return;
  }

  VLOG(6) << "TestApp::LogMemoryUsage(" << num_clients << ", " << event_code << ", " << component
          << ", " << bytes << ").";
  for (size_t i = 0; i < num_clients; i++) {
    auto logger = logger_factory_->NewLogger();
    auto status = logger->LogMemoryUsage(current_metric_->id(), event_code, component, bytes);
    if (status != logger::kOK) {
      LOG(ERROR) << "LogMemoryUsage() failed with status " << status
                 << ". metric=" << current_metric_->metric_name() << ". event_code=" << event_code
                 << ". component=" << component << ". bytes=" << bytes;
      break;
    }
  }
  *ostream_ << "Done." << std::endl;
}

// We know that command[0] = "log", command[1] = <num_clients>,
// command[2] = "int_histogram"
void TestApp::LogIntHistogram(uint64_t num_clients, const std::vector<std::string>& command) {
  enum { INDEX = 3, COMPONENT, BUCKET, COUNT };
  const auto expected_size = 7;
  if (command.size() != expected_size) {
    *ostream_ << "Malformed log int_histogram command. Expected 4 additional "
              << "parameters." << std::endl;
    return;
  }

  int64_t event_code;
  if (!ParseNonNegativeInt(command[INDEX], true, &event_code)) {
    *ostream_ << "Unable to parse <index> from log command: " << command[INDEX] << std::endl;
    return;
  }

  int64_t bucket;
  if (!ParseNonNegativeInt(command[BUCKET], true, &bucket)) {
    *ostream_ << "Unable to parse <bucket> from log command: " << command[BUCKET] << std::endl;
    return;
  }

  int64_t count;
  if (!ParseNonNegativeInt(command[COUNT], true, &count)) {
    *ostream_ << "Unable to parse <count> from log command: " << command[COUNT] << std::endl;
    return;
  }

  LogIntHistogram(num_clients, event_code, command[COMPONENT], bucket, count);
}

void TestApp::LogIntHistogram(uint64_t num_clients, uint32_t event_code,
                              const std::string& component, int64_t bucket, int64_t count) {
  if (!current_metric_) {
    *ostream_ << "Cannot LogIntHistogram. There is no current metric set." << std::endl;
    return;
  }

  VLOG(6) << "TestApp::LogIntHistogram(" << num_clients << ", " << event_code << ", " << component
          << ", " << bucket << ", " << count << ").";

  for (size_t i = 0; i < num_clients; i++) {
    HistogramPtr histogram_ptr = std::make_unique<RepeatedPtrField<HistogramBucket>>();
    auto* histogram = histogram_ptr->Add();
    histogram->set_index(bucket);
    histogram->set_count(count);

    auto logger = logger_factory_->NewLogger();
    auto status = logger->LogIntHistogram(current_metric_->id(), event_code, component,
                                          std::move(histogram_ptr));
    if (status != logger::kOK) {
#ifndef PROTO_LITE
      LOG(ERROR) << "LogIntHistogram() failed with status " << status
                 << ". metric=" << current_metric_->metric_name() << ". event_code=" << event_code
                 << ". component=" << component
                 << ". histogram=" << histogram_ptr->Get(0).DebugString();
#else
      LOG(ERROR) << "LogIntHistogram() failed with status " << status
                 << ". metric=" << current_metric_->metric_name() << ". event_code=" << event_code
                 << ". component=" << component;
#endif
      break;
    }
  }
  *ostream_ << "Done." << std::endl;
}

// We know that command[0] = "log", command[1] = <num_clients>,
// command[2] = "custom"
void TestApp::LogCustomEvent(uint64_t num_clients, const std::vector<std::string>& command) {
  if (command.size() <= 3) {
    *ostream_ << "Malformed log custom event command. Expected a list of "
                 "<part>:<value>."
              << std::endl;
    return;
  }

  std::vector<std::string> part_names;
  std::vector<std::string> values;
  for (size_t i = 3; i < command.size(); i++) {
    part_names.emplace_back();
    values.emplace_back();
    if (!ParsePartValuePair(command[i], &part_names.back(), &values.back())) {
      *ostream_ << "Malformed <part>:<value> in log command: " << command[i] << std::endl;
      return;
    }
  }

  LogCustomEvent(num_clients, part_names, values);
}

void TestApp::LogCustomEvent(uint64_t num_clients, const std::vector<std::string>& metric_parts,
                             const std::vector<std::string>& values) {
  CHECK_EQ(metric_parts.size(), values.size());

  if (!current_metric_) {
    *ostream_ << "Cannot LogCustomEvent. There is no current metric set." << std::endl;
    return;
  }

  VLOG(6) << "TestApp::LogCustomEvent(" << num_clients << ", custom_event).";
  for (size_t i = 0; i < num_clients; i++) {
    auto logger = logger_factory_->NewLogger();
    EventValuesPtr event_values = NewCustomEvent(metric_parts, values);
    auto status = logger->LogCustomEvent(current_metric_->id(), std::move(event_values));
    if (status != logger::kOK) {
      LOG(ERROR) << "LogCustomEvent() failed with status " << status
                 << ". metric=" << current_metric_->metric_name();
      break;
    }
  }
  *ostream_ << "Done." << std::endl;
}

// We know that command[0] = "generate"
void TestApp::GenerateAggregatedObservations(const std::vector<std::string>& command) {
  if (command.size() > 2) {
    *ostream_ << "Malformed generate command: too many arguments." << std::endl;
    return;
  }
  uint32_t day_index;
  if (command.size() < 2) {
    day_index = CurrentDayIndex();
  } else if (!ParseDay(command[1], &day_index)) {
    *ostream_ << "Could not parse argument " << command[1] << " to a day index" << std::endl;
    return;
  }
  GenerateAggregatedObservationsAndSend(day_index);
}

void TestApp::GenerateAggregatedObservationsAndSend(uint32_t day_index) {
  logger_factory_->NewLogger();
  logger_factory_->ResetObservationCount();
  if (logger_factory_->GenerateAggregatedObservations(day_index)) {
    *ostream_ << "Generated " << logger_factory_->ObservationCount()
              << " locally aggregated observations for day index " << day_index << std::endl;
  } else {
    *ostream_ << "Failed to generate locally aggregated observations for day index " << day_index
              << std::endl;
    return;
  }
  if (!logger_factory_->SendAccumulatedObservations()) {
    *ostream_ << "Failed to send locally aggregated observations" << std::endl;
  }
}

void TestApp::ResetLocalAggregation() {
  logger_factory_->NewLogger();
  logger_factory_->ResetLocalAggregation();
  *ostream_ << "Reset local aggregation." << std::endl;
}

void TestApp::ListParameters() {
  std::string metric_name = "No metric set";
  if (current_metric_) {
    metric_name = current_metric_->metric_name();
  }
  *ostream_ << std::endl;
  *ostream_ << "Settable values" << std::endl;
  *ostream_ << "---------------" << std::endl;
  *ostream_ << "Metric: '" << metric_name << "'" << std::endl;
  *ostream_ << std::endl;
  *ostream_ << "Values set by flag at startup." << std::endl;
  *ostream_ << "-----------------------------" << std::endl;
  *ostream_ << "Customer: " << logger_factory_->project_context()->project().customer_name()
            << std::endl;
  *ostream_ << "Project: " << logger_factory_->project_context()->project().project_name()
            << std::endl;
  *ostream_ << "Clearcut endpoint: " << FLAGS_clearcut_endpoint << std::endl;
  *ostream_ << std::endl;
}

void TestApp::SetParameter(const std::vector<std::string>& command) {
  if (command.size() != 3) {
    *ostream_ << "Malformed set command. Expected 2 additional arguments." << std::endl;
    return;
  }

  if (command[1] == "metric") {
    if (SetMetric(command[2])) {
      *ostream_ << "Metric set." << std::endl;
    } else {
      *ostream_ << "Current metric unchanged." << std::endl;
    }
  } else {
    *ostream_ << command[1] << " is not a settable parameter." << std::endl;
  }
}

void TestApp::Send(const std::vector<std::string>& command) {
  if (command.size() != 1) {
    *ostream_ << "The send command doesn't take any arguments." << std::endl;
    return;
  }

  if (logger_factory_->SendAccumulatedObservations()) {
    if (mode_ == TestApp::kInteractive) {
      std::cout << "Send to server succeeded." << std::endl;
    } else {
      VLOG(2) << "Send to server succeeded";
    }
  } else {
    if (mode_ == TestApp::kInteractive) {
      std::cout << "Send to server failed." << std::endl;
    } else {
      LOG(ERROR) << "Send to server failed.";
    }
  }
}

#ifdef PROTO_LITE
namespace {

std::string PrintMetricType(MetricDefinition_MetricType type) {
  switch (type) {
    case MetricDefinition::UNSET:
      return "UNSET";
    case MetricDefinition::EVENT_OCCURRED:
      return "EVENT_OCCURRED";
    case MetricDefinition::EVENT_COUNT:
      return "EVENT_COUNT";
    case MetricDefinition::ELAPSED_TIME:
      return "ELAPSED_TIME";
    case MetricDefinition::FRAME_RATE:
      return "FRAME_RATE";
    case MetricDefinition::MEMORY_USAGE:
      return "MEMORY_USAGE";
    case MetricDefinition::INT_HISTOGRAM:
      return "INT_HISTOGRAM";
    case MetricDefinition::STRING_USED:
      return "STRING_USED";
    case MetricDefinition::CUSTOM:
      return "CUSTOM";
    default:
      LOG(FATAL) << "Invalid metric type encountered";
      std::exit(-1);
  }
}

std::string PrintReportType(ReportDefinition_ReportType type) {
  switch (type) {
    case ReportDefinition::REPORT_TYPE_UNSET:
      return "REPORT_TYPE_UNSET";
    case ReportDefinition::SIMPLE_OCCURRENCE_COUNT:
      return "SIMPLE_OCCURRENCE_COUNT";
    case ReportDefinition::EVENT_COMPONENT_OCCURRENCE_COUNT:
      return "EVENT_COMPONENT_OCCURRENCE_COUNT";
    case ReportDefinition::NUMERIC_AGGREGATION:
      return "NUMERIC_AGGREGATION";
    case ReportDefinition::INT_RANGE_HISTOGRAM:
      return "INT_RANGE_HISTOGRAM";
    case ReportDefinition::HIGH_FREQUENCY_STRING_COUNTS:
      return "HIGH_FREQUENCY_STRING_COUNTS";
    case ReportDefinition::STRING_COUNTS_WITH_THRESHOLD:
      return "STRING_COUNTS_WITH_THRESHOLD";
    case ReportDefinition::NUMERIC_PERF_RAW_DUMP:
      return "NUMERIC_PERF_RAW_DUMP";
    case ReportDefinition::UNIQUE_N_DAY_ACTIVES:
      return "UNIQUE_N_DAY_ACTIVES";
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS:
      return "PER_DEVICE_NUMERIC_STATS";
    case ReportDefinition::CUSTOM_RAW_DUMP:
      return "CUSTOM_RAW_DUMP";
    default:
      LOG(FATAL) << "Invalid report type encountered";
      std::exit(-1);
  }
}

}  // namespace

#endif  // PROTO_LITE

void TestApp::Show(const std::vector<std::string>& command) {
  // show config is currently the only show command.
  if (command.size() != 2 || command[1] != "config") {
    *ostream_ << "Expected 'show config'." << std::endl;
    return;
  }

  if (!current_metric_) {
    *ostream_ << "There is no current metric set." << std::endl;
  } else {
    *ostream_ << "Metric '" << current_metric_->metric_name() << "'" << std::endl;
    *ostream_ << "-----------------" << std::endl;
#ifndef PROTO_LITE
    *ostream_ << current_metric_->DebugString();
#else
    *ostream_ << "metric_name: \"" << current_metric_->metric_name() << "\"" << std::endl;
    *ostream_ << "metric_type: " << PrintMetricType(current_metric_->metric_type()) << std::endl;

    for (const auto& report : current_metric_->reports()) {
      *ostream_ << std::endl << "report_name: \"" << report.report_name() << "\"" << std::endl;
      *ostream_ << "report_type: " << PrintReportType(report.report_type()) << std::endl;
    }
#endif
    *ostream_ << std::endl;
  }
}

bool TestApp::ParseNonNegativeInt(const std::string& str, bool complain, int64_t* x) {
  CHECK(x);
  std::istringstream iss(str);
  *x = -1;
  iss >> *x;
  char c;
  if (*x <= -1 || iss.fail() || iss.get(c)) {
    if (complain) {
      if (mode_ == kInteractive) {
        *ostream_ << "Expected non-negative integer instead of " << str << "." << std::endl;
      } else {
        LOG(ERROR) << "Expected non-negative integer instead of " << str;
      }
    }
    return false;
  }
  return true;
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
        *ostream_ << "Expected positive integer instead of " << str << "." << std::endl;
      } else {
        LOG(ERROR) << "Expected positive integer instead of " << str;
      }
    }
    return false;
  }
  return true;
}

bool TestApp::ParseFloat(const std::string& str, bool complain, float* x) {
  CHECK(x);
  std::istringstream iss(str);
  *x = 0;
  iss >> *x;
  char c;
  if (iss.fail() || iss.get(c)) {
    if (complain) {
      if (mode_ == kInteractive) {
        *ostream_ << "Expected float instead of " << str << "." << std::endl;
      } else {
        LOG(ERROR) << "Expected float instead of " << str;
      }
    }
    return false;
  }
  return true;
}

bool TestApp::ParseIndex(const std::string& str, uint32_t* index) {
  CHECK(index);
  const auto index_len = strlen("index=");
  if (str.size() < index_len + 1) {
    return false;
  }
  if (str.substr(0, index_len) != "index=") {
    return false;
  }
  auto index_string = str.substr(index_len);
  std::istringstream iss(index_string);
  int64_t possible_index;
  iss >> possible_index;
  char c;
  if (iss.fail() || iss.get(c) || possible_index < 0 || possible_index > UINT32_MAX) {
    if (mode_ == kInteractive) {
      *ostream_ << "Expected small non-negative integer instead of " << index_string << "."
                << std::endl;
    } else {
      LOG(ERROR) << "Expected small non-negative integer instead of  " << index_string;
    }
    return false;
  }
  *index = possible_index;
  return true;
}

bool TestApp::ParseDay(const std::string& str, uint32_t* day_index) {
  CHECK(day_index);
  const auto day_len = strlen("day=");
  if (str.size() < day_len + 1 || str.substr(0, day_len) != "day=") {
    *ostream_ << "Expected prefix 'day='." << std::endl;
    return false;
  }
  auto day_string = str.substr(day_len);
  const auto today_len = strlen("today");

  // Handle the case where |day_string| is "today", "today+N", or "today-N".
  if (day_string.size() >= today_len && day_string.substr(0, today_len) == "today") {
    auto current_day_index = CurrentDayIndex();
    if (day_string.size() == today_len) {
      *day_index = current_day_index;
      return true;
    }

    if (day_string.size() > today_len + 1) {
      int64_t offset;
      if (!ParseNonNegativeInt(day_string.substr(today_len + 1), true, &offset)) {
        return false;
      }
      auto modifier = day_string.substr(today_len, 1);
      if (modifier == "+") {
        *day_index = (current_day_index + offset);
        return true;
      }
      if (modifier == "-") {
        if (offset > current_day_index) {
          *ostream_ << "Negative offset cannot be larger than the current day index." << std::endl;
          return false;
        }
        *day_index = (current_day_index - offset);
        return true;
      }
      return false;
    }
  }

  // Handle the case where |day_string| is an integer.
  std::istringstream iss(day_string);
  int64_t possible_day_index;
  iss >> possible_day_index;
  char c;
  if (iss.fail() || iss.get(c) || possible_day_index < 0 || possible_day_index > UINT32_MAX) {
    if (mode_ == kInteractive) {
      *ostream_ << "Expected small non-negative integer instead of " << day_string << "."
                << std::endl;
    } else {
      LOG(ERROR) << "Expected small non-negative integer instead of  " << day_string;
    }
    return false;
  }
  *day_index = possible_day_index;
  return true;
}

// Parses a string of the form <part>:<value> and writes <part> into |part_name|
// and <value> into |value|.
// Returns true if and only if this succeeds.
bool TestApp::ParsePartValuePair(const std::string& pair, std::string* part_name,
                                 std::string* value) {
  CHECK(part_name);
  CHECK(value);
  if (pair.size() < 3) {
    return false;
  }

  auto index1 = pair.find(':');
  if (index1 == std::string::npos || index1 == 0 || index1 > pair.size() - 2) {
    return false;
  }

  *part_name = std::string(pair, 0, index1);
  *value = std::string(pair, index1 + 1);

  return true;
}

CustomDimensionValue TestApp::ParseCustomDimensionValue(const std::string& value_string) {
  CustomDimensionValue value;
  int64_t int_val;
  uint32_t index;

  if (ParseInt(value_string, false, &int_val)) {
    value.set_int_value(int_val);
  } else if (ParseIndex(value_string, &index)) {
    value.set_index_value(index);
  } else {
    value.set_string_value(value_string);
  }
  return value;
}

EventValuesPtr TestApp::NewCustomEvent(std::vector<std::string> dimension_names,
                                       std::vector<std::string> values) {
  CHECK(dimension_names.size() == values.size());
  EventValuesPtr custom_event =
      std::make_unique<google::protobuf::Map<std::string, CustomDimensionValue>>();
  for (auto i = 0u; i < values.size(); i++) {
    (*custom_event)[dimension_names[i]] = ParseCustomDimensionValue(values[i]);
  }
  return custom_event;
}

}  // namespace cobalt

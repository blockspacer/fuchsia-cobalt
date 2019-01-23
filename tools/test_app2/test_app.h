// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_TOOLS_TEST_APP2_TEST_APP_H_
#define COBALT_TOOLS_TEST_APP2_TEST_APP_H_

#include <stdint.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "./observation2.pb.h"
#include "logger/logger.h"
#include "logger/project_context.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

class LoggerFactory {
 public:
  virtual ~LoggerFactory() = default;

  virtual std::unique_ptr<logger::LoggerInterface> NewLogger() = 0;

  virtual const logger::ProjectContext* project_context() = 0;

  virtual bool SendAccumulatedObservations() = 0;
};

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
  TestApp(std::unique_ptr<LoggerFactory> logger_factory,
          const std::string initial_metric_name, Mode mode,
          std::ostream* ostream);

  TestApp(const TestApp& other) = delete;
  TestApp(const TestApp&& other) = delete;
  void operator=(const TestApp& other) = delete;
  void operator=(const TestApp&& other) = delete;

  bool SetMetric(const std::string& metric_name);

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

  bool ProcessCommand(const std::vector<std::string>& command);

  void Log(const std::vector<std::string>& command);
  void LogEvent(uint64_t num_clients, const std::vector<std::string>& command);
  void LogEvent(size_t num_clients, uint32_t event_code);
  void LogEventCount(uint64_t num_clients,
                     const std::vector<std::string>& command);
  void LogEventCount(size_t num_clients, uint32_t event_code,
                     const std::string& component, int64_t duration,
                     int64_t count);
  void LogElapsedTime(uint64_t num_clients,
                      const std::vector<std::string>& command);
  void LogElapsedTime(uint64_t num_clients, uint32_t event_code,
                      const std::string& component, int64_t elapsed_micros);
  void LogFrameRate(uint64_t num_clients,
                    const std::vector<std::string>& command);
  void LogFrameRate(uint64_t num_clients, uint32_t event_code,
                    const std::string& component, float fps);
  void LogMemoryUsage(uint64_t num_clients,
                      const std::vector<std::string>& command);
  void LogMemoryUsage(uint64_t num_clients, uint32_t event_code,
                      const std::string& component, int64_t bytes);
  void LogIntHistogram(uint64_t num_clients,
                       const std::vector<std::string>& command);
  void LogIntHistogram(uint64_t num_clients, uint32_t event_code,
                       const std::string& component, int64_t bucket,
                       int64_t count);
  void LogCustomEvent(uint64_t num_clients,
                      const std::vector<std::string>& command);
  void LogCustomEvent(uint64_t num_clients,
                      const std::vector<std::string>& metric_parts,
                      const std::vector<std::string>& values);

  void ListParameters();

  void SetParameter(const std::vector<std::string>& command);

  void Send(const std::vector<std::string>& command);

  void Show(const std::vector<std::string>& command);

  bool ParseInt(const std::string& str, bool complain, int64_t* x);

  bool ParseFloat(const std::string& str, bool complain, float* x);

  bool ParseIndex(const std::string& str, uint32_t* index);

  bool ParseNonNegativeInt(const std::string& str, bool complain, int64_t* x);

  FRIEND_TEST(TestAppTest, ParseInt);
  FRIEND_TEST(TestAppTest, ParseFloat);
  FRIEND_TEST(TestAppTest, ParseIndex);
  FRIEND_TEST(TestAppTest, ParseNonNegativeInt);

  // Parses a string of the form <part>:<value> and writes <part> into
  // |part_name| and <value> into |value|.
  // Returns true if and only if this succeeds.
  bool ParsePartValuePair(const std::string& pair, std::string* part_name,
                          std::string* value);

  CustomDimensionValue ParseCustomDimensionValue(std::string value_string);

  logger::EventValuesPtr NewCustomEvent(
      std::vector<std::string> dimension_names,
      std::vector<std::string> values);

  const MetricDefinition* current_metric_;
  // The TestApp is in interactive mode unless set_mode() is invoked.
  Mode mode_ = kInteractive;
  std::unique_ptr<LoggerFactory> logger_factory_;
  std::ostream* ostream_;
};

}  // namespace cobalt
#endif  // COBALT_TOOLS_TEST_APP2_TEST_APP_H_

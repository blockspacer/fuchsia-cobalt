// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_BIN_TEST_APP_TEST_APP_H_
#define COBALT_SRC_BIN_TEST_APP_TEST_APP_H_

#include <stdint.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "src/lib/util/clock.h"
#include "src/logger/logger.h"
#include "src/logger/project_context.h"
#include "src/pb/observation2.pb.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

class LoggerFactory {
 public:
  virtual ~LoggerFactory() = default;

  // Creates a new Logger. If a nonzero day index is provided, then the Logger
  // will log events with that day index.
  // NOLINTNEXTLINE google-default-arguments
  virtual std::unique_ptr<logger::LoggerInterface> NewLogger(uint32_t day_index = 0) = 0;

  virtual const logger::ProjectContext* project_context() = 0;

  virtual size_t ObservationCount() = 0;

  virtual void ResetObservationCount() = 0;

  virtual void ResetLocalAggregation() = 0;

  virtual bool GenerateAggregatedObservations(uint32_t day_index) = 0;

  virtual bool SendAccumulatedObservations() = 0;
};

// The Cobalt testing client application.
class TestApp {
 public:
  static std::unique_ptr<TestApp> CreateFromFlagsOrDie(char* argv[]);

  // Modes of operation of the Cobalt test application. An instance of
  // TestApp is in interactive mode unless set_mode() is invoked.
  // set_mode() is invoked from CreateFromFlagsOrDie() in order to set the
  // mode to the one specified by the -mode flag.
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
  TestApp(std::unique_ptr<LoggerFactory> logger_factory, const std::string& initial_metric_name,
          Mode mode, std::ostream* ostream);

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
  bool ProcessCommandLine(const std::string& command_line);

  // Returns the current day index in UTC according to the test app's clock.
  uint32_t CurrentDayIndex();

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
  void LogEvent(size_t num_clients, uint32_t event_code, uint32_t day_index = 0u);
  void LogEventCount(uint64_t num_clients, const std::vector<std::string>& command);
  void LogEventCount(size_t num_clients, uint32_t event_code, const std::string& component,
                     int64_t duration, int64_t count, uint32_t day_index = 0u);
  void LogElapsedTime(uint64_t num_clients, const std::vector<std::string>& command);
  void LogElapsedTime(uint64_t num_clients, uint32_t event_code, const std::string& component,
                      int64_t elapsed_micros);
  void LogFrameRate(uint64_t num_clients, const std::vector<std::string>& command);
  void LogFrameRate(uint64_t num_clients, uint32_t event_code, const std::string& component,
                    float fps);
  void LogMemoryUsage(uint64_t num_clients, const std::vector<std::string>& command);
  void LogMemoryUsage(uint64_t num_clients, uint32_t event_code, const std::string& component,
                      int64_t bytes);
  void LogIntHistogram(uint64_t num_clients, const std::vector<std::string>& command);
  void LogIntHistogram(uint64_t num_clients, uint32_t event_code, const std::string& component,
                       int64_t bucket, int64_t count);
  void LogCustomEvent(uint64_t num_clients, const std::vector<std::string>& command);
  void LogCustomEvent(uint64_t num_clients, const std::vector<std::string>& metric_parts,
                      const std::vector<std::string>& values);

  // Generates all aggregated observations for a day index specified by
  // |command|.
  void GenerateAggregatedObservations(const std::vector<std::string>& command);

  // Deletes the local aggregates and the history of aggregated observations.
  void ResetLocalAggregation();

  void ListParameters();

  void SetParameter(const std::vector<std::string>& command);

  void Send(const std::vector<std::string>& command);

  void Show(const std::vector<std::string>& command);

  bool ParseInt(const std::string& str, bool complain, int64_t* x);

  bool ParseFloat(const std::string& str, bool complain, float* x);

  bool ParseIndex(const std::string& str, uint32_t* index);

  // Parses strings of the following forms:
  // day=today
  // day=today+N, where N is a nonnegative number
  // day=today-N, where N is a nonnegative number <= the current day index
  // day=K, where K is a day index
  // Computes the day index of that day in UTC, using |clock_| to get the
  // current day index if |str| begins with "day=today", and writes it to
  // |day_index|.
  bool ParseDay(const std::string& str, uint32_t* day_index);

  bool ParseNonNegativeInt(const std::string& str, bool complain, int64_t* x);

  FRIEND_TEST(TestAppTest, ParseInt);
  FRIEND_TEST(TestAppTest, ParseFloat);
  FRIEND_TEST(TestAppTest, ParseIndex);
  FRIEND_TEST(TestAppTest, ParseNonNegativeInt);
  FRIEND_TEST(TestAppTest, ParseDay);

  // Parses a string of the form <part>:<value> and writes <part> into
  // |part_name| and <value> into |value|.
  // Returns true if and only if this succeeds.
  bool ParsePartValuePair(const std::string& pair, std::string* part_name, std::string* value);

  CustomDimensionValue ParseCustomDimensionValue(const std::string& value_string);

  logger::EventValuesPtr NewCustomEvent(std::vector<std::string> dimension_names,
                                        std::vector<std::string> values);

  void GenerateAggregatedObservationsAndSend(uint32_t day_index);

  void ResetLocalAggregateStore();

  void ResetAggregatedObservationHistory();

  const MetricDefinition* current_metric_;
  // The TestApp is in interactive mode unless set_mode() is invoked.
  Mode mode_ = kInteractive;
  std::unique_ptr<LoggerFactory> logger_factory_;
  std::ostream* ostream_;
  util::SystemClockInterface* clock_;
};

}  // namespace cobalt

#endif  // COBALT_SRC_BIN_TEST_APP_TEST_APP_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_FAKE_LOGGER_H_
#define COBALT_LOGGER_FAKE_LOGGER_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "./gtest.h"
#include "config/project_configs.h"
#include "encoder/shipping_manager.h"
#include "logger/encoder.h"
#include "logger/local_aggregation.pb.h"
#include "logger/logger_interface.h"
#include "logger/project_context.h"
#include "util/consistent_proto_store.h"
#include "util/posix_file_system.h"
#include "util/status.h"

namespace cobalt {
namespace logger {
namespace testing {

// An implementation of LoggerInterface that counts how many times the Log*
// methods were called for purposes of testing that internal metrics are being
// collected properly.
class FakeLogger : public LoggerInterface {
 public:
  Status LogEvent(uint32_t metric_id, uint32_t event_code) override;

  Status LogEventCount(uint32_t metric_id,
                       const std::vector<uint32_t>& event_codes,
                       const std::string& component,
                       int64_t period_duration_micros, uint32_t count) override;

  Status LogElapsedTime(uint32_t metric_id,
                        const std::vector<uint32_t>& event_codes,
                        const std::string& component,
                        int64_t elapsed_micros) override;

  Status LogFrameRate(uint32_t metric_id,
                      const std::vector<uint32_t>& event_codes,
                      const std::string& component, float fps) override;

  Status LogMemoryUsage(uint32_t metric_id,
                        const std::vector<uint32_t>& event_codes,
                        const std::string& component, int64_t bytes) override;

  Status LogIntHistogram(uint32_t metric_id,
                         const std::vector<uint32_t>& event_codes,
                         const std::string& component,
                         HistogramPtr histogram) override;

  Status LogString(uint32_t metric_id, const std::string& str) override;

  Status LogCustomEvent(uint32_t metric_id,
                        EventValuesPtr event_values) override;

  uint32_t call_count() { return call_count_; }
  Event last_event_logged() { return last_event_logged_; }

 private:
  Event last_event_logged_;
  uint32_t call_count_ = 0;
};

}  // namespace testing
}  // namespace logger
}  // namespace cobalt

#endif  //  COBALT_LOGGER_FAKE_LOGGER_H_

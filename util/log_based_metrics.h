// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes macros to make it easier to log in a standardized format
// so that we can create logs-based metrics for stackdriver.
//
// https://cloud.google.com/logging/docs/logs-based-metrics/

#ifndef COBALT_UTIL_LOG_BASED_METRICS_H_
#define COBALT_UTIL_LOG_BASED_METRICS_H_

#define LOG_STACKDRIVER_METRIC(level, metric_id) LOG(level) << "$@LBSDM@$ [" << (metric_id) << "] "

#define LOG_BOOL_STACKDRIVER_METRIC(level, metric_id, value) \
  LOG_STACKDRIVER_METRIC(level, metric_id) << "B[" << (value) << "] "

#define LOG_INT_STACKDRIVER_METRIC(level, metric_id, value) \
  LOG_STACKDRIVER_METRIC(level, metric_id) << "I[" << (value) << "] "

#define LOG_STACKDRIVER_COUNT_METRIC(level, metric_id) LOG_STACKDRIVER_METRIC(level, metric_id)

#define LOG_STRING_STACKDRIVER_METRIC(level, metric_id, value) \
  LOG_STACKDRIVER_METRIC(level, metric_id) << "S[" << (value) << "] "

#endif  // COBALT_UTIL_LOG_BASED_METRICS_H_

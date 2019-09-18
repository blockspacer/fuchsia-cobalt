// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOGGER_TESTING_CONSTANTS_H_
#define COBALT_SRC_LOGGER_TESTING_CONSTANTS_H_

#include "src/logger/logger_test_utils.h"

// Generated from all_reports_test_registry.yaml
// Namespace: cobalt::logger::testing::all_report_types
#include "src/logger/test_registries/all_report_types_test_registry.cb.h"
// Generated from mixed_time_zone_test_registry.yaml
// Namespace: cobalt::logger::testing::mixed_time_zone
#include "src/logger/test_registries/mixed_time_zone_test_registry.cb.h"
// Generated from per_device_numeric_stats_test_registry.yaml
// Namespace: cobalt::logger::testing::per_device_numeric_stats
#include "src/logger/test_registries/per_device_numeric_stats_test_registry.cb.h"
// Generated from per_device_histogram_test_registry.yaml
// Namespace: cobalt::logger::testing::per_device_histogram
#include "src/logger/test_registries/per_device_histogram_test_registry.cb.h"
// Generated from unique_actives_noise_free_test_registry.yaml
// Namespace: cobalt::logger::testing::unique_actives_noise_free
#include "src/logger/test_registries/unique_actives_noise_free_test_registry.cb.h"
// Generated from unique_actives_test_registry.yaml
// Namespace: cobalt::logger::testing::unique_actives
#include "src/logger/test_registries/unique_actives_test_registry.cb.h"

namespace cobalt {
namespace logger {
namespace testing {

// Constants specific to the registry defined in
// test_registries/all_report_types_test_registry.yaml
namespace all_report_types {

// MetricReportIds of the locally aggregated reports in this registry
constexpr MetricReportId kDeviceBootsMetricReportId =
    MetricReportId(kDeviceBootsMetricId, kDeviceBootsUniqueDevicesReportId);
constexpr MetricReportId kFeaturesActiveMetricReportId =
    MetricReportId(kFeaturesActiveMetricId, kFeaturesActiveUniqueDevicesReportId);
constexpr MetricReportId kEventsOccurredMetricReportId =
    MetricReportId(kEventsOccurredMetricId, kEventsOccurredUniqueDevicesReportId);
constexpr MetricReportId kSettingsChangedMetricReportId =
    MetricReportId(kSettingsChangedMetricId, kSettingsChangedPerDeviceCountReportId);
constexpr MetricReportId kConnectionFailuresMetricReportId =
    MetricReportId(kConnectionFailuresMetricId, kConnectionFailuresPerDeviceCountReportId);
constexpr MetricReportId kStreamingTimeTotalMetricReportId =
    MetricReportId(kStreamingTimeMetricId, kStreamingTimePerDeviceTotalReportId);
constexpr MetricReportId kStreamingTimeMinMetricReportId =
    MetricReportId(kStreamingTimeMetricId, kStreamingTimePerDeviceMinReportId);
constexpr MetricReportId kStreamingTimeMaxMetricReportId =
    MetricReportId(kStreamingTimeMetricId, kStreamingTimePerDeviceMaxReportId);
constexpr MetricReportId kLoginModuleFrameRateMinMetricReportId =
    MetricReportId(kLoginModuleFrameRateMetricId, kLoginModuleFrameRatePerDeviceMinReportId);
constexpr MetricReportId kLedgerMemoryUsageMaxMetricReportId =
    MetricReportId(kLedgerMemoryUsageMetricId, kLedgerMemoryUsagePerDeviceMaxReportId);

// Expected parameters of the locally aggregated reports in this registry
const ExpectedAggregationParams kExpectedAggregationParams = {
    34,

    {kDeviceBootsMetricReportId, kFeaturesActiveMetricReportId, kEventsOccurredMetricReportId,
     kSettingsChangedMetricReportId, kConnectionFailuresMetricReportId,
     kStreamingTimeTotalMetricReportId, kStreamingTimeMinMetricReportId,
     kStreamingTimeMaxMetricReportId, kLoginModuleFrameRateMinMetricReportId,
     kLedgerMemoryUsageMaxMetricReportId},

    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 15},
     {kEventsOccurredMetricReportId, 10},
     {kSettingsChangedMetricReportId, 1},
     {kConnectionFailuresMetricReportId, 1},
     {kStreamingTimeTotalMetricReportId, 1},
     {kStreamingTimeMinMetricReportId, 1},
     {kStreamingTimeMaxMetricReportId, 1},
     {kLoginModuleFrameRateMinMetricReportId, 1},
     {kLedgerMemoryUsageMaxMetricReportId, 1}},

    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 5},
     {kEventsOccurredMetricReportId, 5}},

    {{kDeviceBootsMetricReportId, {1}},
     {kFeaturesActiveMetricReportId, {1, 7, 30}},
     {kEventsOccurredMetricReportId, {1, 7}},
     {kSettingsChangedMetricReportId, {7, 30}},
     {kConnectionFailuresMetricReportId, {1}},
     {kStreamingTimeTotalMetricReportId, {1, 7}},
     {kStreamingTimeMinMetricReportId, {1, 7}},
     {kStreamingTimeMaxMetricReportId, {1, 7}},
     {kLoginModuleFrameRateMinMetricReportId, {1, 7}},
     {kLedgerMemoryUsageMaxMetricReportId, {1, 7}}}};

}  // namespace all_report_types

// Constants specific to the registry defined in
// test_registries/mixed_time_zone_test_registry.yaml
namespace mixed_time_zone {

// MetricReportIds of the locally aggregated reports in this registry
constexpr MetricReportId kDeviceBootsMetricReportId =
    MetricReportId(kDeviceBootsMetricId, kDeviceBootsUniqueDevicesReportId);
constexpr MetricReportId kFeaturesActiveMetricReportId =
    MetricReportId(kFeaturesActiveMetricId, kFeaturesActiveUniqueDevicesReportId);

// Expected parameters of the locally aggregated reports in this registry
const ExpectedAggregationParams kExpectedAggregationParams = {
    6,

    {kDeviceBootsMetricReportId, kFeaturesActiveMetricReportId},

    {{kDeviceBootsMetricReportId, 3}, {kFeaturesActiveMetricReportId, 3}},

    {{kDeviceBootsMetricReportId, 3}, {kFeaturesActiveMetricReportId, 3}},

    {{kDeviceBootsMetricReportId, {1}}, {kFeaturesActiveMetricReportId, {1}}}};

}  // namespace mixed_time_zone

// Constants specific to the registry defined in
// test_registries/per_device_numeric_stats_test_registry.yaml
namespace per_device_numeric_stats {

// MetricReportIds of the locally aggregated reports in this registry
constexpr MetricReportId kSettingsChangedMetricReportId =
    MetricReportId(kSettingsChangedMetricId, kSettingsChangedPerDeviceCountReportId);
constexpr MetricReportId kConnectionFailuresMetricReportId =
    MetricReportId(kConnectionFailuresMetricId, kConnectionFailuresPerDeviceCountReportId);
constexpr MetricReportId kStreamingTimeTotalMetricReportId =
    MetricReportId(kStreamingTimeMetricId, kStreamingTimePerDeviceTotalReportId);
constexpr MetricReportId kStreamingTimeMinMetricReportId =
    MetricReportId(kStreamingTimeMetricId, kStreamingTimePerDeviceMinReportId);
constexpr MetricReportId kStreamingTimeMaxMetricReportId =
    MetricReportId(kStreamingTimeMetricId, kStreamingTimePerDeviceMaxReportId);
constexpr MetricReportId kLoginModuleFrameRateMinMetricReportId =
    MetricReportId(kLoginModuleFrameRateMetricId, kLoginModuleFrameRatePerDeviceMinReportId);
constexpr MetricReportId kLedgerMemoryUsageMaxMetricReportId =
    MetricReportId(kLedgerMemoryUsageMetricId, kLedgerMemoryUsagePerDeviceMaxReportId);

// Expected parameters of the locally aggregated reports in this registry
const ExpectedAggregationParams kExpectedAggregationParams = {
    7,

    {kSettingsChangedMetricReportId, kConnectionFailuresMetricReportId,
     kStreamingTimeTotalMetricReportId, kStreamingTimeMinMetricReportId,
     kStreamingTimeMaxMetricReportId, kLoginModuleFrameRateMinMetricReportId,
     kLedgerMemoryUsageMaxMetricReportId},

    {{kSettingsChangedMetricReportId, 1},
     {kConnectionFailuresMetricReportId, 1},
     {kStreamingTimeTotalMetricReportId, 1},
     {kStreamingTimeMinMetricReportId, 1},
     {kStreamingTimeMaxMetricReportId, 1},
     {kLoginModuleFrameRateMinMetricReportId, 1},
     {kLedgerMemoryUsageMaxMetricReportId, 1}},

    {},

    {{kSettingsChangedMetricReportId, {7, 30}},
     {kConnectionFailuresMetricReportId, {1}},
     {kStreamingTimeTotalMetricReportId, {1, 7}},
     {kStreamingTimeMinMetricReportId, {1, 7}},
     {kStreamingTimeMaxMetricReportId, {1, 7}},
     {kLoginModuleFrameRateMinMetricReportId, {1, 7}},
     {kLedgerMemoryUsageMaxMetricReportId, {1, 7}}}};

}  // namespace per_device_numeric_stats

// Constants specific to the registry defined in
// test_registries/unique_actives_test_registry.yaml
namespace unique_actives {

// MetricReportIds of the locally aggregated reports in this registry
constexpr MetricReportId kDeviceBootsMetricReportId =
    MetricReportId(kDeviceBootsMetricId, kDeviceBootsUniqueDevicesReportId);
constexpr MetricReportId kFeaturesActiveMetricReportId =
    MetricReportId(kFeaturesActiveMetricId, kFeaturesActiveUniqueDevicesReportId);
constexpr MetricReportId kNetworkActivityMetricReportId =
    MetricReportId(kNetworkActivityMetricId, kNetworkActivityUniqueDevicesReportId);

// Expected parameters of the locally aggregated reports in this registry
const ExpectedAggregationParams kExpectedAggregationParams = {
    21,

    {kDeviceBootsMetricReportId, kFeaturesActiveMetricReportId, kNetworkActivityMetricReportId},

    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 10},
     {kNetworkActivityMetricReportId, 9}},

    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 5},
     {kNetworkActivityMetricReportId, 3}},

    {{kDeviceBootsMetricReportId, {1}},
     {kFeaturesActiveMetricReportId, {7, 30}},
     {kNetworkActivityMetricReportId, {1, 7, 30}}}};

}  // namespace unique_actives

// Constants specific to the registry defined in
// test_registries/unique_actives_noise_free_test_registry.yaml
namespace unique_actives_noise_free {

// MetricReportIds of the locally aggregated reports in this registry
constexpr MetricReportId kDeviceBootsMetricReportId =
    MetricReportId(kDeviceBootsMetricId, kDeviceBootsUniqueDevicesReportId);
constexpr MetricReportId kFeaturesActiveMetricReportId =
    MetricReportId(kFeaturesActiveMetricId, kFeaturesActiveUniqueDevicesReportId);
constexpr MetricReportId kEventsOccurredMetricReportId =
    MetricReportId(kEventsOccurredMetricId, kEventsOccurredUniqueDevicesReportId);

// Expected parameters of the locally aggregated reports in this registry
const ExpectedAggregationParams kExpectedAggregationParams = {
    27,

    {kDeviceBootsMetricReportId, kFeaturesActiveMetricReportId, kEventsOccurredMetricReportId},

    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 15},
     {kEventsOccurredMetricReportId, 10}},

    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 5},
     {kEventsOccurredMetricReportId, 5}},

    {{kDeviceBootsMetricReportId, {1}},
     {kFeaturesActiveMetricReportId, {1, 7, 30}},
     {kEventsOccurredMetricReportId, {1, 7}}}};

}  // namespace unique_actives_noise_free

}  // namespace testing
}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_SRC_LOGGER_TESTING_CONSTANTS_H_

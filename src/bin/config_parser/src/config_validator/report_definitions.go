// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"fmt"
)

// This file contains logic to validate list of ReportDefinition protos in MetricDefinition protos.

// Maps MetricTypes to valid ReportTypes.
var allowedReportTypes = map[config.MetricDefinition_MetricType]map[config.ReportDefinition_ReportType]bool{
	config.MetricDefinition_EVENT_OCCURRED: map[config.ReportDefinition_ReportType]bool{
		config.ReportDefinition_SIMPLE_OCCURRENCE_COUNT: true,
		config.ReportDefinition_UNIQUE_N_DAY_ACTIVES:    true,
	},
	config.MetricDefinition_EVENT_COUNT: map[config.ReportDefinition_ReportType]bool{
		config.ReportDefinition_EVENT_COMPONENT_OCCURRENCE_COUNT: true,
		config.ReportDefinition_INT_RANGE_HISTOGRAM:              true,
		config.ReportDefinition_NUMERIC_AGGREGATION:              true,
		config.ReportDefinition_PER_DEVICE_NUMERIC_STATS:         true,
		config.ReportDefinition_PER_DEVICE_HISTOGRAM:             true,
	},
	config.MetricDefinition_ELAPSED_TIME: map[config.ReportDefinition_ReportType]bool{
		config.ReportDefinition_INT_RANGE_HISTOGRAM:      true,
		config.ReportDefinition_NUMERIC_AGGREGATION:      true,
		config.ReportDefinition_NUMERIC_PERF_RAW_DUMP:    true,
		config.ReportDefinition_PER_DEVICE_NUMERIC_STATS: true,
		config.ReportDefinition_PER_DEVICE_HISTOGRAM:     true,
	},
	config.MetricDefinition_FRAME_RATE: map[config.ReportDefinition_ReportType]bool{
		config.ReportDefinition_INT_RANGE_HISTOGRAM:      true,
		config.ReportDefinition_NUMERIC_AGGREGATION:      true,
		config.ReportDefinition_NUMERIC_PERF_RAW_DUMP:    true,
		config.ReportDefinition_PER_DEVICE_NUMERIC_STATS: true,
		config.ReportDefinition_PER_DEVICE_HISTOGRAM:     true,
	},
	config.MetricDefinition_MEMORY_USAGE: map[config.ReportDefinition_ReportType]bool{
		config.ReportDefinition_INT_RANGE_HISTOGRAM:      true,
		config.ReportDefinition_NUMERIC_AGGREGATION:      true,
		config.ReportDefinition_NUMERIC_PERF_RAW_DUMP:    true,
		config.ReportDefinition_PER_DEVICE_NUMERIC_STATS: true,
		config.ReportDefinition_PER_DEVICE_HISTOGRAM:     true,
	},
	config.MetricDefinition_INT_HISTOGRAM: map[config.ReportDefinition_ReportType]bool{
		config.ReportDefinition_INT_RANGE_HISTOGRAM: true,
	},
	config.MetricDefinition_STRING_USED: map[config.ReportDefinition_ReportType]bool{
		config.ReportDefinition_HIGH_FREQUENCY_STRING_COUNTS: true,
		config.ReportDefinition_STRING_COUNTS_WITH_THRESHOLD: true,
	},
	config.MetricDefinition_CUSTOM: map[config.ReportDefinition_ReportType]bool{
		config.ReportDefinition_CUSTOM_RAW_DUMP: true,
	},
}

func validateReportDefinitions(m config.MetricDefinition) error {
	reportIds := map[uint32]int{}
	reportNames := map[string]bool{}
	for i, r := range m.Reports {
		if _, ok := reportIds[r.Id]; ok {
			return fmt.Errorf("There are two reports with id=%v.", r.Id)
		}
		reportIds[r.Id] = i

		if _, ok := reportNames[r.ReportName]; ok {
			return fmt.Errorf("There are two reports with name=%v.", r.ReportName)
		}
		reportNames[r.ReportName] = true

		if err := validateReportDefinitionForMetric(m, *r); err != nil {
			return fmt.Errorf("Error validating report '%s': %v", r.ReportName, err)
		}
	}

	return nil
}

// Validate a single instance of a ReportDefinition with its associated metric.
func validateReportDefinitionForMetric(m config.MetricDefinition, r config.ReportDefinition) error {
	if err := validateReportType(m.MetricType, r.ReportType); err != nil {
		return err
	}

	if err := validateReportDefinition(r); err != nil {
		return err
	}

	return nil
}

// Validate a single instance of a ReportDefinition.
func validateReportDefinition(r config.ReportDefinition) error {
	if !validNameRegexp.MatchString(r.ReportName) {
		return fmt.Errorf("Invalid report name. Report names must match the regular expression '%v'.", validNameRegexp)
	}

	if r.Id == 0 {
		return fmt.Errorf("Report ID of zero is not allowed. Please specify a positive report ID.")
	}

	if err := validateReportDefinitionForType(r); err != nil {
		return fmt.Errorf("Report %s: %v", r.ReportName, err)
	}

	return nil
}

// Validates that the MetricType and ReportType provided are compatible.
func validateReportType(mt config.MetricDefinition_MetricType, rt config.ReportDefinition_ReportType) error {
	if rt == config.ReportDefinition_REPORT_TYPE_UNSET {
		return fmt.Errorf("report_type is not set.")
	}

	rts, ok := allowedReportTypes[mt]
	if !ok {
		return fmt.Errorf("Unknown metric type %s.", mt)
	}

	if _, ok = rts[rt]; !ok {
		return fmt.Errorf("Reports of type %s cannot be used with metrics of type %s", rt, mt)
	}

	return nil
}

/////////////////////////////////////////////////////////////////
// Validation for specific report types:
/////////////////////////////////////////////////////////////////

// Validates ReportDefinitions of some types.
func validateReportDefinitionForType(r config.ReportDefinition) error {
	switch r.ReportType {
	case config.ReportDefinition_SIMPLE_OCCURRENCE_COUNT:
		return validateSimpleOccurrenceCountReportDef(r)
	case config.ReportDefinition_HIGH_FREQUENCY_STRING_COUNTS:
		return validateHighFrequencyStringCountsReportDef(r)
	case config.ReportDefinition_UNIQUE_N_DAY_ACTIVES:
		return validateUniqueActivesReportDef(r)
	case config.ReportDefinition_PER_DEVICE_NUMERIC_STATS:
		return validatePerDeviceNumericStatsReportDef(r)
	case config.ReportDefinition_PER_DEVICE_HISTOGRAM:
		return validatePerDeviceHistogramReportDef(r)
	case config.ReportDefinition_NUMERIC_AGGREGATION:
		return validateNumericAggregationReportDef(r)
	}

	return nil
}

func validateSimpleOccurrenceCountReportDef(r config.ReportDefinition) error {
	if err := validateLocalPrivacyNoiseLevel(r); err != nil {
		return err
	}

	return nil
}

func validateHighFrequencyStringCountsReportDef(r config.ReportDefinition) error {
	if err := validateLocalPrivacyNoiseLevel(r); err != nil {
		return err
	}

	return nil
}

func validateUniqueActivesReportDef(r config.ReportDefinition) error {
	if err := validateWindowSize(r); err != nil {
		return err
	}
	if err := validateLocalPrivacyNoiseLevel(r); err != nil {
		return err
	}

	return nil
}

func validatePerDeviceNumericStatsReportDef(r config.ReportDefinition) error {
	if err := validateWindowSize(r); err != nil {
		return err
	}

	return nil
}

func validatePerDeviceHistogramReportDef(r config.ReportDefinition) error {
	if r.IntBuckets == nil {
		return fmt.Errorf("No int_buckets specified for report of type %s.", r.ReportType)
	}

	if err := validateWindowSize(r); err != nil {
		return err
	}

	return nil
}

func validateNumericAggregationReportDef(r config.ReportDefinition) error {
	if err := validatePercentiles(r); err != nil {
		return err
	}

	return nil
}

/////////////////////////////////////////////////////////////////
// Validation for specific fields of report definitions
//
// These functions are oblivious to the report's type and should
// only be called with reports for which that field is required.
/////////////////////////////////////////////////////////////////

// Check that the report definition has a nonempty window_size field, and that none of the window sizes
// are UNSET.
func validateWindowSize(r config.ReportDefinition) error {
	if len(r.WindowSize) == 0 {
		return fmt.Errorf("No window_size specified for report of type %s.", r.ReportType)
	}
	for _, ws := range r.WindowSize {
		if ws == config.WindowSize_UNSET {
			return fmt.Errorf("Unset window size found for report of type %s.", r.ReportType)
		}
	}

	return nil
}

// Check that the local_privacy_noise_level field is set.
func validateLocalPrivacyNoiseLevel(r config.ReportDefinition) error {
	if r.LocalPrivacyNoiseLevel == config.ReportDefinition_NOISE_LEVEL_UNSET {
		return fmt.Errorf("No local_privacy_noise_level specified for report of type %s.", r.ReportType)
	}

	return nil
}

// Check that the percentiles are between 0 and 100.
func validatePercentiles(r config.ReportDefinition) error {
	for _, percentage := range r.Percentiles {
		if percentage < 0 || percentage > 100 {
			return fmt.Errorf(
				"Percentiles must be in the range 0-100.  Received value %d for report of type %s.",
				percentage, r.ReportType)
		}
	}

	return nil
}

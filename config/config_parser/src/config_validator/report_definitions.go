// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"config_parser"
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
		config.ReportDefinition_PER_DEVICE_NUMERIC_STATS:         true,
	},
	config.MetricDefinition_ELAPSED_TIME: map[config.ReportDefinition_ReportType]bool{
		config.ReportDefinition_NUMERIC_AGGREGATION:      true,
		config.ReportDefinition_INT_RANGE_HISTOGRAM:      true,
		config.ReportDefinition_NUMERIC_PERF_RAW_DUMP:    true,
		config.ReportDefinition_PER_DEVICE_NUMERIC_STATS: true,
	},
	config.MetricDefinition_FRAME_RATE: map[config.ReportDefinition_ReportType]bool{
		config.ReportDefinition_NUMERIC_AGGREGATION:   true,
		config.ReportDefinition_INT_RANGE_HISTOGRAM:   true,
		config.ReportDefinition_NUMERIC_PERF_RAW_DUMP: true,
	},
	config.MetricDefinition_MEMORY_USAGE: map[config.ReportDefinition_ReportType]bool{
		config.ReportDefinition_NUMERIC_AGGREGATION:   true,
		config.ReportDefinition_INT_RANGE_HISTOGRAM:   true,
		config.ReportDefinition_NUMERIC_PERF_RAW_DUMP: true,
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
	for _, r := range m.Reports {
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

	if r.Id != config_parser.IdFromName(r.ReportName) {
		return fmt.Errorf("Report id specified in the config file. Report ids may not be set by users.")
	}

	if r.Id == 0 {
		return fmt.Errorf("Report hashes to a zero report id. This is invalid. Please change the report name.")
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

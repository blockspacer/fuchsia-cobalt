// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"config_parser"
	"testing"
)

// makeValidReport returns a valid instance of config.ReportDefinition which
// can be modified to fail various validation checks for testing purposes.
func makeValidReport() config.ReportDefinition {
	return makeValidReportWithName("the_report_name")
}

func makeValidReportWithName(name string) config.ReportDefinition {
	return config.ReportDefinition{
		Id:         config_parser.IdFromName(name),
		ReportName: name,
		ReportType: config.ReportDefinition_EVENT_COMPONENT_OCCURRENCE_COUNT,
	}
}

// Test that makeValidReport returns a valid report.
func TestValidateMakeValidReport(t *testing.T) {
	r := makeValidReport()
	if err := validateReportDefinition(r); err != nil {
		t.Errorf("Rejected valid report: %v", err)
	}
}

func TestValidateCorrectReportId(t *testing.T) {
	r := makeValidReport()
	r.Id += 1

	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report with wrong report id.")
	}
}

func TestValidateInvalidName(t *testing.T) {
	r := makeValidReportWithName("_invalid_name")

	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report with invalid name.")
	}
}

func TestValidateZeroReportId(t *testing.T) {
	r := makeValidReportWithName("NRaMinLNcqiYmgEypLLVGnXymNpxJzqabtbbjLycCMEohvVzZtAYpah")

	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report with 0 id.")
	}
}

func TestValidateUnsetReportType(t *testing.T) {
	if err := validateReportType(config.MetricDefinition_EVENT_OCCURRED, config.ReportDefinition_REPORT_TYPE_UNSET); err == nil {
		t.Error("Accepted report with no report type set.")
	}
}

// Test that validateLocalPrivacyNoiseLevel accepts a ReportDefinition if and only if it has a local_privacy_noise_level field.
func TestValidateLocalPrivacyNoiseLevel(t *testing.T) {
	r := makeValidReport()

	if err := validateLocalPrivacyNoiseLevel(r); err == nil {
		t.Error("Accepted report definition with local_privacy_noise_level unset.")
	}

	r.LocalPrivacyNoiseLevel = config.ReportDefinition_SMALL
	if err := validateLocalPrivacyNoiseLevel(r); err != nil {
		t.Errorf("Rejected report definition with local_privacy_noise_level set: %v", err)
	}
}

// Test that validateWindowSize rejects a ReportDefinition with no window_size field, and a ReportDefinition with a
// window_size field where one of the window sizes is UNSET.
func TestValidateWindowSize(t *testing.T) {
	r := makeValidReport()

	if err := validateWindowSize(r); err == nil {
		t.Error("Accepted report definition without window_size field.")
	}

	r.WindowSize = []config.WindowSize{config.WindowSize_WINDOW_1_DAY, config.WindowSize_UNSET}
	if err := validateWindowSize(r); err == nil {
		t.Error("Accepted report definition with an UNSET window size.")
	}

	r.WindowSize = []config.WindowSize{config.WindowSize_WINDOW_1_DAY, config.WindowSize_WINDOW_7_DAYS}
	if err := validateWindowSize(r); err != nil {
		t.Errorf("Rejected report definition with valid window_size field: %v", err)
	}
}

// Test that local_privacy_noise_level must be set if the report type is SIMPLE_OCCURRENCE_COUNT.
func TestValidateReportDefinitionForSimpleOccurrenceCount(t *testing.T) {
	r := makeValidReport()
	r.ReportType = config.ReportDefinition_SIMPLE_OCCURRENCE_COUNT

	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report definition of type SIMPLE_OCCURRENCE_COUNT with local_privacy_noise_level unset.")
	}
}

// Test that local_privacy_noise_level must be set if the report type is HIGH_FREQUENCY_STRING_COUNTS.
func TestValidateReportDefinitionForHighFrequencyStringCounts(t *testing.T) {
	r := makeValidReport()
	r.ReportType = config.ReportDefinition_HIGH_FREQUENCY_STRING_COUNTS

	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report definition of type HIGH_FREQUENCY_STRING_COUNTS with local_privacy_noise_level unset.")
	}
}

// Test that local_privacy_noise_level and window_size must be set if the report type is UNIQUE_N_DAY_ACTIVES.
func TestValidateReportDefinitionForUniqueActives(t *testing.T) {
	r := makeValidReport()
	r.ReportType = config.ReportDefinition_UNIQUE_N_DAY_ACTIVES

	// Add a valid window_size, but not a local_privacy_noise_level.
	r.WindowSize = []config.WindowSize{config.WindowSize_WINDOW_1_DAY}
	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report definition of type UNIQUE_N_DAY_ACTIVES with local_privacy_noise_level unset.")
	}

	// Remove window_size and add a local_privacy_noise_level.
	r.WindowSize = []config.WindowSize{}
	r.LocalPrivacyNoiseLevel = config.ReportDefinition_SMALL
	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report definition of type UNIQUE_N_DAY_ACTIVES without window_size field.")
	}

	// Add a valid window size to window_size, but also add an UNSET window size.
	r.WindowSize = []config.WindowSize{config.WindowSize_WINDOW_1_DAY, config.WindowSize_UNSET}
	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report definition of type UNIQUE_N_DAY_ACTIVES with window_size field containing an UNSET window size.")
	}
}

// Test that window_size must be set if the report type is PER_DEVICE_NUMERIC_STATS.
func TestValidateReportDefinitionForPerDeviceNumericStats(t *testing.T) {
	r := makeValidReport()
	r.ReportType = config.ReportDefinition_PER_DEVICE_NUMERIC_STATS

	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report definition of type PER_DEVICE_NUMERIC_STATS without window_size field.")
	}

	// Add a valid window size to window_size, but also add an UNSET window size.
	r.WindowSize = []config.WindowSize{config.WindowSize_WINDOW_1_DAY, config.WindowSize_UNSET}
	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report definition of type PER_DEVICE_NUMERIC_STATS with window_size field containing an UNSET window size.")
	}
}

// Test percentiles range for NUMERIC_AGGREGATION report types.
func TestValidateReportDefinitionForNumericAggregation(t *testing.T) {
	r := makeValidReport()
	r.ReportType = config.ReportDefinition_NUMERIC_AGGREGATION

	// Test that percentiles are optional.
	if err := validateReportDefinition(r); err != nil {
		t.Error("Rejected report definition of type NUMERIC_AGGREGATION with no percentiles set.")
	}

	// Test that percentiles between 0 and 100 are accepted.
	r.Percentiles = []uint32{0, 25, 50, 75, 90, 99, 100}
	if err := validateReportDefinition(r); err != nil {
		t.Error("Rejected report definition of type NUMERIC_AGGREGATION with valid percentiles.")
	}

	// Test that out of bounds percentiles are rejected.
	r.Percentiles = []uint32{0, 25, 50, 75, 100, 101}
	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report definition of type NUMERIC_AGGREGATION with invalid percentiles.")
	}
}

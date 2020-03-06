// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"testing"
)

// makeValidReport returns a valid instance of config.ReportDefinition which
// can be modified to fail various validation checks for testing purposes.
func makeValidReport() config.ReportDefinition {
	return makeValidReportWithNameAndType("the_report_name", config.ReportDefinition_EVENT_COMPONENT_OCCURRENCE_COUNT)
}

func makeValidReportWithType(t config.ReportDefinition_ReportType) config.ReportDefinition {
	return makeValidReportWithNameAndType("the_report_name", t)
}

func makeValidReportWithName(name string) config.ReportDefinition {
	return makeValidReportWithNameAndType(name, config.ReportDefinition_EVENT_COMPONENT_OCCURRENCE_COUNT)
}

func makeValidReportWithNameAndType(name string, t config.ReportDefinition_ReportType) config.ReportDefinition {
	return config.ReportDefinition{
		Id:         10,
		ReportName: name,
		ReportType: t,
	}
}

// Given a metric type |mt|, returns a valid report type for that metric type.
// Prefers to return a non-histogram report type if the given metric type supports
// one, to aid in checking where int_buckets get set in tests.
func getMatchingReportType(mt config.MetricDefinition_MetricType) config.ReportDefinition_ReportType {
	switch mt {
	case config.MetricDefinition_EVENT_OCCURRED:
		return config.ReportDefinition_SIMPLE_OCCURRENCE_COUNT
	case config.MetricDefinition_EVENT_COUNT:
		return config.ReportDefinition_EVENT_COMPONENT_OCCURRENCE_COUNT
	case config.MetricDefinition_ELAPSED_TIME:
		return config.ReportDefinition_NUMERIC_AGGREGATION
	case config.MetricDefinition_FRAME_RATE:
		return config.ReportDefinition_NUMERIC_AGGREGATION
	case config.MetricDefinition_MEMORY_USAGE:
		return config.ReportDefinition_NUMERIC_AGGREGATION
	case config.MetricDefinition_INT_HISTOGRAM:
		return config.ReportDefinition_INT_RANGE_HISTOGRAM
	case config.MetricDefinition_CUSTOM:
		return config.ReportDefinition_CUSTOM_RAW_DUMP
	case config.MetricDefinition_OCCURRENCE:
		return config.ReportDefinition_FLEETWIDE_OCCURRENCE_COUNTS
	case config.MetricDefinition_INTEGER:
		return config.ReportDefinition_FLEETWIDE_MEANS
	case config.MetricDefinition_INTEGER_HISTOGRAM:
		return config.ReportDefinition_FLEETWIDE_HISTOGRAMS
	case config.MetricDefinition_STRING:
		return config.ReportDefinition_STRING_HISTOGRAMS
	}

	return config.ReportDefinition_REPORT_TYPE_UNSET
}

// Test that makeValidReport returns a valid report.
func TestValidateMakeValidReport(t *testing.T) {
	r := makeValidReport()
	if err := validateReportDefinition(r); err != nil {
		t.Errorf("Rejected valid report: %v", err)
	}
}

func TestDifferentReportId(t *testing.T) {
	r := makeValidReport()
	r.Id += 1

	if err := validateReportDefinition(r); err != nil {
		t.Error("Reject report with different report id.")
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
	r.Id = 0

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

// Test that window_size must be set if the report type is PER_DEVICE_HISTOGRAM.
func TestValidateReportDefinitionForPerDeviceHistogram(t *testing.T) {
	r := makeValidReport()
	r.ReportType = config.ReportDefinition_PER_DEVICE_HISTOGRAM

	if err := validateReportDefinition(r); err == nil {
		t.Error("No int_buckets specified for report of type PER_DEVICE_HISTOGRAM.")
	}

	r.IntBuckets = &config.IntegerBuckets{}
	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report definition of type PER_DEVICE_HISTOGRAM without window_size field.")
	}

	// Add a valid window size to window_size, but also add an UNSET window size.
	r.WindowSize = []config.WindowSize{config.WindowSize_WINDOW_1_DAY, config.WindowSize_UNSET}
	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report definition of type PER_DEVICE_HISTOGRAM with window_size field containing an UNSET window size.")
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

// Test that int_buckets is not set if the report type is STRING_HISTOGRAMS.
func TestValidateReportDefinitionForStringHistograms(t *testing.T) {
	r := makeValidReport()
	r.ReportType = config.ReportDefinition_STRING_HISTOGRAMS

	if err := validateReportDefinition(r); err != nil {
		t.Errorf("Failed validation for valid report of type STRING_HISTOGRAMS: %v", err)
	}

	r.IntBuckets = &config.IntegerBuckets{}
	if err := validateReportDefinition(r); err == nil {
		t.Error("Accepted report definition of type STRING_HISTOGRAMS with int_buckets specified.")
	}
}

// Test that Histogram report types get properly validated for int_buckets in main validation functions.
func TestHistogramReportsHaveIntBuckets(t *testing.T) {
	// ================================================================================
	// Test PER_DEVICE_HISTOGRAM reports with int_buckets set in the report definition.
	// ================================================================================
	m := makeValidCobalt10BaseMetric(config.MetricDefinition_EVENT_COUNT)
	r := makeValidReportWithType(config.ReportDefinition_PER_DEVICE_HISTOGRAM)
	r.WindowSize = []config.WindowSize{config.WindowSize_WINDOW_1_DAY, config.WindowSize_WINDOW_7_DAYS}

	// Test that no IntBuckets defined returns an error.
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted PER_DEVICE_HISTOGRAM report with no int_bucket defined: %v", err)
	}

	// Test that an IntBuckets without a linear or exponential bucket defined returns an error.
	r.IntBuckets = &config.IntegerBuckets{}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted PER_DEVICE_HISTOGRAM report with an uninitialized int_buckets set in the report: %v", err)
	}

	// Test that an empty Linear bucket config returns an error.
	r.IntBuckets.Buckets = &config.IntegerBuckets_Linear{}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted PER_DEVICE_HISTOGRAM report with an empty linear bucket config in the report: %v", err)
	}

	// Test that a Linear bucket config with invalid num_buckets returns an error.
	linear := &config.LinearIntegerBuckets{Floor: 0, NumBuckets: 0, StepSize: 10}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Linear{Linear: linear}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted PER_DEVICE_HISTOGRAM report with an invalid linear num_buckets in the report: %v", err)
	}

	// Test that a Linear bucket config with invalid step_size returns an error.
	linear = &config.LinearIntegerBuckets{Floor: 0, NumBuckets: 10, StepSize: 0}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Linear{Linear: linear}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted PER_DEVICE_HISTOGRAM report with an invalid linear step_size in the report: %v", err)
	}

	// Test that a valid Linear bucket config passes.
	linear = &config.LinearIntegerBuckets{Floor: 0, NumBuckets: 20, StepSize: 10}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Linear{Linear: linear}
	if err := validateReportDefinitionForMetric(m, r); err != nil {
		t.Errorf("Rejected PER_DEVICE_HISTOGRAM report with a valid linear int_config in the report: %v", err)
	}

	// Test that an empty Exponential bucket config returns an error.
	r.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted PER_DEVICE_HISTOGRAM report with an empty exponential bucket config in the report: %v", err)
	}

	// Test that an Exponential bucket config with invalid num_buckets returns an error.
	exponential := &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 0, InitialStep: 10, StepMultiplier: 2}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted PER_DEVICE_HISTOGRAM report with an invalid exponential num_buckets in the report: %v", err)
	}

	// Test that an Exponential bucket config with invalid initial_step returns an error.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 10, InitialStep: 0, StepMultiplier: 2}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted PER_DEVICE_HISTOGRAM report with an invalid exponential initial_step in the report: %v", err)
	}

	// Test that an Exponential bucket config with invalid step_multiplier returns an error.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 20, InitialStep: 2, StepMultiplier: 0}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted PER_DEVICE_HISTOGRAM report with an invalid exponential step_multiplier in the report: %v", err)
	}

	// Test that a valid Exponential bucket config passes.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 30, InitialStep: 2, StepMultiplier: 2}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateReportDefinitionForMetric(m, r); err != nil {
		t.Errorf("Accepted PER_DEVICE_HISTOGRAM report with an invalid exponential step_multiplier in the report: %v", err)
	}

	// ================================================================================
	// Test INT_RANGE_HISTOGRAM reports with int_buckets set in the report definition.
	// ================================================================================

	r = makeValidReportWithType(config.ReportDefinition_INT_RANGE_HISTOGRAM)

	// Test that no IntBuckets defined returns an error.
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with no int_bucket defined: %v", err)
	}

	// Test that an IntBuckets without a linear or exponential bucket defined returns an error.
	r.IntBuckets = &config.IntegerBuckets{}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an uninitialized int_buckets set in the report: %v", err)
	}

	// Test that an empty Linear bucket config returns an error.
	r.IntBuckets.Buckets = &config.IntegerBuckets_Linear{}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an empty linear bucket config in the report: %v", err)
	}

	// Test that a Linear bucket config with invalid num_buckets returns an error.
	linear = &config.LinearIntegerBuckets{Floor: 0, NumBuckets: 0, StepSize: 10}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Linear{Linear: linear}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an invalid linear num_buckets in the report: %v", err)
	}

	// Test that a Linear bucket config with invalid step_size returns an error.
	linear = &config.LinearIntegerBuckets{Floor: 0, NumBuckets: 10, StepSize: 0}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Linear{Linear: linear}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an invalid linear step_size in the report: %v", err)
	}

	// Test that a valid Linear bucket config passes.
	linear = &config.LinearIntegerBuckets{Floor: 0, NumBuckets: 20, StepSize: 10}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Linear{Linear: linear}
	if err := validateReportDefinitionForMetric(m, r); err != nil {
		t.Errorf("Rejected INT_RANGE_HISTOGRAM report with a valid linear int_config in the report: %v", err)
	}

	// Test that an empty Exponential bucket config returns an error.
	r.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an empty exponential bucket config in the report: %v", err)
	}

	// Test that an Exponential bucket config with invalid num_buckets returns an error.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 0, InitialStep: 10, StepMultiplier: 2}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an invalid exponential num_buckets in the report: %v", err)
	}

	// Test that an Exponential bucket config with invalid initial_step returns an error.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 10, InitialStep: 0, StepMultiplier: 2}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an invalid exponential initial_step in the report: %v", err)
	}

	// Test that an Exponential bucket config with invalid step_multiplier returns an error.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 20, InitialStep: 2, StepMultiplier: 0}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an invalid exponential step_multiplier in the report: %v", err)
	}

	// Test that a valid Exponential bucket config passes.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 30, InitialStep: 2, StepMultiplier: 2}
	r.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateReportDefinitionForMetric(m, r); err != nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an invalid exponential step_multiplier in the report: %v", err)
	}

	// ================================================================================
	// Test INT_RANGE_HISTOGRAM reports with int_buckets set in the metric definition.
	// ================================================================================
	m = makeValidCobalt10BaseMetric(config.MetricDefinition_INT_HISTOGRAM)
	r = makeValidReportWithType(config.ReportDefinition_INT_RANGE_HISTOGRAM)

	// Test that an IntBuckets without a linear or exponential bucket defined returns an error.
	m.IntBuckets = &config.IntegerBuckets{}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an uninitialized int_buckets set in the metric: %v", err)
	}

	// Test that an empty Linear bucket config returns an error.
	m.IntBuckets.Buckets = &config.IntegerBuckets_Linear{}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an empty linear bucket config in the metric: %v", err)
	}

	// Test that a Linear bucket config with invalid num_buckets returns an error.
	linear = &config.LinearIntegerBuckets{Floor: 0, NumBuckets: 0, StepSize: 10}
	m.IntBuckets.Buckets = &config.IntegerBuckets_Linear{Linear: linear}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an invalid linear num_buckets in the metric: %v", err)
	}

	// Test that a Linear bucket config with invalid step_size returns an error.
	linear = &config.LinearIntegerBuckets{Floor: 0, NumBuckets: 10, StepSize: 0}
	m.IntBuckets.Buckets = &config.IntegerBuckets_Linear{Linear: linear}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an invalid linear step_size in the metric: %v", err)
	}

	// Test that a valid Linear bucket config passes.
	linear = &config.LinearIntegerBuckets{Floor: 0, NumBuckets: 20, StepSize: 10}
	m.IntBuckets.Buckets = &config.IntegerBuckets_Linear{Linear: linear}
	if err := validateReportDefinitionForMetric(m, r); err != nil {
		t.Errorf("Rejected INT_RANGE_HISTOGRAM report with a valid linear int_config in the metric: %v", err)
	}

	// Test that an empty Exponential bucket config returns an error.
	m.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an empty exponential bucket config in the metric: %v", err)
	}

	// Test that an Exponential bucket config with invalid num_buckets returns an error.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 0, InitialStep: 10, StepMultiplier: 2}
	m.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an invalid exponential num_buckets in the metric: %v", err)
	}

	// Test that an Exponential bucket config with invalid initial_step returns an error.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 10, InitialStep: 0, StepMultiplier: 2}
	m.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an invalid exponential initial_step in the metric: %v", err)
	}

	// Test that an Exponential bucket config with invalid step_multiplier returns an error.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 20, InitialStep: 2, StepMultiplier: 0}
	m.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateReportDefinitionForMetric(m, r); err == nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an invalid exponential step_multiplier in the metric: %v", err)
	}

	// Test that a valid Exponential bucket config passes.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 30, InitialStep: 2, StepMultiplier: 2}
	m.IntBuckets.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateReportDefinitionForMetric(m, r); err != nil {
		t.Errorf("Accepted INT_RANGE_HISTOGRAM report with an invalid exponential step_multiplier in the metric: %v", err)
	}
}

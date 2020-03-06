// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"testing"
)

func testForIntBucketsDefinition(metric_type config.MetricDefinition_MetricType, set_metric_int_bucket bool, report_type config.ReportDefinition_ReportType, set_report_int_bucket bool) error {
	m := makeValidMetric(metric_type)
	if set_metric_int_bucket {
		m.IntBuckets = &config.IntegerBuckets{}
	} else {
		// The makeValidMetric function will set an IntBuckets object on metrics where it is valid, so unset it.
		m.IntBuckets = nil
	}
	r := makeValidReportWithType(report_type)
	if set_report_int_bucket {
		r.IntBuckets = &config.IntegerBuckets{}
	}
	return checkHasIntBuckets(m, r)
}

// Test that specific metric and report types have an int_buckets set.
func TestHasIntBuckets(t *testing.T) {
	// Test setting int_buckets on metric types that require it.
	for _, mt := range []config.MetricDefinition_MetricType{config.MetricDefinition_INT_HISTOGRAM, config.MetricDefinition_INTEGER_HISTOGRAM} {
		// Test that histogram metrics without an int_buckets returns an error.
		if err := testForIntBucketsDefinition(mt, false, getMatchingReportType(mt), false); err == nil {
			t.Errorf("Accepted %s metric definition with no int_bucket defined: %v", mt, err)
		}

		// Test that histogram metrics with an int_bucket and a report without an int_buckets passes.
		if err := testForIntBucketsDefinition(mt, true, getMatchingReportType(mt), false); err != nil {
			t.Errorf("Rejected %s metric definition with an int_bucket defined.", mt)
		}

		// Test that histogram metrics with an int_bucket and a report that redefines int_buckets fails.
		if err := testForIntBucketsDefinition(mt, true, getMatchingReportType(mt), true); err == nil {
			t.Errorf("Accepted %s metric with int_bucket redefined in the report definition: %v", mt, err)
		}
	}

	// Test that int_buckets set on other metric types returns an error.
	for _, mt := range metricTypesExcept(config.MetricDefinition_INT_HISTOGRAM, config.MetricDefinition_INTEGER_HISTOGRAM) {
		if err := testForIntBucketsDefinition(mt, true, getMatchingReportType(mt), false); err == nil {
			t.Errorf("Accepted %s metric definition with int_buckets defined.", mt)
		}
	}

	// Test that an INT_RANGE_HISTOGRAM report has an int_buckets set.
	for _, mt := range []config.MetricDefinition_MetricType{config.MetricDefinition_ELAPSED_TIME, config.MetricDefinition_EVENT_COUNT, config.MetricDefinition_FRAME_RATE, config.MetricDefinition_MEMORY_USAGE} {
		if err := testForIntBucketsDefinition(mt, false, config.ReportDefinition_INT_RANGE_HISTOGRAM, false); err == nil {
			t.Errorf("Accepted INT_RANGE_HISTOGRAM report without int_buckets definition on metric type %s.", mt)
		}
		if err := testForIntBucketsDefinition(mt, false, config.ReportDefinition_INT_RANGE_HISTOGRAM, true); err != nil {
			t.Errorf("Rejected INT_RANGE_HISTOGRAM report with an int_buckets defined on metric type %s: %v", mt, err)
		}
	}

	// Test that a PER_DEVICE_HISTOGRAM report has an int_buckets set.
	for _, mt := range []config.MetricDefinition_MetricType{config.MetricDefinition_ELAPSED_TIME, config.MetricDefinition_EVENT_COUNT, config.MetricDefinition_FRAME_RATE, config.MetricDefinition_MEMORY_USAGE} {
		if err := testForIntBucketsDefinition(mt, false, config.ReportDefinition_PER_DEVICE_HISTOGRAM, false); err == nil {
			t.Errorf("Accepted PER_DEVICE_HISTOGRAM report without int_buckets definition on metric type %s.", mt)
		}
		if err := testForIntBucketsDefinition(mt, false, config.ReportDefinition_PER_DEVICE_HISTOGRAM, true); err != nil {
			t.Errorf("Rejected PER_DEVICE_HISTOGRAM report with an int_buckets defined on metric type %s: %v", mt, err)
		}
	}

	// Test that a UNIQUE_DEVICE_HISTOGRAMS report has an int_buckets set.
	for _, mt := range []config.MetricDefinition_MetricType{config.MetricDefinition_OCCURRENCE, config.MetricDefinition_INTEGER} {
		if err := testForIntBucketsDefinition(mt, false, config.ReportDefinition_UNIQUE_DEVICE_HISTOGRAMS, false); err == nil {
			t.Errorf("Accepted UNIQUE_DEVICE_HISTOGRAMS report without int_buckets definition on metric type %s.", mt)
		}
		if err := testForIntBucketsDefinition(mt, false, config.ReportDefinition_UNIQUE_DEVICE_HISTOGRAMS, true); err != nil {
			t.Errorf("Rejected UNIQUE_DEVICE_HISTOGRAMS report with an int_buckets defined on metric type %s: %v", mt, err)
		}
	}

	// Test that a HOURLY_VALUE_HISTOGRAMS report has an int_buckets set.
	for _, mt := range []config.MetricDefinition_MetricType{config.MetricDefinition_OCCURRENCE, config.MetricDefinition_INTEGER} {
		if err := testForIntBucketsDefinition(mt, false, config.ReportDefinition_HOURLY_VALUE_HISTOGRAMS, false); err == nil {
			t.Errorf("Accepted HOURLY_VALUE_HISTOGRAMS report without int_buckets definition on metric type %s.", mt)
		}
		if err := testForIntBucketsDefinition(mt, false, config.ReportDefinition_HOURLY_VALUE_HISTOGRAMS, true); err != nil {
			t.Errorf("Rejected HOURLY_VALUE_HISTOGRAMS report with an int_buckets defined on metric type %s: %v", mt, err)
		}
	}

	// Test that a FLEETWIDE_HISTOGRAMS report has an int_buckets set.
	if err := testForIntBucketsDefinition(config.MetricDefinition_INTEGER, false, config.ReportDefinition_FLEETWIDE_HISTOGRAMS, false); err == nil {
		t.Errorf("Accepted FLEETWIDE_HISTOGRAMS report without int_buckets definition on metric type INTEGER.")
	}
	if err := testForIntBucketsDefinition(config.MetricDefinition_INTEGER, false, config.ReportDefinition_FLEETWIDE_HISTOGRAMS, true); err != nil {
		t.Errorf("Rejected FLEETWIDE_HISTOGRAMS report with an int_buckets defined on metric type INTEGER: %v", err)
	}
}

// Tests int_buckets validation.
func TestValidateIntBuckets(t *testing.T) {
	// Test that an IntBuckets without a linear or exponential bucket defined returns an error.
	bucket := config.IntegerBuckets{}
	if err := validateIntBuckets(bucket); err == nil {
		t.Errorf("Uninitialized int_buckets should return an error.")
	}

	// Test that an empty Linear bucket config returns an error.
	bucket.Buckets = &config.IntegerBuckets_Linear{}
	if err := validateIntBuckets(bucket); err == nil {
		t.Errorf("int_buckets with an empty linear_bucket should return an error.")
	}

	// Test that a Linear bucket config with invalid num_buckets returns an error.
	linear := &config.LinearIntegerBuckets{Floor: 0, NumBuckets: 0, StepSize: 10}
	bucket.Buckets = &config.IntegerBuckets_Linear{Linear: linear}
	if err := validateIntBuckets(bucket); err == nil {
		t.Errorf("A linear_bucket with 0 num_buckets should return an error.")
	}

	// Test that a Linear bucket config with invalid step_size returns an error.
	linear = &config.LinearIntegerBuckets{Floor: 0, NumBuckets: 10, StepSize: 0}
	bucket.Buckets = &config.IntegerBuckets_Linear{Linear: linear}
	if err := validateIntBuckets(bucket); err == nil {
		t.Errorf("A linear_bucket with 0 step_size should return an error.")
	}

	// Test that a valid Linear bucket config passes.
	linear = &config.LinearIntegerBuckets{Floor: 0, NumBuckets: 20, StepSize: 10}
	bucket.Buckets = &config.IntegerBuckets_Linear{Linear: linear}
	if err := validateIntBuckets(bucket); err != nil {
		t.Errorf("Rejected a valid linear int_config: %v", err)
	}

	// Test that an empty Exponential bucket config returns an error.
	bucket.Buckets = &config.IntegerBuckets_Exponential{}
	if err := validateIntBuckets(bucket); err == nil {
		t.Errorf("int_buckets with an empty exponential_bucket should return an error.")
	}

	// Test that an Exponential bucket config with invalid num_buckets returns an error.
	exponential := &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 0, InitialStep: 10, StepMultiplier: 2}
	bucket.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateIntBuckets(bucket); err == nil {
		t.Errorf("An exponential_bucket with 0 num_buckets should return an error.")
	}

	// Test that an Exponential bucket config with invalid initial_step returns an error.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 10, InitialStep: 0, StepMultiplier: 2}
	bucket.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateIntBuckets(bucket); err == nil {
		t.Errorf("An exponential_bucket with 0 initial_step should return an error.")
	}

	// Test that an Exponential bucket config with invalid step_multiplier returns an error.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 20, InitialStep: 2, StepMultiplier: 0}
	bucket.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateIntBuckets(bucket); err == nil {
		t.Errorf("An exponential_bucket with 0 step_multipier should return an error.")
	}

	// Test that a valid Exponential bucket config passes.
	exponential = &config.ExponentialIntegerBuckets{Floor: 0, NumBuckets: 30, InitialStep: 2, StepMultiplier: 2}
	bucket.Buckets = &config.IntegerBuckets_Exponential{Exponential: exponential}
	if err := validateIntBuckets(bucket); err != nil {
		t.Errorf("Rejected a valid exponential int_config: %v", err)
	}
}

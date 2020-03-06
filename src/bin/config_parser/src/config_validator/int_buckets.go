// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"fmt"
)

// This file contains logic to validate integer_buckets in protos.

// Check that int_buckets is set for the correct metrics and reports.
func checkHasIntBuckets(m config.MetricDefinition, r config.ReportDefinition) error {
	if m.MetricType == config.MetricDefinition_INT_HISTOGRAM || m.MetricType == config.MetricDefinition_INTEGER_HISTOGRAM {
		// IntBuckets should be defined in the metric.
		if m.IntBuckets == nil {
			return fmt.Errorf("%s metric types must define an int_buckets field.", m.MetricType)
		}

		// IntBuckets should not be redefined in a report.
		if r.IntBuckets != nil {
			return fmt.Errorf("Reports for a %s metric must not redefine int_buckets.", m.MetricType)
		}
	} else {
		// Other metric types should not have int_buckets set.
		if m.IntBuckets != nil {
			return fmt.Errorf("int_buckets should not be specified for metrics of type %s.", m.MetricType)
		}

		// Check that specific report types contain int_buckets.
		switch r.ReportType {
		case config.ReportDefinition_PER_DEVICE_HISTOGRAM:
			fallthrough
		case config.ReportDefinition_INT_RANGE_HISTOGRAM:
			fallthrough
		case config.ReportDefinition_UNIQUE_DEVICE_HISTOGRAMS:
			fallthrough
		case config.ReportDefinition_HOURLY_VALUE_HISTOGRAMS:
			fallthrough
		case config.ReportDefinition_FLEETWIDE_HISTOGRAMS:
			if r.IntBuckets == nil {
				return fmt.Errorf("No int_buckets specified for report of type %s.", r.ReportType)
			}
		}
	}
	return nil
}

func validateIntBucketsForReport(m config.MetricDefinition, r config.ReportDefinition) error {
	if err := checkHasIntBuckets(m, r); err != nil {
		return err
	}

	if r.IntBuckets != nil {
		// If it exists, the report bucket config takes priority.
		return validateIntBuckets(*r.IntBuckets)
	} else if m.IntBuckets != nil {
		return validateIntBuckets(*m.IntBuckets)
	}

	return nil
}

// Validate the fields in an int_buckets.
func validateIntBuckets(bucket config.IntegerBuckets) error {
	if bucket_value := bucket.GetLinear(); bucket_value != nil {
		if bucket_value.NumBuckets < 1 {
			return fmt.Errorf("Linear bucket must set a num_buckets greater than or equal to 1.")
		}
		if bucket_value.StepSize < 1 {
			return fmt.Errorf("Linear bucket must set a step_size greater than or equal to 1.")
		}
	} else if bucket_value := bucket.GetExponential(); bucket_value != nil {
		if bucket_value.NumBuckets < 1 {
			return fmt.Errorf("Exponential bucket must set a num_buckets greater than or equal to 1.")
		}
		if bucket_value.InitialStep < 1 {
			return fmt.Errorf("Exponential bucket must set an InitialStep greater than or equal to 1.")
		}
		if bucket_value.StepMultiplier < 1 {
			return fmt.Errorf("Exponential bucket must set a step_multiplier greater than or equal to 1.")
		}
	} else {
		return fmt.Errorf("int_config must define either a linear or exponential bucket definition.")
	}

	return nil
}

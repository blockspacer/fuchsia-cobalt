// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"testing"
)

func TestValidateMetricNoNilMetricPart(t *testing.T) {
	config := &config.ProjectConfigFile{
		MetricConfigs: []*config.Metric{
			&config.Metric{
				Parts: map[string]*config.MetricPart{"int_part": nil},
			},
		},
	}

	if err := validateConfiguredMetrics(config); err == nil {
		t.Error("Metric with nil metric part was accepted.")
	}
}

// Test that metric parts with a name that starts with an underscore are rejected.
func TestValidateMetricInvalidMetricPartName(t *testing.T) {
	config := &config.ProjectConfigFile{
		MetricConfigs: []*config.Metric{
			&config.Metric{
				Parts: map[string]*config.MetricPart{"_int_part": &config.MetricPart{}},
			},
		},
	}

	if err := validateConfiguredMetrics(config); err == nil {
		t.Error("Metric with invalid name for metric part was accepted.")
	}
}

// Tests that we catch encodings with id = 0.
func TestValidateNoZeroMetricIds(t *testing.T) {
	config := &config.ProjectConfigFile{
		MetricConfigs: []*config.Metric{
			makeMetric(0, nil),
		},
	}

	if err := validateConfiguredMetrics(config); err == nil {
		t.Error("Accepted metric config with id of 0.")
	}
}

// Tests that we catch non-unique metric ids.
func TestValidateUniqueMetricIds(t *testing.T) {
	config := &config.ProjectConfigFile{
		MetricConfigs: []*config.Metric{
			makeMetric(1, nil), makeMetric(1, nil),
		},
	}

	if err := validateConfiguredMetrics(config); err == nil {
		t.Error("Accepted non-unique metric id.")
	}
}

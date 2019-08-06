// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"fmt"
)

// containsSystemProfileField checks to make sure that a metric has a particular
// system_profile_field set.
func containsSystemProfileField(metric *config.Metric, e config.SystemProfileField) bool {
	for _, a := range metric.SystemProfileField {
		if a == e {
			return true
		}
	}
	return false
}

// validateSystemProfileFields makes sure that all system_profile_fields used in
// reports are present in their associated metrics.
func validateSystemProfileFields(config *config.ProjectConfigFile) error {
	metrics := map[uint32]uint32{}

	for i, metric := range config.MetricConfigs {
		key := metric.Id
		metrics[key] = uint32(i)
	}

	for _, report := range config.ReportConfigs {
		metric := config.MetricConfigs[metrics[report.MetricId]]
		for _, field := range report.SystemProfileField {
			if !containsSystemProfileField(metric, field) {
				return fmt.Errorf("report %d uses SystemProfileField: %v, but metric %d does not supply it", report.Id, field, metric.Id)
			}
		}
	}
	return nil
}

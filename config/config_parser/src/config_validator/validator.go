// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"config_parser"
	"fmt"
)

func ValidateProjectConfig(c *config_parser.ProjectConfig) (err error) {
	if c.CobaltVersion == config_parser.CobaltVersion0 {
		if err = validateConfigV0(&c.ProjectConfig); err != nil {
			return fmt.Errorf("Error in configuration for project %s: %v", c.ProjectName, err)
		}
	} else {
		if err = validateConfigV1(&c.ProjectConfig); err != nil {
			return fmt.Errorf("Error in configuration for project %s: %v", c.ProjectName, err)
		}
	}
	return nil
}

// Validate a project config for Cobalt 0.1.
func validateConfigV0(config *config.ProjectConfigFile) (err error) {
	if len(config.MetricDefinitions) > 0 {
		return fmt.Errorf("Version 0 projects cannot contain metric_definition entries.")
	}

	if err = validateConfiguredEncodings(config); err != nil {
		return err
	}

	if err = validateConfiguredMetrics(config); err != nil {
		return err
	}

	if err = validateConfiguredReports(config); err != nil {
		return err
	}

	if err = validateSystemProfileFields(config); err != nil {
		return err
	}

	return nil
}

// Validate a project config for Cobalt 1.0.
func validateConfigV1(config *config.ProjectConfigFile) (err error) {
	if len(config.EncodingConfigs) > 0 || len(config.MetricConfigs) > 0 || len(config.ReportConfigs) > 0 {
		return fmt.Errorf("Version 1 projects cannot contain encoding_config, metric_config or report_config entries.")
	}

	if err = validateConfiguredMetricDefinitions(config.MetricDefinitions); err != nil {
		return err
	}
	return nil
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config_parser"
	"fmt"
)

func ValidateProjectConfigs(configs []config_parser.ProjectConfig) error {
	for _, c := range configs {
		if err := validateProjectConfig(&c); err != nil {
			return err
		}
	}

	return nil
}

func validateProjectConfig(c *config_parser.ProjectConfig) (err error) {
	if c.CobaltVersion == config_parser.CobaltVersion0 {
		return fmt.Errorf("Cobalt version 0.1 configs are no longer supported")
	}

	if err = validateConfiguredMetricDefinitions(c.ProjectConfig.MetricDefinitions); err != nil {
		return fmt.Errorf("Error in configuration for project %s: %v", c.ProjectName, err)
	}
	return nil
}

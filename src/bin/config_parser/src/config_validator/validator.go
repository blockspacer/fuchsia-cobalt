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
		return fmt.Errorf("Cobalt version 0.1 configs are no longer supported")
	} else {
		if err = validateConfigV1(&c.ProjectConfig); err != nil {
			return fmt.Errorf("Error in configuration for project %s: %v", c.ProjectName, err)
		}
	}
	return nil
}

// Validate a project config for Cobalt 1.0.
func validateConfigV1(config *config.ProjectConfigFile) (err error) {
	if err = validateConfiguredMetricDefinitions(config.MetricDefinitions); err != nil {
		return err
	}
	return nil
}

// Copyright 2017 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This file contains the representation for the configuration of a cobalt
// project (See projectConfig) and a way to parse that configuration information
// from a yaml string.

package config_parser

import (
	"config"
	"fmt"
	"yamlpb"
)

type CobaltVersion int

const (
	// Cobalt version 0.1
	CobaltVersion0 = iota

	// Cobalt version 1.0
	CobaltVersion1
)

// Represents the configuration of a single project.
type ProjectConfig struct {
	CustomerName  string
	CustomerId    uint32
	ProjectName   string
	ProjectId     uint32
	Contact       string
	CobaltVersion CobaltVersion
	ProjectConfig *config.ProjectConfigFile
}

// Parse the configuration for one project from the yaml string provided into
// the config field in ProjectConfig.
func parseProjectConfig(y string, c *ProjectConfig) (err error) {
	c.ProjectConfig = &config.ProjectConfigFile{}
	if err := yamlpb.UnmarshalString(y, c.ProjectConfig); err != nil {
		return fmt.Errorf("Error while parsing yaml: %v", err)
	}

	for _, e := range c.ProjectConfig.MetricDefinitions {
		e.CustomerId = c.CustomerId
		e.ProjectId = c.ProjectId
		e.CustomerName = c.CustomerName
		e.ProjectName = c.ProjectName
		if e.Id == 0 {
			return fmt.Errorf("Metric named '%v' does not have a metric id specified.", e.MetricName)
		}

		for _, r := range e.Reports {
			if r.Id == 0 {
				return fmt.Errorf("Report named '%v' does not have a report id specified.", r.ReportName)
			}
		}
	}

	return nil
}

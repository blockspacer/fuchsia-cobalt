// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"fmt"
	"github.com/golang/glog"
)

func validateConfiguredReports(config *config.ProjectConfigFile) (err error) {
	// Mapping of metric ids to their order in the MetricConfigs slice.
	metrics := map[uint32]uint32{}

	// Set of report ids. Used to detect duplicates.
	reportIds := map[uint32]bool{}

	for i, metric := range config.MetricConfigs {
		metrics[metric.Id] = uint32(i)
	}

	for i, report := range config.ReportConfigs {
		if report.Id == 0 {
			return fmt.Errorf("Error validating report %v: Report id '0' is invalid.", report.Name)
		}

		reportKey := report.Id
		if reportIds[reportKey] {
			return fmt.Errorf("Report id %v is repeated in report config entry number %v. Report ids must be unique.", reportKey, i+1)
		}
		reportIds[reportKey] = true

		metricKey := report.MetricId
		if _, ok := metrics[metricKey]; !ok {
			return fmt.Errorf("Error validating report %v (%v): There is no metric id %v.", report.Name, report.Id, metricKey)
		}

		metric := config.MetricConfigs[metrics[metricKey]]
		if err := validateReportVariables(report, metric); err != nil {
			return fmt.Errorf("Error validating report %v (%v): %v", report.Name, report.Id, err)
		}

		for exportConfigIdx, exportConfig := range report.ExportConfigs {
			if exportConfig.ExportSerialization == nil {
				return fmt.Errorf("Error validating report %v (%v): element %v of export_configs has no export serialization set.", report.Name, report.Id, exportConfigIdx)
			}

			if exportConfig.ExportLocation == nil {
				return fmt.Errorf("Error validating report %v (%v): element %v of export_configs has no export location set.", report.Name, report.Id, exportConfigIdx)
			}
		}
	}

	return nil
}

// Checks that the report variables are compatible with the specific metric.
func validateReportVariables(c *config.ReportConfig, m *config.Metric) (err error) {
	if len(c.Variable) == 0 {
		glog.Warningf("Report '%v' (Customer %v, Project %v Id %v) does not have any report variables.", c.Name, c.CustomerId, c.ProjectId, c.Id)
		return nil
	}

	for i, v := range c.Variable {
		if v == nil {
			return fmt.Errorf("Report Variable in position %v is null. This is not allowed.", i)
		}

		// Check that the metric part being referenced can be found.
		p, ok := m.Parts[v.MetricPart]
		if !ok {
			return fmt.Errorf("Metric part '%v' is not present in metric '%v'.", v.MetricPart, m.Name)
		}

		// Checks that if index labels are found, the metric part referred to is an index.
		if v.IndexLabels != nil && len(v.IndexLabels.Labels) > 0 && p.DataType != config.MetricPart_INDEX {
			return fmt.Errorf("Report variable %v has index labels specified "+
				"which implies referring to an index metric part. But metric part '%v'"+
				"of metric '%v' (%v) is of type %v.",
				i, v.MetricPart, m.Name, m.Id, config.MetricPart_DataType_name[int32(p.DataType)])
		}

		// Checks that if RAPPOR candidates are found, the metric part referred to is a string.
		if v.RapporCandidates != nil && len(v.RapporCandidates.Candidates) > 0 && p.DataType != config.MetricPart_STRING {
			return fmt.Errorf("Report variable %v has RAPPOR candidates specified "+
				"which implies referring to a string metric part. But metric part '%v'"+
				"of metric '%v' (%v) is of type %v.",
				i, v.MetricPart, m.Name, m.Id, config.MetricPart_DataType_name[int32(p.DataType)])
		}
	}

	return nil
}

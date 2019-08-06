// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"testing"
)

// Test that a report config with an unknown metric id gets rejected.
func TestProjectConfigUnknownMetricIdInReportConfig(t *testing.T) {
	config := &config.ProjectConfigFile{
		ReportConfigs: []*config.ReportConfig{
			makeReport(1, 10, nil),
		},
	}

	if err := validateConfiguredReports(config); err == nil {
		t.Error("Accepted report config with unknown metric id.")
	}
}

// Check that valid report variables are accepted.
func TestValidateReportVariables(t *testing.T) {
	config := &config.ProjectConfigFile{
		MetricConfigs: []*config.Metric{
			&config.Metric{
				Parts: map[string]*config.MetricPart{
					"int_part":    &config.MetricPart{DataType: config.MetricPart_INT},
					"string_part": &config.MetricPart{DataType: config.MetricPart_STRING},
					"blob_part":   &config.MetricPart{DataType: config.MetricPart_BLOB},
					"index_part":  &config.MetricPart{DataType: config.MetricPart_INDEX},
				},
			},
		},
		ReportConfigs: []*config.ReportConfig{
			&config.ReportConfig{
				Id: 1,
				Variable: []*config.ReportVariable{
					&config.ReportVariable{
						MetricPart: "int_part",
					},
					&config.ReportVariable{
						MetricPart:  "index_part",
						IndexLabels: &config.IndexLabels{Labels: map[uint32]string{0: "zero"}},
					},
					&config.ReportVariable{
						MetricPart:       "string_part",
						RapporCandidates: &config.RapporCandidateList{Candidates: []string{"hello"}},
					},
				},
			},
		},
	}

	if err := validateConfiguredReports(config); err != nil {
		t.Error(err)
	}
}

// Test that a report variable referring to an unknown metric part will be rejected.
func TestValidateReportVariablesUnknownMetricPart(t *testing.T) {
	config := &config.ProjectConfigFile{
		MetricConfigs: []*config.Metric{},
		ReportConfigs: []*config.ReportConfig{
			&config.ReportConfig{
				Variable: []*config.ReportVariable{
					&config.ReportVariable{
						MetricPart: "int_part",
					},
				},
			},
		},
	}

	if err := validateConfiguredReports(config); err == nil {
		t.Error("Report with unknown metric part was accepted.")
	}
}

// Test that if a report variable specifies index labels, the metric part it
// refers to must be of type index.
func TestValidateReportVariablesIndexLablesNonIndexMetric(t *testing.T) {
	config := &config.ProjectConfigFile{
		MetricConfigs: []*config.Metric{
			&config.Metric{
				Parts: map[string]*config.MetricPart{
					"int_part": &config.MetricPart{DataType: config.MetricPart_INT},
				},
			},
		},
		ReportConfigs: []*config.ReportConfig{
			&config.ReportConfig{
				Variable: []*config.ReportVariable{
					&config.ReportVariable{
						MetricPart:  "int_part",
						IndexLabels: &config.IndexLabels{Labels: map[uint32]string{0: "zero"}},
					},
				},
			},
		},
	}

	if err := validateConfiguredReports(config); err == nil {
		t.Error("Report with with index labels specified for a non-index metric part accepted.")
	}
}

// Test that if a report variable specifies rappor candidates, the metric part
// it refers to must be of type string.
func TestValidateReportVarialesRapporCandidatesNonStringMetric(t *testing.T) {
	config := &config.ProjectConfigFile{
		MetricConfigs: []*config.Metric{
			&config.Metric{
				Parts: map[string]*config.MetricPart{
					"int_part": &config.MetricPart{DataType: config.MetricPart_INT},
				},
			},
		},
		ReportConfigs: []*config.ReportConfig{
			&config.ReportConfig{
				Variable: []*config.ReportVariable{
					&config.ReportVariable{
						MetricPart:       "int_part",
						RapporCandidates: &config.RapporCandidateList{Candidates: []string{"alpha"}},
					},
				},
			},
		},
	}

	if err := validateConfiguredReports(config); err == nil {
		t.Error("Report with with rappor candidates specified for a non-string metric part accepted.")
	}
}

// Tests that we catch reports with id = 0.
func TestValidateNoZeroReportIds(t *testing.T) {
	config := &config.ProjectConfigFile{
		ReportConfigs: []*config.ReportConfig{
			makeReport(0, 1, nil),
		},
	}

	if err := validateConfiguredReports(config); err == nil {
		t.Error("Accepted report config with id of 0.")
	}
}

// Tests that we catch non-unique report ids.
func TestValidateUniqueReportIds(t *testing.T) {
	config := &config.ProjectConfigFile{
		ReportConfigs: []*config.ReportConfig{
			makeReport(1, 1, nil), makeReport(1, 1, nil),
		},
	}
	if err := validateConfiguredReports(config); err == nil {
		t.Error("Accepted non-unique report id.")
	}
}

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

package config_parser

import (
	"config"
	"testing"

	proto "github.com/golang/protobuf/proto"
)

// Basic test for parseProjectConfig with a 1.0 project.
func TestParseProjectConfig(t *testing.T) {
	y := `
metric_definitions:
- metric_name: the_metric_name
  id: 1
  time_zone_policy: UTC
  reports:
  - report_name: the_report
    report_type: CUSTOM_RAW_DUMP
  - report_name: the_other_report
    report_type: STRING_COUNTS_WITH_THRESHOLD
- metric_name: the_other_metric_name
  id: 2
  time_zone_policy: LOCAL
  reports:
  - report_name: the_report
    report_type: NUMERIC_PERF_RAW_DUMP
`
	c := ProjectConfig{
		CustomerId:    1,
		ProjectId:     10,
		CustomerName:  "customer_name",
		ProjectName:   "project_name",
		CobaltVersion: CobaltVersion1,
	}

	if err := parseProjectConfig(y, &c); err != nil {
		t.Error(err)
	}

	e := config.ProjectConfigFile{
		MetricDefinitions: []*config.MetricDefinition{
			&config.MetricDefinition{
				CustomerId:     1,
				ProjectId:      10,
				CustomerName:   "customer_name",
				ProjectName:    "project_name",
				MetricName:     "the_metric_name",
				Id:             1,
				TimeZonePolicy: config.MetricDefinition_UTC,
				Reports: []*config.ReportDefinition{
					&config.ReportDefinition{
						ReportName: "the_report",
						Id:         IdFromName("the_report"),
						ReportType: config.ReportDefinition_CUSTOM_RAW_DUMP,
					},
					&config.ReportDefinition{
						ReportName: "the_other_report",
						Id:         IdFromName("the_other_report"),
						ReportType: config.ReportDefinition_STRING_COUNTS_WITH_THRESHOLD,
					},
				},
			},
			&config.MetricDefinition{
				CustomerId:     1,
				ProjectId:      10,
				CustomerName:   "customer_name",
				ProjectName:    "project_name",
				MetricName:     "the_other_metric_name",
				Id:             2,
				TimeZonePolicy: config.MetricDefinition_LOCAL,
				Reports: []*config.ReportDefinition{
					&config.ReportDefinition{
						ReportName: "the_report",
						Id:         IdFromName("the_report"),
						ReportType: config.ReportDefinition_NUMERIC_PERF_RAW_DUMP,
					},
				},
			},
		},
	}

	if !proto.Equal(&e, &c.ProjectConfig) {
		t.Errorf("%v\n!=\n%v", proto.MarshalTextString(&e), proto.MarshalTextString(&c.ProjectConfig))
	}

}

// Tests that we catch non-unique encoding ids.
func TestParseProjectConfigUniqueEncodingIds(t *testing.T) {
	y := `
metric_configs:
- id: 1
  name: metric_name
  time_zone_policy: UTC
- id: 2
  name: other_metric_name
  time_zone_policy: LOCAL
encoding_configs:
- id: 1
  basic_rappor:
    prob_0_becomes_1: 0.5
    prob_1_stays_1: 0.5
- id: 1
report_configs:
- id: 1
  metric_id: 5
- id: 2
`

	c := ProjectConfig{
		CustomerId: 1,
		ProjectId:  10,
	}

	if err := parseProjectConfig(y, &c); err == nil {
		t.Error("Accepted non-unique encoding id.")
	}
}

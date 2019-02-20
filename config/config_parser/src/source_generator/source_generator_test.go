// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Compares the generated source files to a list of golden files.
// If the test fails due to a change in the config protos, you can find the
// new golden files in your /tmp directory.

package source_generator

import (
	"config"
	"config_parser"
	"fmt"
	"io/ioutil"
	"path"
	"runtime"
	"strings"
	"testing"

	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"
)

type memConfigReader struct {
	customers string
	projects  map[string]string
}

func (r memConfigReader) Customers() (string, error) {
	return r.customers, nil
}

func (r memConfigReader) Project(customerName string, projectName string) (string, error) {
	key := customerName + "|" + projectName
	yaml, ok := r.projects[key]
	if !ok {
		return yaml, fmt.Errorf("Project could not be read!")
	}
	return yaml, nil
}

func (r *memConfigReader) SetProject(customerName string, projectName string, yaml string) {
	if r.projects == nil {
		r.projects = map[string]string{}
	}
	key := customerName + "|" + projectName
	r.projects[key] = yaml
}

const v0ProjectConfigYaml = `
metric_configs:
- id: 1
  name: "Daily rare event counts"
  description: "Daily counts of several events that are expected to occur rarely if ever."
  time_zone_policy: UTC
  parts:
    "Event name":
      description: "Which rare event occurred?"
- id: 2
  name: "Module views"
  description: "Tracks each incidence of viewing a module by its URL."
  time_zone_policy: UTC
  parts:
    "url":
      description: "The URL of the module being launched."

encoding_configs:
- id: 1
  basic_rappor:
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    string_categories:
      category:
      - "Ledger-startup"
      - "Commits-received-out-of-order"
      - "Commits-merged"
      - "Merged-commits-merged"
- id: 2
  forculus:
    threshold: 2
    epoch_type: MONTH

report_configs:
- id: 1
  name: "Fuchsia Ledger Daily Rare Events"
  description: "A daily report of events that are expected to happen rarely."
  metric_id: 1
  variable:
  - metric_part: "Event name"
  scheduling:
    report_finalization_days: 3
    aggregation_epoch_type: DAY
  export_configs:
  - csv: {}
    gcs:
      bucket: "fuchsia-cobalt-reports-p2-test-app"

- id: 2
  name: "Fuchsia Module Daily Launch Counts"
  description: "A daily report of the daily counts of module launches by URL."
  metric_id: 2
  variable:
  - metric_part: "url"
  scheduling:
    report_finalization_days: 3
    aggregation_epoch_type: DAY
  export_configs:
  - csv: {}
    gcs:
      bucket: "fuchsia-cobalt-reports-p2-test-app"
`

const v1ProjectConfigYaml = `
metric_definitions:
- metric_name: the_metric_name
  id: 100
  time_zone_policy: UTC
  reports:
  - report_name: the_report
    report_type: CUSTOM_RAW_DUMP
  - report_name: the_other_report
    report_type: STRING_COUNTS_WITH_THRESHOLD
- metric_name: the_other_metric_name
  id: 200
  time_zone_policy: LOCAL
  metric_type: EVENT_OCCURRED
  event_codes:
    0: AnEvent
    1: AnotherEvent
    2: A third event
  max_event_code: 200
  reports:
  - report_name: the_report
    report_type: NUMERIC_PERF_RAW_DUMP
- id: 300
  metric_name: "event groups"
  time_zone_policy: LOCAL
  metric_type: EVENT_OCCURRED
  metric_dimensions:
    - dimension: "The First Group"
      event_codes:
        0: AnEvent
        1: AnotherEvent
        2: A third event
      max_event_code: 2
    - dimension: "A second group"
      event_codes:
        1: This
        2: Is
        3: another
        4: Test
    - event_codes:
        0: ThisMetric
        2: HasNo
        4: Name
  reports:
  - report_name: the_report
    report_type: NUMERIC_PERF_RAW_DUMP
`

func readGoldenFile(filename string) (string, error) {
	_, thisFname, _, _ := runtime.Caller(0)
	goldenFname := path.Join(path.Dir(thisFname), "source_generator_test_files", filename)
	contents, err := ioutil.ReadFile(goldenFname)
	if err != nil {
		return "", err
	}
	return string(contents), nil
}

func getConfigFrom(config string, cobalt_version config_parser.CobaltVersion) config.CobaltRegistry {
	r := memConfigReader{}
	r.SetProject("customer", "project", config)
	con := config_parser.ProjectConfig{
		CustomerName:  "customer",
		CustomerId:    10,
		ProjectName:   "project",
		ProjectId:     5,
		CobaltVersion: cobalt_version,
	}

	_ = config_parser.ReadProjectConfig(r, &con)
	return config_parser.MergeConfigs([]config_parser.ProjectConfig{con})
}

var cfgTests = []struct {
	yaml           string
	goldenFile     string
	cobalt_version config_parser.CobaltVersion
	formatter      OutputFormatter
	hideOnClient   bool
}{
	{v0ProjectConfigYaml, "golden_v0.cb.dart", config_parser.CobaltVersion0, DartOutputFactory("config", false), false},
	{v0ProjectConfigYaml, "golden_v0.cb.h", config_parser.CobaltVersion0, CppOutputFactory("config", []string{"a", "b"}, false), false},
	{v0ProjectConfigYaml, "golden_v0.cb.rs", config_parser.CobaltVersion0, RustOutputFactory("config", []string{"a", "b"}, false), false},

	{v0ProjectConfigYaml, "golden_v0_filtered.cb.dart", config_parser.CobaltVersion0, DartOutputFactory("config", false), true},
	{v0ProjectConfigYaml, "golden_v0_filtered.cb.h", config_parser.CobaltVersion0, CppOutputFactory("config", []string{"a", "b"}, false), true},
	{v0ProjectConfigYaml, "golden_v0_filtered.cb.rs", config_parser.CobaltVersion0, RustOutputFactory("config", []string{"a", "b"}, false), true},

	{v1ProjectConfigYaml, "golden_v1.cb.dart", config_parser.CobaltVersion1, DartOutputFactory("config", false), false},
	{v1ProjectConfigYaml, "golden_v1.cb.h", config_parser.CobaltVersion1, CppOutputFactory("config", []string{}, false), false},
	{v1ProjectConfigYaml, "golden_v1.cb.rs", config_parser.CobaltVersion1, RustOutputFactory("config", []string{}, false), false},

	{v1ProjectConfigYaml, "golden_v1_filtered.cb.dart", config_parser.CobaltVersion1, DartOutputFactory("config", false), true},
	{v1ProjectConfigYaml, "golden_v1_filtered.cb.h", config_parser.CobaltVersion1, CppOutputFactory("config", []string{}, false), true},
	{v1ProjectConfigYaml, "golden_v1_filtered.cb.rs", config_parser.CobaltVersion1, RustOutputFactory("config", []string{}, false), true},

	{v1ProjectConfigYaml, "golden_v1_for_testing.cb.dart", config_parser.CobaltVersion1, DartOutputFactory("config", true), false},
	{v1ProjectConfigYaml, "golden_v1_for_testing.cb.h", config_parser.CobaltVersion1, CppOutputFactory("config", []string{}, true), false},
	{v1ProjectConfigYaml, "golden_v1_for_testing.cb.rs", config_parser.CobaltVersion1, RustOutputFactory("config", []string{}, true), false},
}

func TestPrintConfig(t *testing.T) {
	for _, tt := range cfgTests {
		c := getConfigFrom(tt.yaml, tt.cobalt_version)
		filtered := proto.Clone(&c).(*config.CobaltRegistry)
		if tt.hideOnClient {
			config_parser.FilterHideOnClient(filtered)
		}

		configBytes, err := tt.formatter(&c, filtered)
		if err != nil {
			t.Errorf("Error generating file: %v", err)
		}
		goldenFile, err := readGoldenFile(tt.goldenFile)
		if err != nil {
			t.Errorf("Error reading golden file: %v", err)
		}
		generatedConfig := string(configBytes)
		goldenLines := strings.Split(goldenFile, "\n")
		generatedLines := strings.Split(generatedConfig, "\n")
		if diff := cmp.Diff(goldenLines, generatedLines); diff != "" {
			genFile := "/tmp/" + tt.goldenFile
			ioutil.WriteFile(genFile, configBytes, 0644)
			t.Errorf("Golden file %s doesn't match the generated config (%s). Diff: %s", tt.goldenFile, genFile, diff)
		}
	}
}

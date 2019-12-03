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

func (r memConfigReader) CustomersFilePath() string {
	return "fake/customers.yaml"
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

const projectConfigYaml = `
metric_definitions:
- metric_name: the_metric_name
  id: 100
  time_zone_policy: UTC
  reports:
  - report_name: the_report
    id: 10
    report_type: CUSTOM_RAW_DUMP
  - report_name: the_other_report
    id: 20
    report_type: NUMERIC_AGGREGATION
- metric_name: the_other_metric_name
  id: 200
  time_zone_policy: LOCAL
  metric_type: EVENT_OCCURRED
  metric_dimensions:
    - event_codes:
        0: AnEvent
        1: AnotherEvent
        2: A third event
      max_event_code: 200
  reports:
  - report_name: the_report
    id: 10
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
      event_code_aliases:
        HasNo: Alias
  reports:
  - report_name: the_report
    id: 30
    report_type: NUMERIC_PERF_RAW_DUMP
- id: 400
  metric_name: "linear buckets"
  int_buckets:
    linear:
      floor: 0
      num_buckets: 140
      step_size: 5
- id: 500
  metric_name: "exponential buckets"
  reports:
  - report_name: report
    id: 40
    int_buckets:
      exponential:
        floor: 0
        num_buckets: 3
        initial_step: 2
        step_multiplier: 2
- id: 600
  metric_name: "metric"
  metric_dimensions: &dimensions
    - dimension: "First"
      event_codes:
        1: A
        2: Set
        3: OfEvent
        4: Codes
    - dimension: "Second"
      event_codes:
        0: Some
        4: More
        8: Event
        16: Codes
- id: 601
  metric_name: "second metric"
  metric_dimensions: *dimensions
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
	r.SetProject("the_customer", "the_project", config)
	con := config_parser.ProjectConfig{
		CustomerName:  "the_customer",
		CustomerId:    10,
		ProjectName:   "the_project",
		ProjectId:     5,
		CobaltVersion: cobalt_version,
	}

	_ = config_parser.ReadProjectConfig(r, &con)
	return config_parser.MergeConfigs([]config_parser.ProjectConfig{con})
}

var cfgTests = []struct {
	goldenFile   string
	formatter    OutputFormatter
	hideOnClient bool
}{
	{"golden.cb.dart", DartOutputFactory("config", false), false},
	{"golden.cb.h", CppOutputFactory("config", []string{}, false), false},
	{"golden.cb.rs", RustOutputFactory("config", []string{}, false), false},
	{"golden.cb.go", GoOutputFactory("config", "package", false), false},

	{"golden_with_ns.cb.h", CppOutputFactory("config", []string{"ns1", "ns2"}, false), false},
	{"golden_with_ns.cb.rs", RustOutputFactory("config", []string{"ns1", "ns2"}, false), false},

	{"golden_filtered.cb.dart", DartOutputFactory("config", false), true},
	{"golden_filtered.cb.h", CppOutputFactory("config", []string{}, false), true},
	{"golden_filtered.cb.rs", RustOutputFactory("config", []string{}, false), true},
	{"golden_filtered.cb.go", GoOutputFactory("config", "package", false), true},

	{"golden_for_testing.cb.dart", DartOutputFactory("config", true), false},
	{"golden_for_testing.cb.h", CppOutputFactory("config", []string{}, true), false},
	{"golden_for_testing.cb.rs", RustOutputFactory("config", []string{}, true), false},
	{"golden_for_testing.cb.go", GoOutputFactory("config", "package", true), false},

	// JSONOutput() ignores hideOnClients so we only test once
	{"golden.cb.json", JSONOutput, true},
}

func TestPrintConfig(t *testing.T) {
	for _, tt := range cfgTests {
		c := getConfigFrom(projectConfigYaml, config_parser.CobaltVersion1)
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

var headerGuardTests = []struct {
	projectName  string
	customerName string
	namespaces   []string
	expected     string
}{
	{"Project", "Customer", []string{}, "COBALT_REGISTRY_PROJECT_CUSTOMER_GEN_"},
	{"PrOjeCt", "custOMER", []string{"single"}, "COBALT_REGISTRY_PR_OJE_CT_CUST_OMER_SINGLE_GEN_"},
	{"Pr)j#ct", "Cu$t@m#r", []string{"123", "!@#"}, "COBALT_REGISTRY_PR_J_CT_CU_T_M_R_123_GEN_"},
	{"AnotherProject", "AlsoCustomer", []string{"a", "very", "nested", "namespace", "for", "some", "reason"}, "COBALT_REGISTRY_ANOTHER_PROJECT_ALSO_CUSTOMER_A_VERY_NESTED_NAMESPACE_FOR_SOME_REASON_GEN_"},
}

func TestGetHeaderGuard(t *testing.T) {
	for _, tt := range headerGuardTests {
		result := getHeaderGuard(tt.projectName, tt.customerName, tt.namespaces)
		if result != tt.expected {
			t.Errorf("getHeaderGuard(%v, %v, %v), %v != %v", tt.projectName, tt.customerName, tt.namespaces, result, tt.expected)
		}
	}
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_parser

import (
	"config"
	"fmt"
	"strings"
	"testing"

	proto "github.com/golang/protobuf/proto"
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

const customersYaml = `
- customer_name: fuchsia
  customer_id: 1
  projects:
    - project_name: ledger
      project_id: 1
      project_contact: bob
    - project_name: module_usage_tracking
      project_id: 2
      project_contact: bob
- customer_name: test_customer
  customer_id: 100
  projects:
    - project_name: test_project
      project_id: 1
      project_contact: bob
`

const invalidCustomersYaml = `
- customer_name: fuchsia
  customer_id: 1
  projects:
    - naINVALIDme: ledger
      project_id: 1
      project_contact: bob
    - project_name: module_usage_tracking
      project_id: 2
      project_contact: bob
- customer_name: test_customer
  customer_id: 100
  projects:
    - project_name: test_project
      project_id: 1
      project_contact: bob
`

const projectConfigYaml = `
metric_definitions:
  - id: 1
    metric_name: "MetricB1a"
`

// Tests the ReadProjectConfig function's basic functionality.
func TestReadProjectConfig(t *testing.T) {
	r := memConfigReader{}
	r.SetProject("customer", "project", projectConfigYaml)
	c := ProjectConfig{
		CustomerName: "customer",
		CustomerId:   10,
		ProjectName:  "project",
		ProjectId:    5,
	}
	if err := ReadProjectConfig(r, &c); err != nil {
		t.Errorf("Error reading project config: %v", err)
	}
}

// Tests the ReadConfig function's basic functionality.
func TestReadConfig(t *testing.T) {
	r := memConfigReader{
		customers: customersYaml}
	r.SetProject("fuchsia", "ledger", projectConfigYaml)
	r.SetProject("fuchsia", "module_usage_tracking", projectConfigYaml)
	r.SetProject("test_customer", "test_project", projectConfigYaml)
	l := []ProjectConfig{}
	if err := ReadConfig(r, &l); err != nil {
		t.Errorf("Error reading project config: %v", err)
	}

	if 3 != len(l) {
		t.Errorf("Expected 3 customers. Got %v.", len(l))
	}
}

// Tests that ReadConfig customer parsing failures include the YAML file project_name.
func TestReadInvalidConfig(t *testing.T) {
	r := memConfigReader{
		customers: invalidCustomersYaml}
	r.SetProject("fuchsia", "ledger", projectConfigYaml)
	r.SetProject("fuchsia", "module_usage_tracking", projectConfigYaml)
	r.SetProject("test_customer", "test_project", projectConfigYaml)
	l := []ProjectConfig{}
	if err := ReadConfig(r, &l); err != nil {
		if !strings.Contains(err.Error(), "fake/customers.yaml") {
			t.Errorf("Error message did not contain the expected fake/customers.yaml: %v", err)
		}
	} else {
		t.Errorf("Expected to get an error")
	}
}

func TestMergeConfigs(t *testing.T) {
	l := []ProjectConfig{
		ProjectConfig{
			CustomerName:  "customer5",
			CustomerId:    5,
			ProjectName:   "project3",
			ProjectId:     3,
			Contact:       "project3@customer5.com",
			CobaltVersion: CobaltVersion1,
		},
		ProjectConfig{
			CustomerName:  "customer2",
			CustomerId:    2,
			ProjectName:   "project1",
			ProjectId:     1,
			Contact:       "project1@customer2.com",
			CobaltVersion: CobaltVersion1,
		},
		ProjectConfig{
			CustomerName:  "customer5",
			CustomerId:    5,
			ProjectName:   "project2",
			ProjectId:     2,
			Contact:       "project2@customer5.com",
			CobaltVersion: CobaltVersion1,
		},
	}

	s := MergeConfigs(l)

	expected := config.CobaltRegistry{
		Customers: []*config.CustomerConfig{
			&config.CustomerConfig{
				CustomerName: "customer2",
				CustomerId:   2,
				Projects: []*config.ProjectConfig{
					&config.ProjectConfig{
						ProjectName:    "project1",
						ProjectId:      1,
						ProjectContact: "project1@customer2.com",
					},
				},
			},
			&config.CustomerConfig{
				CustomerName: "customer5",
				CustomerId:   5,
				Projects: []*config.ProjectConfig{
					&config.ProjectConfig{
						ProjectName:    "project2",
						ProjectId:      2,
						ProjectContact: "project2@customer5.com",
					},
					&config.ProjectConfig{
						ProjectName:    "project3",
						ProjectId:      3,
						ProjectContact: "project3@customer5.com",
					},
				},
			},
		},
	}
	if !proto.Equal(&s, &expected) {
		t.Errorf("%v != %v", s, expected)
	}
}

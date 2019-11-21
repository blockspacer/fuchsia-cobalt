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
	"reflect"
	"testing"

	yaml "github.com/go-yaml/yaml"
)

// Basic test case for parseCustomerList.
func TestParseCustomerList(t *testing.T) {
	y := `
- customer_name: fuchsia
  customer_id: 20
  projects:
  - project_name: ledger
    project_contact: ben
    project_id: 10
- customer_name: test_project
  customer_id: 25
  projects:
  - project_name: ledger
    project_id: 10
    project_contact: ben
`

	e := []ProjectConfig{
		ProjectConfig{
			CustomerName:  "fuchsia",
			CustomerId:    20,
			ProjectName:   "ledger",
			ProjectId:     10,
			Contact:       "ben",
			CobaltVersion: CobaltVersion1,
		},
		ProjectConfig{
			CustomerName:  "test_project",
			CustomerId:    25,
			ProjectName:   "ledger",
			ProjectId:     10,
			Contact:       "ben",
			CobaltVersion: CobaltVersion1,
		},
	}

	l := []ProjectConfig{}
	if err := parseCustomerList(y, &l); err != nil {
		t.Error(err)
	}

	if !reflect.DeepEqual(e, l) {
		t.Errorf("%v != %v", e, l)
	}
}

// Tests that duplicated customer project_names and ids result in errors.
func TestParseCustomerListDuplicateValues(t *testing.T) {
	var y string
	l := []ProjectConfig{}

	// Checks that an error is returned if a duplicate customer project_name is used.
	y = `
- customer_name: fuchsia
  customer_id: 10
  projects:
  - project_name: ledger
    project_contact: ben
- customer_name: fuchsia
  customer_id: 11
  projects:
  - project_name: ledger
    project_contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer list with duplicate customer project_names.")
	}

	// Checks that an error is returned if a duplicate customer id is used.
	y = `
- customer_name: fuchsia
  customer_id: 10
  projects:
  - project_name: ledger
    project_contact: ben
- customer_name: test_project
  customer_id: 10
  projects:
  - project_name: ledger
    project_contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer list with duplicate customer ids.")
	}
}

// Tests the customer project_name validation logic.
func TestParseCustomerListNameValidation(t *testing.T) {
	var y string
	l := []ProjectConfig{}

	// Checks that an error is returned if no customer project_name is specified.
	y = `
- customer_id: 20
  projects:
  - project_name: ledger
    project_contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer without a project_name.")
	}

	// Checks that an error is returned if the customer project_name has an invalid type.
	y = `
- customer_name: 20
  customer_id: 10
  projects:
  - project_name: ledger
    project_contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer with invalid project_name type.")
	}

	// Checks that an error is returned if a project_name is invalid.
	y = `
- customer_name: hello world
  customer_id: 10
  projects:
  - project_name: ledger
    project_contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer with invalid project_name.")
	}
}

// Tests the customer id validation logic.
func TestParseCustomerListIdValidation(t *testing.T) {
	var y string
	l := []ProjectConfig{}

	// Checks that an error is returned if no customer id is specified.
	y = `
- customer_name: fuchsia
  projects:
  - project_name: ledger
    project_contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer without an id.")
	}

	// Checks that an error is returned if a non-numeric customer id is specified.
	y = `
- customer_id: fuchsia
  customer_name: fuchsia
  projects:
  - project_name: ledger
    project_contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer with non-numeric id.")
	}

	// Checks that an error is returned if a negative customer id is specified.
	y = `
- customer_id: -10
  customer_name: fuchsia
  projects:
  - project_name: ledger
    project_contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer with negative id.")
	}
}

// Allows tests to specify input data in yaml when testing populateProjectList.
func parseProjectListForTest(content string, l *[]ProjectConfig) (err error) {
	var y []interface{}
	if err := yaml.Unmarshal([]byte(content), &y); err != nil {
		panic(err)
	}

	return populateProjectList(y, l)
}

// Basic test case for populateProjectList.
func TestPopulateProjectList(t *testing.T) {
	y := `
- project_name: ledger
  project_contact: ben,etienne
  project_id: 10
- project_name: zircon
  project_id: 20
  project_contact: yvonne
`

	l := []ProjectConfig{}
	if err := parseProjectListForTest(y, &l); err != nil {
		t.Error(err)
	}

	e := []ProjectConfig{
		ProjectConfig{
			ProjectName:   "ledger",
			ProjectId:     10,
			Contact:       "ben,etienne",
			CobaltVersion: CobaltVersion1,
		},
		ProjectConfig{
			ProjectName:   "zircon",
			ProjectId:     20,
			Contact:       "yvonne",
			CobaltVersion: CobaltVersion1,
		},
	}
	if !reflect.DeepEqual(e, l) {
		t.Errorf("%v != %v", e, l)
	}
}

// Test duplicate project project_name or id validation logic.
func TestDuplicateProjectValuesValidation(t *testing.T) {
	var y string
	var l []ProjectConfig
	// Checks that an error is returned if a project_name is duplicated.
	y = `
- project_name: ledger
  project_contact: ben
- project_name: ledger
  project_contact: yvonne
`

	l = []ProjectConfig{}
	if err := parseProjectListForTest(y, &l); err == nil {
		t.Errorf("Accepted list with duplicate project project_name.")
	}
}

// Allows tests to specify inputs in yaml when testing populateProjectConfig.
func parseProjectConfigForTest(content string, c *ProjectConfig) (err error) {
	var y map[string]interface{}
	if err := yaml.Unmarshal([]byte(content), &y); err != nil {
		panic(err)
	}

	return populateProjectConfig(y, c)
}

// Checks validation for the project_name field.
func TestPopulateProjectListNameValidation(t *testing.T) {
	var y string
	var c ProjectConfig
	// Checks that an error is returned if a project_name is the wrong type.
	y = `
project_name: 10
project_contact: ben
`
	c = ProjectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project with numeric project_name.")
	}

	// Checks that an error is returned if a project_name is invalid.
	y = `
project_name: hello world
project_contact: ben
`
	c = ProjectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project with invalid project_name.")
	}

	// Checks that an error is returned if no project_name is provided for a project.
	y = `
project_contact: ben
`
	c = ProjectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project without project_name.")
	}
}

// Checks validation for the cobalt_version field.
func TestPopulateProjectListVersionValidation(t *testing.T) {
	var y string
	var c ProjectConfig

	y = `
project_name: ledger
project_id: 1
project_contact: ben
cobalt_version: 0
`
	c = ProjectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted a project with Cobalt version 0: %v", err)
	}

	y = `
project_name: ledger
project_contact: ben
`
	c = ProjectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted a project without an ID")
	}

	// Checks that an error is returned if the Cobalt version is an unexpected value.
	y = `
project_name: ledger
project_contact: ben
cobalt_version: 10
`
	c = ProjectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project with invalid Cobalt version.")
	}
}

// Checks validation for the id field.
func TestPopulateProjectListIdValidation(t *testing.T) {
	var y string
	var c ProjectConfig

	// Checks that an error is returned if the id missing.
	y = `
project_name: ledger
project_contact: ben
cobalt_version: 0
`
	c = ProjectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project without id.")
	}

	// Checks that Cobalt version 1 projects without an id are accepted.
	y = `
project_name: ledger
project_contact: ben
`
	c = ProjectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted a project without an ID")
	}

	// Checks that an error is returned if the id is an invalid type.
	y = `
project_name: ledger
project_id: ledger
project_contact: ben
cobalt_version: 0
`
	c = ProjectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project with non-integer id.")
	}

	// Checks that an error is returned if the id is negative.
	y = `
project_name: ledger
project_id: -10
project_contact: ben
cobalt_version: 0
`
	c = ProjectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project with negative id.")
	}
}

// Checks validation for the project_contact field.
func TestPopulateProjectListContactValidation(t *testing.T) {
	var y string
	var c ProjectConfig

	// Checks that an error is returned if a project_contact is the wrong type.
	y = `
project_name: ledger
project_id: 1
project_contact: 10
`
	c = ProjectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project with numeric project_contact.")
	}

	// Checks that an error is returned if a project_contact is missing.
	y = `
project_name: ledger
project_id: 10
`
	c = ProjectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project without project_contact.")
	}
}

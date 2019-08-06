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

// Functions in this file parse a yaml string that lists all Cobalt customers
// and their associated projects. It is used in order to find where the project
// configs are stored.

package config_parser

import (
	"fmt"
	"math"
	"regexp"

	yaml "github.com/go-yaml/yaml"
	"github.com/golang/glog"
)

var validNameRegexp = regexp.MustCompile("^[a-zA-Z][_a-zA-Z0-9]{1,81}$")

// Parse a list of customers appending all their projects to the ProjectConfig
// list that was passed in.
func parseCustomerList(content string, l *[]ProjectConfig) (err error) {
	var y []map[string]interface{}
	if err := yaml.Unmarshal([]byte(content), &y); err != nil {
		return fmt.Errorf("Error while parsing the yaml for a list of Cobalt customer definitions: %v", err)
	}

	customerNames := map[string]bool{}
	customerIds := map[int]bool{}
	for i, customer := range y {
		v, ok := customer["customer_name"]
		if !ok {
			return fmt.Errorf("customer_name field is missing in entry %v of the customer list.", i)
		}
		customerName, ok := v.(string)
		if !ok {
			return fmt.Errorf("Customer name '%v' is not a string.", v)
		}
		if !validNameRegexp.MatchString(customerName) {
			return fmt.Errorf("Customer name '%v' is invalid. Customer names must match the regular expression '%v'", customerName, validNameRegexp)
		}
		if customerNames[customerName] {
			return fmt.Errorf("Customer name '%v' repeated. Customer names must be unique.", customerName)
		}
		customerNames[customerName] = true

		v, ok = customer["customer_id"]
		if !ok {
			return fmt.Errorf("Missing customer id for '%v'.", customerName)
		}
		customerId, ok := v.(int)
		if !ok {
			return fmt.Errorf("Customer id '%v' for '%v' is not numeric.", customerId, customerName)
		}
		if customerId < 0 {
			return fmt.Errorf("Customer id for '%v' is negative. Customer ids must be positive.", customerName)
		}
		if customerId == math.MaxInt32 && customerName != "cobalt_internal" {
			return fmt.Errorf("Customer '%v' tried to register id %d, which is reserved for the internal customer 'cobalt_internal'.", customerName, math.MaxInt32)
		}
		if customerIds[customerId] {
			return fmt.Errorf("Customer id %v for customer '%v' repeated. Customer names must be unique.", customerId, customerName)
		}
		customerIds[customerId] = true

		projectsAsI, ok := customer["projects"]
		if !ok {
			glog.Warningf("No projects found for customer '%v'.", customerName)
			continue
		}

		projectsAsList, ok := projectsAsI.([]interface{})
		if !ok {
			fmt.Errorf("Project list for customer %v is invalid. It should be a yaml list.", customerName)
		}

		c := []ProjectConfig{}
		if err := populateProjectList(projectsAsList, &c); err != nil {
			return fmt.Errorf("Project list for customer %v is invalid: %v", customerName, err)
		}

		for i := range c {
			c[i].CustomerId = uint32(customerId)
			c[i].CustomerName = customerName
		}
		*l = append(*l, c...)
	}

	return nil

}

// populateProjectList populates a list of cobalt projects given in the form of
// a map as returned by a call to yaml.Unmarshal. For more details, see
// populateProjectConfig. This function also validates that project names and
// ids are unique.
func populateProjectList(y []interface{}, l *[]ProjectConfig) (err error) {
	projectNames := map[string]bool{}
	projectIds := map[uint32]bool{}
	for i, v := range y {
		m, ok := v.(map[interface{}]interface{})
		if !ok {
			return fmt.Errorf("Entry %v in project list is not a yaml map.", i)
		}
		p, err := toStrMap(m)
		if err != nil {
			return fmt.Errorf("Entry %v in project list is not valid: %v", i, err)
		}
		c := ProjectConfig{}
		if err := populateProjectConfig(p, &c); err != nil {
			return fmt.Errorf("Error in entry %v in project list: %v", i, err)
		}

		if projectNames[c.ProjectName] {
			return fmt.Errorf("Project name '%v' repeated. Project names must be unique.", c.ProjectName)
		}
		projectNames[c.ProjectName] = true

		if projectIds[c.ProjectId] {
			return fmt.Errorf("Project id %v for project %v is repeated. Project ids must be unique.", c.ProjectId, c.ProjectName)
		}
		projectIds[c.ProjectId] = true

		*l = append(*l, c)
	}
	return
}

// populateProjectConfig populates a cobalt project given in the form of a map
// as returned by a call to yaml.Unmarshal. It populates the name, projectId and
// contact fields of the ProjectConfig it returns. It also validates those
// values. The project id must be a positive integer. The project must have
// name, id and contact fields.
func populateProjectConfig(p map[string]interface{}, c *ProjectConfig) (err error) {
	v, ok := p["name"]
	if !ok {
		return fmt.Errorf("Missing name in project list.")
	}
	c.ProjectName, ok = v.(string)
	if !ok {
		return fmt.Errorf("Project name '%v' is not a string.", v)
	}
	if !validNameRegexp.MatchString(c.ProjectName) {
		return fmt.Errorf("Project name '%v' is invalid. Project names must match the regular expression '%v'", c.ProjectName, validNameRegexp)
	}

	c.CobaltVersion = CobaltVersion0
	v, ok = p["cobalt_version"]
	if ok {
		version, ok := v.(int)
		if !ok {
			return fmt.Errorf("Cobalt version '%v' for project %v is not an integer.", v, c.ProjectName)
		}
		if version == 0 {
			c.CobaltVersion = CobaltVersion0
		} else if version == 1 {
			c.CobaltVersion = CobaltVersion1
		} else {
			return fmt.Errorf("Version '%v' for project %v is not '1' or '0'.", version, c.ProjectName)
		}
	}

	if c.CobaltVersion == CobaltVersion1 {
		_, ok = p["id"]
		if ok {
			return fmt.Errorf("Project %v is using version 1.0. Version 1.0 projects may not specify an id.", c.ProjectName)
		}
		c.ProjectId = IdFromName(c.ProjectName)
	} else {
		v, ok = p["id"]
		if !ok {
			return fmt.Errorf("Missing id for project %v.", c.ProjectName)
		}
		projectId, ok := v.(int)
		if !ok {
			return fmt.Errorf("Id '%v' for project %v is not an integer.", v, c.ProjectName)
		}
		if projectId <= 0 {
			return fmt.Errorf("Id for project %v is not a positive integer.", c.ProjectName)
		}
		c.ProjectId = uint32(projectId)
	}

	v, ok = p["contact"]
	if !ok {
		return fmt.Errorf("Missing contact for project %v.", c.ProjectName)
	}
	c.Contact, ok = v.(string)
	if !ok {
		return fmt.Errorf("Contact '%v' for project %v is not a string.", v, c.ProjectName)
	}

	return nil
}

func toStrMap(i map[interface{}]interface{}) (o map[string]interface{}, err error) {
	o = make(map[string]interface{})
	for k, v := range i {
		s, ok := k.(string)
		if !ok {
			return nil, fmt.Errorf("Expected a yaml map with string keys. '%v' is not a string.", k)
		}

		o[s] = v
	}

	return o, nil
}

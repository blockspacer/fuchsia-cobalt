// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements reading the Cobalt configuration from a directory.
// See ReadConfigFromDir for details.

package config_parser

import (
	"config"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"sort"
)

// ReadConfigFromDir reads the whole configuration for Cobalt from a directory on the file system.
// It is assumed that <rootDir>/projects.yaml contains the customers and projects list. (see project_list.go)
// It is assumed that <rootDir>/<customerName>/<projectName>/config.yaml
// contains the configuration for a project. (see project_config.go)
func ReadConfigFromDir(rootDir string) (c []ProjectConfig, err error) {
	r, err := newConfigReaderForDir(rootDir)
	if err != nil {
		return c, err
	}

	if err := ReadConfig(r, &c); err != nil {
		return c, err
	}

	return c, nil
}

func ReadProjectConfigFromDir(rootDir string, customerId uint32, projectId uint32) (c ProjectConfig, err error) {
	r, err := newConfigReaderForDir(rootDir)
	if err != nil {
		return c, err
	}

	l := []ProjectConfig{}
	if err = readProjectsList(r, &l); err != nil {
		return c, err
	}

	for i := range l {
		config := &l[i]
		if config.CustomerId == customerId && config.ProjectId == projectId {
			if err = ReadProjectConfig(r, config); err != nil {
				return c, fmt.Errorf("Error reading config for %v %v: %v", config.CustomerName, config.ProjectName, err)
			}
			return *config, nil
		}
	}

	return c, fmt.Errorf("Could not find config for customer %d, project %d", customerId, projectId)
}

func ReadProjectConfigFromDirByName(rootDir string, customerId uint32, projectName string) (c ProjectConfig, err error) {
	r, err := newConfigReaderForDir(rootDir)
	if err != nil {
		return c, err
	}

	l := []ProjectConfig{}
	if err = readProjectsList(r, &l); err != nil {
		return c, err
	}

	for i := range l {
		config := &l[i]
		if config.CustomerId == customerId && config.ProjectName == projectName {
			if err = ReadProjectConfig(r, config); err != nil {
				return c, fmt.Errorf("Error reading config for %v %v: %v", config.CustomerName, config.ProjectName, err)
			}
			return *config, nil
		}
	}

	return c, fmt.Errorf("Could not find config for customer %d, project %q", customerId, projectName)
}

// ReadConfigFromYaml reads the configuration for a single project from a single yaml file.
// See project_config.go for the format.
func ReadConfigFromYaml(yamlConfigPath string, customerId uint32, projectId uint32, version CobaltVersion) (c ProjectConfig, err error) {
	r, err := newConfigReaderForFile(yamlConfigPath)
	if err != nil {
		return c, err
	}

	c.CustomerId = customerId
	c.ProjectId = projectId
	c.CobaltVersion = version
	if err := ReadProjectConfig(r, &c); err != nil {
		return c, err
	}

	return c, nil
}

// GetConfigFilesListFromConfigDir reads the configuration for Cobalt from a
// directory on the file system (See ReadConfigFromDir) and returns the list
// of files which constitute the configuration. The purpose is generating a
// list of dependencies.
func GetConfigFilesListFromConfigDir(rootDir string) (files []string, err error) {
	r, err := newConfigDirReader(rootDir)
	if err != nil {
		return files, err
	}

	l := []ProjectConfig{}
	if err := readProjectsList(r, &l); err != nil {
		return files, err
	}

	files = append(files, r.CustomersFilePath())

	for i, _ := range l {
		c := &(l[i])
		files = append(files, r.projectFilePath(c.CustomerName, c.ProjectName))
	}
	return files, nil
}

// configReader is an interface that returns configuration data in the yaml format.
type configReader interface {
	// Returns the yaml representation of the customer and project list.
	// See project_list.go
	Customers() (string, error)
	// Returns the yaml representation of the configuration for a particular project.
	// See project_config.go
	Project(customerName string, projectName string) (string, error)
	// Returns the path to the yaml representation of the customer list.
	CustomersFilePath() string
}

// configDirReader is an implementation of configReader where the configuration
// data is stored in configDir.
type configDirReader struct {
	configDir string
}

// newConfigDirReader returns a pointer to a configDirReader which will read the
// Cobalt configuration stored in the provided directory.
func newConfigDirReader(configDir string) (r *configDirReader, err error) {
	info, err := os.Stat(configDir)
	if err != nil {
		return nil, err
	}

	if !info.IsDir() {
		return nil, fmt.Errorf("%v is not a directory.", configDir)
	}

	return &configDirReader{configDir: configDir}, nil
}

// newConfigReaderForDir returns a configReader which will read the Cobalt
// configuration stored in the provided directory.
func newConfigReaderForDir(configDir string) (r configReader, err error) {
	return newConfigDirReader(configDir)
}

func (r *configDirReader) CustomersFilePath() string {
	// The customer and project list is at <rootDir>/projects.yaml
	return filepath.Join(r.configDir, "projects.yaml")
}

func (r *configDirReader) Customers() (string, error) {
	customerList, err := ioutil.ReadFile(r.CustomersFilePath())
	if err != nil {
		return "", err
	}
	return string(customerList), nil
}

func (r *configDirReader) projectFilePath(customerName string, projectName string) string {
	// A project's metrics is at <rootDir>/<customerName>/<projectName>/metrics.yaml
	return filepath.Join(r.configDir, customerName, projectName, "metrics.yaml")
}

func (r *configDirReader) Project(customerName string, projectName string) (string, error) {
	projectConfig, err := ioutil.ReadFile(r.projectFilePath(customerName, projectName))
	if err != nil {
		return "", err
	}
	return string(projectConfig), nil
}

// configFileReader is an implementation of configReader where the configuration
// data is stored in a single file.
type configFileReader struct {
	configFile string
}

// newConfigFileReader returns a pointer to a configFileReader which will read
// the Cobalt configuration stored in the provided file.
func newConfigFileReader(configFile string) (r *configFileReader, err error) {
	info, err := os.Stat(configFile)
	if err != nil {
		return nil, err
	}

	if !info.Mode().IsRegular() {
		return nil, fmt.Errorf("%v is not a file.", configFile)
	}

	return &configFileReader{configFile: configFile}, nil
}

// newConfigReaderForFile returns a pointer to a configReader which will read the
// Cobalt configuration stored in the provided file.
func newConfigReaderForFile(configFile string) (r configReader, err error) {
	return newConfigFileReader(configFile)
}

func (r *configFileReader) Customers() (string, error) {
	return "", fmt.Errorf("configFileReader does not have a customers list.")
}

func (r *configFileReader) CustomersFilePath() string {
	return ""
}

func (r *configFileReader) Project(_ string, _ string) (string, error) {
	projectConfig, err := ioutil.ReadFile(r.configFile)
	if err != nil {
		return "", err
	}

	return string(projectConfig), nil
}

func readProjectsList(r configReader, l *[]ProjectConfig) (err error) {
	// First, we get and parse the customer list.
	customerListYaml, err := r.Customers()
	if err != nil {
		return err
	}

	if err = parseCustomerList(customerListYaml, l); err != nil {
		return fmt.Errorf("Error parsing customer list YAML file %v: %v", r.CustomersFilePath(), err)
	}

	return nil
}

// ReadConfig reads and parses the configuration for all projects from a configReader.
func ReadConfig(r configReader, l *[]ProjectConfig) (err error) {
	if err = readProjectsList(r, l); err != nil {
		return err
	}

	// Then, based on the customer list, we read and parse all the project configs.
	for i, _ := range *l {
		c := &((*l)[i])
		if err = ReadProjectConfig(r, c); err != nil {
			return fmt.Errorf("Error reading config for %v %v: %v", c.CustomerName, c.ProjectName, err)
		}
	}

	return nil
}

// ReadProjectConfig reads the configuration of a particular project.
func ReadProjectConfig(r configReader, c *ProjectConfig) (err error) {
	configYaml, err := r.Project(c.CustomerName, c.ProjectName)
	if err != nil {
		return err
	}
	return parseProjectConfig(configYaml, c)
}

// cmpConfigEntry takes two protobuf pointers that must have the fields
// "CustomerId", "ProjectId", and "Id". It is used in generically sorting the
// config entries in the ProjectConfigFile proto.
func cmpConfigEntry(i, j interface{}) bool {
	a := reflect.ValueOf(i).Elem()
	b := reflect.ValueOf(j).Elem()

	aCi := a.FieldByName("CustomerId").Uint()
	bCi := b.FieldByName("CustomerId").Uint()
	if aCi != bCi {
		return aCi < bCi
	}

	aPi := a.FieldByName("ProjectId").Uint()
	bPi := b.FieldByName("ProjectId").Uint()
	if aPi != bPi {
		return aPi < bPi
	}

	ai := a.FieldByName("Id").Uint()
	bi := b.FieldByName("Id").Uint()
	if ai != bi {
		return ai < bi
	}

	return false
}

// MergeConfigs accepts a list of ProjectConfigFile protos each of which contains the
// encoding, metric and report configs for a particular project and aggregates
// all those into a single ProjectConfigFile proto.
func MergeConfigs(l []ProjectConfig) (s config.CobaltRegistry) {
	customers := map[uint32]int{}

	for _, c := range l {
		if c.CobaltVersion != CobaltVersion1 {
			continue
		}

		if _, ok := customers[c.CustomerId]; !ok {
			s.Customers = append(s.Customers, &config.CustomerConfig{
				CustomerName: c.CustomerName, CustomerId: c.CustomerId})
			customers[c.CustomerId] = len(s.Customers) - 1
		}
		cidx := customers[c.CustomerId]

		s.Customers[cidx].Projects = append(s.Customers[cidx].Projects, v1ProjectConfig(c))
	}

	sort.SliceStable(s.Customers, func(i, j int) bool {
		return s.Customers[i].CustomerId < s.Customers[j].CustomerId
	})

	for ci := range s.Customers {
		sort.SliceStable(s.Customers[ci].Projects, func(i, j int) bool {
			return s.Customers[ci].Projects[i].ProjectId < s.Customers[ci].Projects[j].ProjectId
		})
	}

	return s
}

// v1Projectconfig converts an internal representation of a ProjectConfig into the ProjectConfig proto.
func v1ProjectConfig(c ProjectConfig) *config.ProjectConfig {
	p := config.ProjectConfig{}
	p.ProjectName = c.ProjectName
	p.ProjectId = c.ProjectId
	p.ProjectContact = c.Contact
	p.Metrics = c.ProjectConfig.MetricDefinitions

	// In order to ensure that we output a stable order in the binary protobuf, we
	// sort the metric definitions.
	sort.SliceStable(p.Metrics, func(i, j int) bool {
		return cmpConfigEntry(p.Metrics[i], p.Metrics[j])
	})

	return &p
}

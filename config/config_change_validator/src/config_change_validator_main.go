// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a program that compares two binary encoded CobaltRegistry,
// and validates that the changes between them are backwards compatible.
package main

import (
	"config"
	"flag"
	"fmt"
	"io/ioutil"

	"github.com/golang/glog"
	"github.com/golang/protobuf/proto"
)

var (
	oldCfg = flag.String("old_config", "", "Path of the old generated config.")
	newCfg = flag.String("new_config", "", "Path of the new generated config.")
)

func check(e error) {
	if e != nil {
		panic(e)
	}
}

func main() {
	flag.Parse()

	if (*oldCfg == "") || (*newCfg == "") {
		glog.Exit("Both 'old_config' and 'new_config' are required")
	}

	old := config.CobaltRegistry{}
	n := config.CobaltRegistry{}

	data, err := ioutil.ReadFile(*oldCfg)
	check(err)
	proto.Unmarshal(data, &old)

	data, err = ioutil.ReadFile(*newCfg)
	check(err)
	proto.Unmarshal(data, &n)

	check(CompareConfigs(&old, &n))
}

func CompareConfigs(oldCfg, newCfg *config.CobaltRegistry) error {
	oldCustomers := map[string]*config.CustomerConfig{}
	newCustomers := map[string]*config.CustomerConfig{}

	for _, cust := range oldCfg.Customers {
		oldCustomers[cust.CustomerName] = cust
	}

	for _, cust := range newCfg.Customers {
		newCustomers[cust.CustomerName] = cust
	}

	for name, oldCust := range oldCustomers {
		newCust, ok := newCustomers[name]
		if ok {
			err := CompareCustomers(oldCust, newCust)
			if err != nil {
				return fmt.Errorf("for customer named '%s': %v", name, err)
			}
		}
	}

	return nil
}

func CompareCustomers(oldCfg, newCfg *config.CustomerConfig) error {
	oldProjects := map[string]*config.ProjectConfig{}
	newProjects := map[string]*config.ProjectConfig{}

	for _, proj := range oldCfg.Projects {
		oldProjects[proj.ProjectName] = proj
	}

	for _, proj := range newCfg.Projects {
		newProjects[proj.ProjectName] = proj
	}

	for name, oldProj := range oldProjects {
		newProj, ok := newProjects[name]
		if ok {
			err := CompareProjects(oldProj, newProj)
			if err != nil {
				return fmt.Errorf("for project named '%s': %v", name, err)
			}
		}
	}

	return nil
}

func CompareProjects(oldCfg, newCfg *config.ProjectConfig) error {
	oldMetrics := map[string]*config.MetricDefinition{}
	newMetrics := map[string]*config.MetricDefinition{}

	for _, metric := range oldCfg.Metrics {
		oldMetrics[metric.MetricName] = metric
	}

	for _, metric := range newCfg.Metrics {
		newMetrics[metric.MetricName] = metric
	}

	for name, oldMetric := range oldMetrics {
		newMetric, ok := newMetrics[name]
		if ok {
			err := CompareMetrics(oldMetric, newMetric)
			if err != nil {
				return fmt.Errorf("for metric named '%s': %v", name, err)
			}
		}
	}

	// Validation for all metrics.
	for _, newMetric := range newMetrics {
		if newMetric.MetaData.AlsoLogLocally {
			return fmt.Errorf("also_log_locally may not be merged into master: Metric `%s`", newMetric.MetricName)
		}
	}

	return nil
}

func CompareMetrics(oldMetric, newMetric *config.MetricDefinition) error {
	// Debug metrics are ignored for the purposes of change validation.
	if oldMetric.MetaData.MaxReleaseStage <= config.ReleaseStage_DEBUG {
		return nil
	}

	if newMetric.MetaData.MaxReleaseStage <= config.ReleaseStage_DEBUG {
		return nil
	}

	oldDimensions := map[int]*config.MetricDefinition_MetricDimension{}
	newDimensions := map[int]*config.MetricDefinition_MetricDimension{}

	for ix, dim := range oldMetric.MetricDimensions {
		oldDimensions[ix] = dim
	}

	for ix, dim := range newMetric.MetricDimensions {
		newDimensions[ix] = dim
	}

	for ix, oldDimension := range oldDimensions {
		newDimension, ok := newDimensions[ix]
		if ok {
			err := CompareDimensions(oldDimension, newDimension)
			if err != nil {
				return fmt.Errorf("for dimension index '%d': %v", ix, err)
			}
		}
	}

	return nil
}

func CompareDimensions(oldDimension, newDimension *config.MetricDefinition_MetricDimension) error {
	for code, oldName := range oldDimension.EventCodes {
		_, ok := newDimension.EventCodes[code]
		if !ok {
			return fmt.Errorf("removing an event code is not allowed: %d: %s", code, oldName)
		}
	}

	return nil
}

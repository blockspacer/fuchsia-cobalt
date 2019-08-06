// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"fmt"
	"github.com/golang/glog"
	"regexp"
	"time"
)

// Note that when reports are serialized to CSV, the column headers used are
// derived from metric part names. Consequently, if this regular expression
// is modified then the code in the function
// EscapeMetricPartNameForCSVColumHeader() in
// //analyzer/report_master/report_serializer.cc must be modified.
var validMetricPartName = regexp.MustCompile("^[a-zA-Z][_a-zA-Z0-9\\- ]+$")

func validateConfiguredMetrics(config *config.ProjectConfigFile) (err error) {
	// Set of encodingIds. Used to check that the Metric only refers to valid encodingIds.
	encodingIds := map[uint32]bool{}

	// Set of metric ids. Used to detect duplicates.
	metricIds := map[uint32]bool{}

	for _, encoding := range config.EncodingConfigs {
		encodingIds[encoding.Id] = true
	}

	for i, metric := range config.MetricConfigs {
		metricKey := metric.Id

		if metricIds[metricKey] {
			return fmt.Errorf("Metric id %v is repeated in metric config entry number %v. Metric ids must be unique.", metricKey, i+1)
		}
		metricIds[metricKey] = true

		if err = validateMetric(metric, encodingIds); err != nil {
			return fmt.Errorf("Error validating metric %v (%v, %v, %v): %v", metric.Name, metric.CustomerId, metric.ProjectId, metric.Id, err)
		}
	}
	return nil
}

func validateMetric(m *config.Metric, encodingIds map[uint32]bool) (err error) {
	if m.Id == 0 {
		return fmt.Errorf("Metric id '0' is invalid.")
	}

	if m.ProjectId >= 100 {
		if m.GetMetaData() == nil || m.GetMetaData().ExpiresAfter == "" {
			return fmt.Errorf("expires_after is not present. All metrics with project_id > 100 must have an expires_after field set.")
		}

		oldestValidExpiry := time.Now().AddDate(1, 0, 0)
		date, err := time.Parse("2006/01/02", m.GetMetaData().ExpiresAfter)
		if err != nil {
			return fmt.Errorf("Unable to parse expires_after. Format should be yyyy/mm/dd. %v", err)
		}

		if date.After(oldestValidExpiry) {
			return fmt.Errorf("Expiry date '%v' is past the maximum expiry date of '%v'", date, oldestValidExpiry)
		}

		// We don't currently enforce expiry dates in code, but we should warn about it.
		if date.Before(time.Now()) {
			glog.Warningf("Metric '%v' (Customer %v, Project %v, Id %v) has expired.", m.Name, m.CustomerId, m.ProjectId, m.Id)
		} else if date.Before(time.Now().AddDate(0, 3, 0)) {
			glog.Warningf("Metric '%v' (Customer %v, Project %v, Id %v) will expire within 3 months.", m.Name, m.CustomerId, m.ProjectId, m.Id)
		}
	}

	for name, v := range m.Parts {
		if v == nil {
			return fmt.Errorf("Metric part '%v' is null. This is not allowed.", name)
		}

		if !validMetricPartName.MatchString(name) {
			return fmt.Errorf("Metric part name '%v' is invalid. Metric part names must match the regular expression '%v'.", name, validMetricPartName)
		}

		encodingId := v.DefaultEncodingId
		if !encodingIds[encodingId] {
			return fmt.Errorf("Metric part '%v' is invalid. There is no encoding id %v.", name, encodingId)
		}
	}
	return nil
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package validator

import (
	"config"
	"fmt"
)

func CompareProjects(oldCfg, newCfg *config.ProjectConfig, ignoreCompatChecks bool) error {
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
			err := CompareMetrics(oldMetric, newMetric, ignoreCompatChecks)
			if err != nil {
				return fmt.Errorf("for metric named '%s': %v", name, err)
			}
		}
	}

	// Validation for all metrics.
	for _, newMetric := range newMetrics {
		if newMetric.MetaData.AlsoLogLocally {
			return fmt.Errorf("The parameter also_log_locally is intended only for use on a development machine. This change may not be committed to the central Cobalt metrics registry. Metric `%s.", newMetric.MetricName)
		}
	}

	return nil
}

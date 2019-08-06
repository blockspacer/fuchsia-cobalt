// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package validator

import (
	"config"
	"fmt"
)

func CompareMetrics(oldMetric, newMetric *config.MetricDefinition, ignoreCompatChecks bool) error {
	// Debug metrics are ignored for the purposes of change validation.
	if oldMetric.MetaData != nil && oldMetric.MetaData.MaxReleaseStage <= config.ReleaseStage_DEBUG {
		return nil
	}

	if newMetric.MetaData != nil && newMetric.MetaData.MaxReleaseStage <= config.ReleaseStage_DEBUG {
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
			err := CompareDimensions(oldDimension, newDimension, ignoreCompatChecks)
			if err != nil {
				return fmt.Errorf("for dimension index '%d': %v", ix, err)
			}
		}
	}

	return nil
}

func CompareDimensions(oldDimension, newDimension *config.MetricDefinition_MetricDimension, ignoreCompatChecks bool) error {
	for code, oldName := range oldDimension.EventCodes {
		_, ok := newDimension.EventCodes[code]
		if !ok && !ignoreCompatChecks {
			return fmt.Errorf("removing an event code is not allowed: %d: %s", code, oldName)
		}
	}

	return nil
}

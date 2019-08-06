// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package validator

import (
	"config"
	"testing"
)

func TestAlsoLogLocally(t *testing.T) {
	oldCfg := &config.ProjectConfig{}
	invalidCfg := &config.ProjectConfig{
		Metrics: []*config.MetricDefinition{
			{
				MetaData: &config.MetricDefinition_Metadata{
					MaxReleaseStage: config.ReleaseStage_GA,
					AlsoLogLocally:  true,
				},
			},
		},
	}

	err := CompareProjects(oldCfg, invalidCfg, false)
	if err == nil {
		t.Errorf("expected invalid change, but was accepted")
	}

	if err = CompareProjects(oldCfg, invalidCfg, true); err == nil {
		t.Errorf("ignoreCompatChecks should not allow also_log_locally")
	}

	invalidCfg.Metrics[0].MetaData.MaxReleaseStage = config.ReleaseStage_DEBUG
	if err = CompareProjects(oldCfg, invalidCfg, false); err == nil {
		t.Errorf("even DEBUG metrics cannot have also_log_locally set")
	}
}

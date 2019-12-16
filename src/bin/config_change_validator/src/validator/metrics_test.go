// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package validator

import (
	"config"
	"testing"
)

var metricstests = []struct {
	name    string
	o       *config.MetricDefinition
	n       *config.MetricDefinition
	isValid bool
}{
	{
		"CompatibleChange",
		&config.MetricDefinition{
			MetaData: &config.MetricDefinition_Metadata{
				MaxReleaseStage: config.ReleaseStage_GA,
			},
		},
		&config.MetricDefinition{
			MetaData: &config.MetricDefinition_Metadata{
				MaxReleaseStage: config.ReleaseStage_GA,
			},
		}, true,
	},
	{
		"IncompatibleChange",
		&config.MetricDefinition{
			MetaData: &config.MetricDefinition_Metadata{
				MaxReleaseStage: config.ReleaseStage_GA,
			},
			MetricDimensions: []*config.MetricDefinition_MetricDimension{{
				EventCodes: map[uint32]string{
					100: "A code",
				},
			}},
		},
		&config.MetricDefinition{
			MetaData: &config.MetricDefinition_Metadata{
				MaxReleaseStage: config.ReleaseStage_GA,
			},
			MetricDimensions: []*config.MetricDefinition_MetricDimension{{
				EventCodes: map[uint32]string{
					101: "A different code",
				},
			}},
		},
		false,
	},
	{
		"AnotherIncompatibleChange",
		&config.MetricDefinition{
			MetaData: &config.MetricDefinition_Metadata{
				MaxReleaseStage: config.ReleaseStage_GA,
			},
			MetricDimensions: []*config.MetricDefinition_MetricDimension{{
				EventCodes: map[uint32]string{
					100: "A code",
					101: "A second code",
					102: "A third code",
					103: "A fourth code",
				},
			}},
		},
		&config.MetricDefinition{
			MetaData: &config.MetricDefinition_Metadata{
				MaxReleaseStage: config.ReleaseStage_GA,
			},
			MetricDimensions: []*config.MetricDefinition_MetricDimension{{
				EventCodes: map[uint32]string{
					100: "A code",
					101: "A second code",
					103: "A fourth code",
				},
			}},
		},
		false,
	},
	{
		"ChangingMaxEventCodeAllowedForNonEventOccurred",
		&config.MetricDefinition{
			MetaData: &config.MetricDefinition_Metadata{
				MaxReleaseStage: config.ReleaseStage_GA,
			},
			MetricDimensions: []*config.MetricDefinition_MetricDimension{{
				EventCodes: map[uint32]string{
					100: "A code",
				},
				MaxEventCode: 100,
			}},
		},
		&config.MetricDefinition{
			MetaData: &config.MetricDefinition_Metadata{
				MaxReleaseStage: config.ReleaseStage_GA,
			},
			MetricDimensions: []*config.MetricDefinition_MetricDimension{{
				EventCodes: map[uint32]string{
					100: "A code",
				},
				MaxEventCode: 101,
			}},
		},
		true,
	},
	{
		"ChangingMaxEventCodeNotAllowedEventOccurred",
		&config.MetricDefinition{
			MetaData: &config.MetricDefinition_Metadata{
				MaxReleaseStage: config.ReleaseStage_GA,
			},
			MetricType: config.MetricDefinition_EVENT_OCCURRED,
			MetricDimensions: []*config.MetricDefinition_MetricDimension{{
				EventCodes: map[uint32]string{
					100: "A code",
				},
				MaxEventCode: 100,
			}},
		},
		&config.MetricDefinition{
			MetaData: &config.MetricDefinition_Metadata{
				MaxReleaseStage: config.ReleaseStage_GA,
			},
			MetricType: config.MetricDefinition_EVENT_OCCURRED,
			MetricDimensions: []*config.MetricDefinition_MetricDimension{{
				EventCodes: map[uint32]string{
					100: "A code",
				},
				MaxEventCode: 101,
			}},
		},
		false,
	},
}

func TestCompareMetrics(t *testing.T) {
	for _, tt := range metricstests {
		t.Run(tt.name, func(t *testing.T) {
			err := CompareMetrics(tt.o, tt.n, false)
			if err != nil && tt.isValid {
				t.Errorf("expected valid change, got %v", err)
			}
			if err == nil && !tt.isValid {
				t.Errorf("expected invalid change, but was accepted")
			}

			if err = CompareMetrics(tt.o, tt.n, true); err != nil {
				t.Errorf("rejected change when ignoreCompatChecks=true, got %v", err)
			}

			origMeta := tt.o.MetaData
			tt.o.MetaData.MaxReleaseStage = config.ReleaseStage_DEBUG
			if err = CompareMetrics(tt.o, tt.n, false); err != nil {
				t.Errorf("rejected change when old.MetaData.MaxReleaseStage=DEBUG, got %v", err)
			}
			tt.o.MetaData = origMeta

			origMeta = tt.n.MetaData
			tt.n.MetaData.MaxReleaseStage = config.ReleaseStage_DEBUG
			if err = CompareMetrics(tt.n, tt.n, false); err != nil {
				t.Errorf("rejected change when new.MetaData.MaxReleaseStage=DEBUG, got %v", err)
			}
			tt.n.MetaData = origMeta
		})
	}
}

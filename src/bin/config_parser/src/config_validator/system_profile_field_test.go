// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"strings"
	"testing"
)

func TestValidateSystemProfileFields(t *testing.T) {
	var tests = []struct {
		config      *config.ProjectConfigFile
		expectedErr string
	}{
		{
			expectedErr: "",
			config:      &config.ProjectConfigFile{},
		},
		{
			expectedErr: "SystemProfileField: BOARD_NAME, but metric 1 does not supply it",
			config: &config.ProjectConfigFile{
				MetricConfigs: []*config.Metric{makeMetric(1, nil)},
				ReportConfigs: []*config.ReportConfig{
					makeReport(1, 1, []config.SystemProfileField{
						config.SystemProfileField_BOARD_NAME,
					}),
				},
			},
		},
		{
			expectedErr: "",
			config: &config.ProjectConfigFile{
				MetricConfigs: []*config.Metric{
					makeMetric(1, []config.SystemProfileField{
						config.SystemProfileField_BOARD_NAME,
					}),
				},
				ReportConfigs: []*config.ReportConfig{
					makeReport(1, 1, []config.SystemProfileField{
						config.SystemProfileField_BOARD_NAME,
					}),
				},
			},
		},
	}

	for _, tt := range tests {
		err := validateSystemProfileFields(tt.config)
		errStr := ""
		if err != nil {
			errStr = err.Error()
		}
		if !strings.Contains(errStr, tt.expectedErr) {
			t.Errorf("validateSystemProfileFields(%+v): expected %v, actual %v", tt.config, tt.expectedErr, err)
		}
	}
}

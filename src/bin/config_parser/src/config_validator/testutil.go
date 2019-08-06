// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
)

func makeMetric(id uint32, fields []config.SystemProfileField) *config.Metric {
	return &config.Metric{
		Id:                 id,
		CustomerId:         1,
		ProjectId:          1,
		SystemProfileField: fields,
	}
}

func makeReport(id, metricId uint32, fields []config.SystemProfileField) *config.ReportConfig {
	return &config.ReportConfig{
		Id:                 id,
		MetricId:           metricId,
		CustomerId:         1,
		ProjectId:          1,
		SystemProfileField: fields,
	}
}

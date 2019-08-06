// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"testing"
)

// Tests that we catch encodings with id = 0.
func TestValidateNoZeroEncodingIds(t *testing.T) {
	config := &config.ProjectConfigFile{
		EncodingConfigs: []*config.EncodingConfig{
			&config.EncodingConfig{
				CustomerId: 1,
				ProjectId:  1,
				Id:         0,
			},
		},
	}

	if err := validateConfiguredEncodings(config); err == nil {
		t.Error("Accepted encoding config with id of 0.")
	}
}

// Tests that we catch non-unique encoding ids.
func TestValidateUniqueEncodingIds(t *testing.T) {
	config := &config.ProjectConfigFile{
		EncodingConfigs: []*config.EncodingConfig{
			&config.EncodingConfig{
				CustomerId: 1,
				ProjectId:  1,
				Id:         1,
			},
			&config.EncodingConfig{
				CustomerId: 1,
				ProjectId:  1,
				Id:         1,
			},
		},
	}
	if err := validateConfiguredEncodings(config); err == nil {
		t.Error("Accepted non-unique encoding id.")
	}
}

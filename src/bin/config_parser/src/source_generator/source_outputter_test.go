// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source_generator

import "testing"

func TestGetOutputFormatter(t *testing.T) {
	formats := []string{"bin", "b64", "cpp", "dart", "rust", "go"}

	for _, format := range formats {
		outputFormatter, err := getOutputFormatter(format, "ns", "package", "varName", false)
		if outputFormatter == nil {
			t.Errorf("Unexpected nil output formatter for format %v", format)
		}
		if err != nil {
			t.Errorf("Unexpected error for format %v: %v", format, err)
		}
	}

	outputFormatter, err := getOutputFormatter("blah", "ns", "package", "varName", false)
	if outputFormatter != nil {
		t.Errorf("Unexpectedly got an output formatter.")
	}
	if err == nil {
		t.Errorf("Unexpectedly did not get an error.")
	}
}

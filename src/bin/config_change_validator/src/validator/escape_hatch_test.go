// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package validator

import (
	"testing"
)

var compatTests = []struct {
	name string
	msg  string
	skip bool
}{
	{"Normal", "A Commit message body", false},
	{"EscapeHatch(1)", "COBALT_BACKWARDS_INCOMPATIBLE_CHANGE_DATA_CORRUPTION_POSSIBLE=1", true},
	{"EscapeHatch(yes)", "COBALT_BACKWARDS_INCOMPATIBLE_CHANGE_DATA_CORRUPTION_POSSIBLE= yes", true},
	{"EscapeHatch(true)", "COBALT_BACKWARDS_INCOMPATIBLE_CHANGE_DATA_CORRUPTION_POSSIBLE =true", true},
	{"EscapeHatch(ok)", "COBALT_BACKWARDS_INCOMPATIBLE_CHANGE_DATA_CORRUPTION_POSSIBLE  =  ok", true},
	{"NotOnItsOwnLine", ".. COBALT_BACKWARDS_INCOMPATIBLE_CHANGE_DATA_CORRUPTION_POSSIBLE  =  ok ..", false},
	{"MultilineNoEscape", "Line 1\nLine 2\nLine 3", false},
	{"MultilineEscape", "Line 1\nLine 2\nLine 3\nCOBALT_BACKWARDS_INCOMPATIBLE_CHANGE_DATA_CORRUPTION_POSSIBLE=1", true},
}

func TestShouldSkipCompatibilityChecks(t *testing.T) {
	for _, tt := range compatTests {
		t.Run(tt.name, func(t *testing.T) {
			r := ShouldSkipCompatibilityChecks(tt.msg)
			if r != tt.skip {
				t.Errorf("Got %v, want %v", r, tt.skip)
			}
		})
	}
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package validator

import (
	"regexp"

	_ "github.com/golang/glog"
)

func ShouldSkipCompatibilityChecks(commitMsg string) bool {
	matches, _ := regexp.MatchString(`(?m:^COBALT_BACKWARDS_INCOMPATIBLE_CHANGE_DATA_CORRUPTION_POSSIBLE *= *(1|yes|true|ok)$)`, commitMsg)
	return matches
}

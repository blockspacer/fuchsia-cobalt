// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a program that compares two binary encoded CobaltRegistry,
// and validates that the changes between them are backwards compatible. In
// general, this tool is designed to verify that changes are okay to be merged
// into master.

package main

import (
	"config"
	"flag"
	"io/ioutil"
	"validator"

	"github.com/golang/glog"
	"github.com/golang/protobuf/proto"
)

var (
	// TODO(zmbush): Remove once these flags aren't used.
	oldCfg = flag.String("old_config", "", "Path of the old generated config. DEPRECATED")
	newCfg = flag.String("new_config", "", "Path of the new generated config. DEPRECATED")

	oldRegistry = flag.String("old_registry", "", "Path of the old metrics registry protobuf.")
	newRegistry = flag.String("new_registry", "", "Path of the new metrics registry protobuf.")
	commitMsg   = flag.String("commit_msg", "", "The commit message of the change that is being verified.")
)

func check(e error) {
	if e != nil {
		panic(e)
	}
}

func readRegistry(fname string) (out config.CobaltRegistry, err error) {
	data, err := ioutil.ReadFile(fname)
	if err != nil {
		return
	}

	err = proto.Unmarshal(data, &out)

	return out, err
}

func main() {
	flag.Parse()

	if *oldRegistry == "" && *oldCfg != "" {
		*oldRegistry = *oldCfg
	}

	if *newRegistry == "" && *newCfg != "" {
		*newRegistry = *newCfg
	}

	if (*oldRegistry == "") || (*newRegistry == "") {
		glog.Exit("Both 'old_registry' and 'new_registry' are required")
	}

	o, err := readRegistry(*oldRegistry)
	check(err)

	n, err := readRegistry(*newRegistry)
	check(err)

	check(validator.CompareRegistries(&o, &n, validator.ShouldSkipCompatibilityChecks(*commitMsg)))
}

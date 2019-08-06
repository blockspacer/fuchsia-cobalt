// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a program that reads cobalt configuration in a YAML format
// and outputs it as a CobaltRegistry serialized protocol buffer.

package main

import (
	"config"
	"config_parser"
	"config_validator"
	"flag"
	"os"
	"source_generator"

	"github.com/golang/glog"
	"github.com/golang/protobuf/proto"
)

var (
	skipValidation = flag.Bool("skip_validation", false, "Skip validating the config, write it no matter what.")
	depFile        = flag.String("dep_file", "", "Generate a depfile (see gn documentation) that lists all the project configuration files. Requires -output_file and -config_dir.")
	forClient      = flag.Bool("for_client", false, "Filters out the hide_on_client tagged fields")
)

func main() {
	flag.Parse()

	// If the location of a depfile is specified, output the depfile.
	if *depFile != "" {
		configFiles, err := config_parser.GetConfigFilesListFromFlags()
		if err != nil {
			glog.Exit(err)
		}
		if err := source_generator.WriteDepFileFromFlags(configFiles, *depFile); err != nil {
			glog.Exit(err)
		}
	}

	// First, we parse the configuration from the specified location.
	configs, err := config_parser.ParseConfigFromFlags()
	if err != nil {
		glog.Exit(err)
	}

	// Unless otherwise specified, validate the registry.
	if !*skipValidation {
		for _, c := range configs {
			if err = config_validator.ValidateProjectConfig(&c); err != nil {
				glog.Exit(err)
			}
		}
	}

	// Merge the list of project configs in a single config.CobaltRegistry.
	c := config_parser.MergeConfigs(configs)
	filtered := proto.Clone(&c).(*config.CobaltRegistry)

	// Filter the fields that are not needed on the client.
	if *forClient {
		config_parser.FilterHideOnClient(filtered)
	}

	// Write the registry depending upon the specified flags.
	if err := source_generator.WriteConfigFromFlags(&c, filtered); err != nil {
		glog.Exit(err)
	}

	os.Exit(0)
}

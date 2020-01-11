// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains flag definitions for the config_parser package as well
// as all the functions which make direct use of these flags.
// TODO(azani): Refactor and test. This is half of the logic that was ripped out
// config_parser_main.go and it still needs to be refactored.
package config_parser

import (
	"flag"
	"fmt"
	"time"
)

var (
	repoUrls      flagList
	configDirs    flagList
	configFile    = flag.String("config_file", "", "File containing the config for a single project. Exactly one of 'repo_url', 'config_file' or 'config_dir' must be specified.")
	customerId    = flag.Int64("customer_id", -1, "Customer Id for the config to be read. Must be set if and only if 'config_file' is set.")
	projectId     = flag.Int64("project_id", -1, "Project Id for the config to be read. Must be set if and only if 'config_file' is set.")
	v1Project     = flag.Bool("v1_project", false, "Specified project is a Cobalt 1.0 project. Can be set if and only if 'config_file' is set.")
	gitTimeoutSec = flag.Int64("git_timeout", 60, "How many seconds should I wait on git commands?")
)

type flagList []string

func (f *flagList) String() string {
	return fmt.Sprintf("%q", *f)
}

func (f *flagList) Set(value string) error {
	*f = append(*f, value)
	return nil
}

// Initialization function that is run before any main().
func init() {
	flag.Var(&repoUrls, "repo_url", "URL of the repository containing the config, can be specified multiple times. Exactly one of 'repo_url', 'config_file' or 'config_dir' must be specified.")
	flag.Var(&configDirs, "config_dir", "Directory containing the config, can be specified multiple times. Exactly one of 'repo_url', 'config_file' or 'config_dir' must be specified.")
}

// checkFlags verifies that the specified flags are compatible with each other.
func checkFlags() error {
	if (len(repoUrls) == 0) == (len(configDirs) == 0) == (*configFile == "") {
		return fmt.Errorf("Exactly one of 'repo_url', 'config_file' and 'config_dir' must be set.")
	}

	if *configFile == "" && len(configDirs) == 0 && (*customerId >= 0 || *projectId >= 0) {
		return fmt.Errorf("'customer_id' and 'project_id' must be set if and only if 'config_file' or 'config_dir' are set.")
	}

	if *v1Project && *configFile == "" {
		return fmt.Errorf("'v1_project' can be set if and only if 'config_file' is set.")
	}

	if *configFile != "" && (*customerId < 0 || *projectId < 0) {
		return fmt.Errorf("If 'config_file' is set, both 'customer_id' and 'project_id' must be set.")
	}

	return nil
}

// ParseConfigFromFlags uses the specified flags to find the specified registry,
// read and parse it.
func ParseConfigFromFlags() ([]ProjectConfig, error) {
	if err := checkFlags(); err != nil {
		return nil, err
	}

	configs := []ProjectConfig{}
	var pc ProjectConfig
	var err error
	if len(repoUrls) != 0 {
		gitTimeout := time.Duration(*gitTimeoutSec) * time.Second
		var repoConfigs []ProjectConfig
		for _, repoUrl := range repoUrls {
			repoConfigs, err = ReadConfigFromRepo(repoUrl, gitTimeout)
			configs = append(configs, repoConfigs...)
		}
	} else if *configFile != "" {
		version := CobaltVersion0
		if *v1Project {
			version = CobaltVersion1
		}
		pc, err = ReadConfigFromYaml(*configFile, uint32(*customerId), uint32(*projectId), CobaltVersion(version))
		configs = append(configs, pc)
	} else if *customerId >= 0 && *projectId >= 0 {
		for _, configDir := range configDirs {
			pc, err = ReadProjectConfigFromDir(configDir, uint32(*customerId), uint32(*projectId))
			configs = append(configs, pc)
		}
	} else {
		var dirConfigs []ProjectConfig
		for _, configDir := range configDirs {
			dirConfigs, err = ReadConfigFromDir(configDir)
			configs = append(configs, dirConfigs...)
		}
	}
	return configs, err
}

// GetConfigFilesListFromFlags returns a list of all the files that comprise
// the registry being parsed. This can be used to generate a depfile.
func GetConfigFilesListFromFlags() ([]string, error) {
	if err := checkFlags(); err != nil {
		return nil, err
	}
	if *configFile != "" {
		return []string{*configFile}, nil
	} else if len(configDirs) != 0 {
		files := []string{}
		var dirFiles []string
		var err error
		for _, configDir := range configDirs {
			dirFiles, err = GetConfigFilesListFromConfigDir(configDir)
			files = append(files, dirFiles...)
		}
		return files, err
	}
	return nil, fmt.Errorf("-dep_file requires -config_dir or -config_file")
}

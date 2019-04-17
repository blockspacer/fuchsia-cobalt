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
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"source_generator"
	"strings"
	"time"

	"github.com/golang/glog"
	"github.com/golang/protobuf/proto"
)

var (
	repoUrl        = flag.String("repo_url", "", "URL of the repository containing the config. Exactly one of 'repo_url', 'config_file' or 'config_dir' must be specified.")
	configDir      = flag.String("config_dir", "", "Directory containing the config. Exactly one of 'repo_url', 'config_file' or 'config_dir' must be specified.")
	configFile     = flag.String("config_file", "", "File containing the config for a single project. Exactly one of 'repo_url', 'config_file' or 'config_dir' must be specified.")
	outFile        = flag.String("output_file", "", "File to which the serialized config should be written. Defaults to stdout. When multiple output formats are specified, it will append the format to the filename")
	outFilename    = flag.String("out_filename", "", "The base name to use for writing files. Should not be used with output_file.")
	outDir         = flag.String("out_dir", "", "The directory into which files should be written.")
	addFileSuffix  = flag.Bool("add_file_suffix", false, "Append the out_format to the out_file, even if there is only one out_format specified")
	checkOnly      = flag.Bool("check_only", false, "Only check that the configuration is valid.")
	skipValidation = flag.Bool("skip_validation", false, "Skip validating the config, write it no matter what.")
	gitTimeoutSec  = flag.Int64("git_timeout", 60, "How many seconds should I wait on git commands?")
	customerId     = flag.Int64("customer_id", -1, "Customer Id for the config to be read. Must be set if and only if 'config_file' is set.")
	projectId      = flag.Int64("project_id", -1, "Project Id for the config to be read. Must be set if and only if 'config_file' is set.")
	v1Project      = flag.Bool("v1_project", false, "Specified project is a Cobalt 1.0 project. Can be set if and only if 'config_file' is set.")
	projectName    = flag.String("project_name", "", "Project name for the config to be read. Must be set if and only if 'config_dir' is set.")
	outFormat      = flag.String("out_format", "bin", "Specifies the output formats (separated by ' '). Supports 'bin' (serialized proto), 'b64' (serialized proto to base 64), 'cpp' (a C++ file containing a variable with a base64-encoded serialized proto.) 'dart' (a Dart library), and 'rust' (a rust crate)")
	varName        = flag.String("var_name", "config", "When using the 'cpp' or 'dart' output format, this will specify the variable name to be used in the output.")
	forTesting     = flag.Bool("for_testing", false, "Generates a constant for each report ID. Report names should be unique in the registry.")
	namespace      = flag.String("namespace", "", "When using the 'cpp' or 'rust' output format, this will specify the period-separated namespace within which the config variable must be placed.")
	depFile        = flag.String("dep_file", "", "Generate a depfile (see gn documentation) that lists all the project configuration files. Requires -output_file and -config_dir.")
	forClient      = flag.Bool("for_client", false, "Filters out the hide_on_client tagged fields")
	dartOutDir     = flag.String("dart_out_dir", "", "The directory to write dart files to (if different from out_dir)")
)

func generateFilename(format string) string {
	if *outFilename != "" {
		dir := *outDir
		if format == "dart" && *dartOutDir != "" {
			dir = *dartOutDir
		}
		fnameBase := fmt.Sprintf("%s/%s", dir, *outFilename)
		switch format {
		case "bin":
			return fmt.Sprintf("%s.pb", fnameBase)
		case "cpp":
			return fmt.Sprintf("%s.cb.h", fnameBase)
		case "rust":
			return fmt.Sprintf("%s.rs", fnameBase)
		default:
			return fmt.Sprintf("%s.%s", fnameBase, format)
		}
	} else {
		return *outFile
	}
}

// Write a depfile listing the files in 'files' at the location specified by
// outFile.
func writeDepFile(formats, files []string, depFile string) error {
	w, err := os.Create(depFile)
	if err != nil {
		return err
	}
	defer w.Close()

	// Since all targets share the same dependencies, we only need to output one.
	if len(formats) > 0 {
		_, err = io.WriteString(w, fmt.Sprintf("%s: %s\n", generateFilename(formats[0]), strings.Join(files, " ")))
	}

	return err
}

func main() {
	flag.Parse()

	if (*repoUrl == "") == (*configDir == "") == (*configFile == "") {
		glog.Exit("Exactly one of 'repo_url', 'config_file' and 'config_dir' must be set.")
	}

	if *projectId >= 0 && *projectName != "" {
		glog.Exit("Exactly one of 'project_id' and 'project_name' must be set.")
	}

	if *configFile == "" && *configDir == "" && (*customerId >= 0 || *projectId >= 0 || *projectName != "") {
		glog.Exit("'customer_id' and 'project_(id/name)'  must be set if and only if 'config_file' or 'config_dir' are set.")
	}

	if *v1Project && *configFile == "" {
		glog.Exit("'v1_project' can be set if and only if 'config_file' is set.")
	}

	if *configFile != "" && (*customerId < 0 || *projectId < 0) {
		glog.Exit("If 'config_file' is set, both 'customer_id' and 'project_id' must be set.")
	}

	if *outFile != "" && *checkOnly {
		glog.Exit("'output_file' does not make sense if 'check_only' is set.")
	}

	if *depFile != "" && (*configDir == "" && *configFile == "") {
		glog.Exit("-dep_file requires -config_dir or -config_file")
	}

	if *depFile != "" && (*outFile == "" && *outFilename == "") {
		glog.Exit("-dep_file requires -output_file or -out_filename")
	}

	if *outFile != "" && *outFilename != "" {
		glog.Exit("-output_file and -out_filename are mutually exclusive.")
	}

	if (*outDir != "" || *dartOutDir != "") && *outFile != "" {
		glog.Exit("-output_file should not be set at the same time as -out_dir or -dart_out_dir")
	}

	if (*outDir != "" || *dartOutDir != "") && *outFilename == "" {
		glog.Exit("-out_dir or -dart_out_dir require specifying -out_filename.")
	}

	var configLocation string
	if *repoUrl != "" {
		configLocation = *repoUrl
	} else if *configFile != "" {
		configLocation = *configFile
	} else {
		configLocation = *configDir
	}

	outFormats := strings.FieldsFunc(*outFormat, func(c rune) bool { return c == ' ' })

	if *depFile != "" {
		var configFiles []string
		if *configFile != "" {
			configFiles = append(configFiles, *configFile)
		} else if *configDir != "" {
			var err error
			configFiles, err = config_parser.GetConfigFilesListFromConfigDir(*configDir)
			if err != nil {
				glog.Exit(err)
			}
		}
		if err := writeDepFile(outFormats, configFiles, *depFile); err != nil {
			glog.Exit(err)
		}
	}

	// First, we parse the configuration from the specified location.
	configs := []config_parser.ProjectConfig{}
	var pc config_parser.ProjectConfig
	var err error
	if *repoUrl != "" {
		gitTimeout := time.Duration(*gitTimeoutSec) * time.Second
		configs, err = config_parser.ReadConfigFromRepo(*repoUrl, gitTimeout)
	} else if *configFile != "" {
		version := config_parser.CobaltVersion0
		if *v1Project {
			version = config_parser.CobaltVersion1
		}
		pc, err = config_parser.ReadConfigFromYaml(*configFile, uint32(*customerId), uint32(*projectId), config_parser.CobaltVersion(version))
		configs = append(configs, pc)
	} else if *customerId >= 0 && *projectId >= 0 {
		pc, err = config_parser.ReadProjectConfigFromDir(*configDir, uint32(*customerId), uint32(*projectId))
		configs = append(configs, pc)
	} else if *customerId >= 0 && *projectName != "" {
		pc, err = config_parser.ReadProjectConfigFromDirByName(*configDir, uint32(*customerId), *projectName)
		configs = append(configs, pc)
	} else {
		configs, err = config_parser.ReadConfigFromDir(*configDir)
	}

	if err != nil {
		glog.Exit(err)
	}

	if !*skipValidation {
		for _, c := range configs {
			if err = config_validator.ValidateProjectConfig(&c); err != nil {
				glog.Exit(err)
			}
		}
	}

	c := config_parser.MergeConfigs(configs)
	filtered := proto.Clone(&c).(*config.CobaltRegistry)

	if *forClient {
		config_parser.FilterHideOnClient(filtered)
	}

	for _, format := range outFormats {
		var outputFormatter source_generator.OutputFormatter
		switch format {
		case "bin":
			outputFormatter = source_generator.BinaryOutput
		case "b64":
			outputFormatter = source_generator.Base64Output
		case "cpp":
			namespaceList := []string{}
			if *namespace != "" {
				namespaceList = strings.Split(*namespace, ".")
			}
			outputFormatter = source_generator.CppOutputFactory(*varName, namespaceList, *forTesting)
		case "dart":
			if len(configs) > 1 {
				glog.Exitf("Dart output can only be used with a single project config.")
			}
			outputFormatter = source_generator.DartOutputFactory(*varName, *forTesting)
		case "rust":
			if len(configs) > 1 {
				glog.Exitf("Rust output can only be used with a single project config.")
			}
			namespaceList := []string{}
			if *namespace != "" {
				namespaceList = strings.Split(*namespace, ".")
			}
			outputFormatter = source_generator.RustOutputFactory(*varName, namespaceList, *forTesting)
		default:
			glog.Exitf("'%v' is an invalid out_format parameter. 'bin', 'b64', 'cpp', 'dart', and 'rust' are the only valid values for out_format.", *outFormat)
		}

		// Then, we serialize the configuration.
		configBytes, err := outputFormatter(&c, filtered)
		if err != nil {
			glog.Exit(err)
		}

		// Check that the output file is not empty.
		if len(configBytes) == 0 {
			glog.Exit("Output file is empty.")
		}

		// If no errors have occured yet and checkOnly was set, we are done.
		if *checkOnly {
			fmt.Printf("%s OK\n", configLocation)
			os.Exit(0)
		}

		// By default we print the output to stdout.
		w := os.Stdout

		// If an output file is specified, we write to a temporary file and then rename
		// the temporary file with the specified output file name.
		if *outFile != "" || *outFilename != "" {
			if w, err = ioutil.TempFile("", "cobalt_config"); err != nil {
				glog.Exit(err)
			}
			defer w.Close()
		}

		_, err = w.Write(configBytes)
		if err != nil {
			glog.Exit(err)
		}

		fname := generateFilename(format)

		if fname != "" {
			if err := os.Rename(w.Name(), fname); err != nil {
				// Rename doesn't work if /tmp is in a different partition. Attempting to copy.
				// TODO(azani): Look into doing this atomically.
				in, err := os.Open(w.Name())
				if err != nil {
					glog.Exit(err)
				}
				defer in.Close()

				out, err := os.Create(fname)
				if err != nil {
					glog.Exit(err)
				}
				defer out.Close()

				_, err = io.Copy(out, in)
				if err != nil {
					glog.Exit(err)
				}
			}
		}
	}

	os.Exit(0)
}

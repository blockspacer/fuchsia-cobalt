// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains flag definitions for the source_generator package as well
// as all the functions which make direct use of these flags.
package source_generator

import (
	"config"
	"flag"
	"fmt"
	"os"
)

var (
	addFileSuffix = flag.Bool("add_file_suffix", false, "Append the out_format to the out_file, even if there is only one out_format specified")
	outFile       = flag.String("output_file", "", "File to which the serialized config should be written. Defaults to stdout. When multiple output formats are specified, it will append the format to the filename")
	outFilename   = flag.String("out_filename", "", "The base name to use for writing files. Should not be used with output_file.")
	outDir        = flag.String("out_dir", "", "The directory into which files should be written.")
	outFormat     = flag.String("out_format", "bin", "Specifies the output formats (separated by ' '). Supports 'bin' (serialized proto), 'b64' (serialized proto to base 64), 'cpp' (a C++ file containing a variable with a base64-encoded serialized proto.) 'dart' (a Dart library), and 'rust' (a rust crate)")
	forTesting    = flag.Bool("for_testing", false, "Generates a constant for each report ID. Report names should be unique in the registry.")
	namespace     = flag.String("namespace", "", "When using the 'cpp' or 'rust' output format, this will specify the period-separated namespace within which the config variable must be placed.")
	dartOutDir    = flag.String("dart_out_dir", "", "The directory to write dart files to (if different from out_dir)")
	varName       = flag.String("var_name", "config", "When using the 'cpp' or 'dart' output format, this will specify the variable name to be used in the output.")
	checkOnly     = flag.Bool("check_only", false, "Only check that the configuration is valid.")
)

// checkFlags verifies that the specified flags are compatible with each other.
func checkFlags() error {
	if *outFile != "" && *checkOnly {
		return fmt.Errorf("'output_file' does not make sense if 'check_only' is set.")
	}

	if *outFile != "" && *outFilename != "" {
		return fmt.Errorf("-output_file and -out_filename are mutually exclusive.")
	}

	if (*outDir != "" || *dartOutDir != "") && *outFile != "" {
		return fmt.Errorf("-output_file should not be set at the same time as -out_dir or -dart_out_dir")
	}

	if (*outDir != "" || *dartOutDir != "") && *outFilename == "" {
		return fmt.Errorf("-out_dir or -dart_out_dir require specifying -out_filename.")
	}
	return nil
}

func filenameGeneratorFromFlags() func(string) string {
	return getFilenameGenerator(*outFile, *outFilename, *outDir, *dartOutDir)
}

// WriteDepFileFromFlags writes a depfile to the location specified in depFile.
// files must be a list of the reigstry input files.
func WriteDepFileFromFlags(files []string, depFile string) error {
	if *outFile == "" && *outFilename == "" {
		return fmt.Errorf("-dep_file requires -output_file or -out_filename")
	}

	w, err := os.Create(depFile)
	if err != nil {
		return err
	}
	defer w.Close()

	return writeDepFile(parseOutFormatList(*outFormat), files, filenameGeneratorFromFlags(), w)
}

// WriteConfigFromFlags writes the specified CobaltRegistry according to the
// flags specified above.
func WriteConfigFromFlags(c, filtered *config.CobaltRegistry) error {
	checkFlags()
	generateFilename := filenameGeneratorFromFlags()
	for _, format := range parseOutFormatList(*outFormat) {
		outputFormatter, err := getOutputFormatter(format, *namespace, *varName, *forTesting)
		if err != nil {
			return err
		}

		// Then, we serialize the configuration.
		configBytes, err := outputFormatter(c, filtered)
		if err != nil {
			return err
		}

		// Check that the output file is not empty.
		if len(configBytes) == 0 {
			return fmt.Errorf("Output file is empty.")
		}

		// If no errors have occured yet and checkOnly was set, we don't need to write anything.
		if *checkOnly {
			continue
		}

		// By default we print the output to stdout.
		w := os.Stdout

		if *outFile != "" || *outFilename != "" {
			fname := generateFilename(format)
			w, err = os.Create(fname)
			if err != nil {
				return err
			}
		}

		if _, err := w.Write(configBytes); err != nil {
			return err
		}
	}
	if *checkOnly {
		fmt.Printf("OK\n")
	}
	return nil
}

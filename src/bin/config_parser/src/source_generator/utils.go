// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements utilities for the registry parser's output.
package source_generator

import (
	"fmt"
	"path/filepath"
	"strings"
)

// getFilenameGenerator returns a function which takes an output format and returns a file path.
func getFilenameGenerator(outFile, outFilename, outDir, dartOutDir string) func(string) string {
	if outFilename == "" {
		return func(_ string) string { return outFile }
	}

	return func(format string) string {
		dir := outDir
		if format == "dart" && dartOutDir != "" {
			dir = dartOutDir
		}
		fnameBase := filepath.Join(dir, outFilename)
		switch format {
		case "bin":
			return fmt.Sprintf("%s.pb", fnameBase)
		case "cpp":
			return fmt.Sprintf("%s.cb.h", fnameBase)
		case "rust":
			return fmt.Sprintf("%s.rs", fnameBase)
		case "json":
			return fmt.Sprintf("%s.json", fnameBase)
		default:
			return fmt.Sprintf("%s.%s", fnameBase, format)
		}
	}
}

// parseOutFormatList parses a space-separated list of output formats.
// TODO(azani): Switch to comma-separated.
func parseOutFormatList(outFormat string) []string {
	return strings.FieldsFunc(outFormat, func(c rune) bool { return c == ' ' })
}

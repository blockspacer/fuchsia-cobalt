// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements writing dep files.
package source_generator

import (
	"fmt"
	"io"
	"strings"
)

// Write a depfile listing the files in 'inputFiles' to the specified 'w' Writer.
func writeDepFile(formats, inputFiles []string, generateFilename func(string) string, w io.Writer) error {
	// Since all targets share the same dependencies, we only need to output one.
	// TODO(azani): Generate one line per output file since different builds might need different generated files.
	if len(formats) == 0 {
		return nil
	}
	_, err := io.WriteString(w, fmt.Sprintf("%s: %s\n", generateFilename(formats[0]), strings.Join(inputFiles, " ")))

	return err
}

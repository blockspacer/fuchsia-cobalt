// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source_generator

import (
	"bytes"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestWriteDepFile(t *testing.T) {
	formats := []string{"dart", "cpp", "rust"}
	registryFiles := []string{"sysmetrics/config.yaml", "ledger/config.yaml"}
	generateFilename := getFilenameGenerator("", "basename", "out_dir", "dart_out_dir")
	expected := "dart_out_dir/basename.dart: sysmetrics/config.yaml ledger/config.yaml\n"
	buf := bytes.Buffer{}
	if err := writeDepFile(formats, registryFiles, generateFilename, &buf); err != nil {
		t.Errorf("Unexpected error: %v", err)
	}

	actual := buf.String()
	if actual != expected {
		t.Errorf("Unexpected diff: %v", cmp.Diff(expected, actual))
	}
}

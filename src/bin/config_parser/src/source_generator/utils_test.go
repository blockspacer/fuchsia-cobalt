// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source_generator

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestParseOutFormatList(t *testing.T) {
	testCases := []struct {
		input    string
		expected []string
	}{
		{
			input:    "",
			expected: []string{},
		},
		{
			input:    "rust",
			expected: []string{"rust"},
		},
		{
			input:    "rust cpp",
			expected: []string{"rust", "cpp"},
		},
		{
			input:    "rust ",
			expected: []string{"rust"},
		},
	}
	for _, tc := range testCases {
		if diff := cmp.Diff(parseOutFormatList(tc.input), tc.expected); diff != "" {
			t.Errorf("%s", diff)
		}
	}
}

func TestGenerateFilename(t *testing.T) {
	testCases := []struct {
		outFile     string
		outFilename string
		outDir      string
		dartOutDir  string
		format      string
		expected    string
	}{
		{
			outFilename: "base",
			format:      "cpp",
			expected:    "base.cb.h",
		},
		{
			outFilename: "base",
			format:      "bin",
			expected:    "base.pb",
		},
		{
			outFilename: "base",
			format:      "rust",
			expected:    "base.rs",
		},
		{
			outFilename: "base",
			format:      "dart",
			expected:    "base.dart",
		},
		{
			outFilename: "base",
			outDir:      "out_dir",
			dartOutDir:  "should be ignored",
			format:      "cpp",
			expected:    "out_dir/base.cb.h",
		},
		{
			outFilename: "base",
			outDir:      "should be ignored",
			dartOutDir:  "dart_dir",
			format:      "dart",
			expected:    "dart_dir/base.dart",
		},
	}

	for _, tc := range testCases {
		actual := getFilenameGenerator(tc.outFile, tc.outFilename, tc.outDir, tc.dartOutDir)(tc.format)
		if actual != tc.expected {
			t.Errorf("getFilenameGenerator(%q, %q, %q, %q)(%q) = %q, expected %q",
				tc.outFile, tc.outFilename, tc.outDir, tc.dartOutDir, tc.format, actual, tc.expected)
		}
	}
}

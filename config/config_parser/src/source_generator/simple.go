// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements the simple output formats (binary and base64).

package source_generator

import (
	"config"
	"encoding/base64"

	"github.com/golang/protobuf/proto"
)

// Outputs the serialized proto.
func BinaryOutput(c *config.CobaltConfig) (outputBytes []byte, err error) {
	buf := proto.Buffer{}
	buf.SetDeterministic(true)
	if err := buf.Marshal(c); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

// Outputs the serialized proto base64 encoded.
func Base64Output(c *config.CobaltConfig) (outputBytes []byte, err error) {
	configBytes, err := BinaryOutput(c)
	if err != nil {
		return outputBytes, err
	}
	encoder := base64.StdEncoding
	outLen := encoder.EncodedLen(len(configBytes))

	outputBytes = make([]byte, outLen, outLen)
	encoder.Encode(outputBytes, configBytes)
	return outputBytes, nil
}

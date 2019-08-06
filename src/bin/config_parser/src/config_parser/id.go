// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the code that computes ids from names.
//
// We use the Fowler-Noll-Vo hash function

package config_parser

import (
	"hash/fnv"
)

func IdFromName(name string) uint32 {
	hash := fnv.New32()
	hash.Write([]byte(name))
	return hash.Sum32()
}

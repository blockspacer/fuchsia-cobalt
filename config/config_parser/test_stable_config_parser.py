#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Checks that the output of the config parser is stable across runs."""

import os
import subprocess
import sys

THIS_DIR = os.path.abspath(os.path.dirname(__file__))
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir, os.pardir))
OUT_DIR = os.path.abspath(os.path.join(SRC_ROOT_DIR, 'out'))
CONFIG_PARSER_BIN = os.path.join(
    OUT_DIR, 'config', 'config_parser', 'config_parser')
CONFIG_DIR = os.path.join(SRC_ROOT_DIR, 'third_party', 'cobalt_config')

def main():
  cmd = [CONFIG_PARSER_BIN, '-config_dir', CONFIG_DIR, '-out_format', 'b64']
  out1 = subprocess.check_output(cmd)
  out2 = subprocess.check_output(cmd)
  if out1 != out2:
    raise Exception('Two calls to the config_parser yielded different outputs!')
  print("PASS")
  return 0

if __name__ == '__main__':
  sys.exit(main())

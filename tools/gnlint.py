#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Runs gofmt on all of Cobalt's go files."""

from __future__ import print_function

import os
import subprocess

import get_files

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))


def main(only_directories=[], all_files=False):
  status = 0
  files_to_lint = get_files.files_to_lint(
      ('.gn', '.gni'), only_directories=only_directories, all_files=all_files)

  for f in files_to_lint:
    try:
      subprocess.check_call(['gn', 'format', '--dry-run', f])
    except:
      print('%s: Does not match expected format. Please run `gn format`' %
            os.path.relpath(f, SRC_ROOT_DIR))
      status += 1

  return status


if __name__ == '__main__':
  exit(main())

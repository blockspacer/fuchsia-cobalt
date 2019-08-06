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
import shutil
import subprocess
import sys

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))

GO_DIRS = [
    os.path.join(SRC_ROOT_DIR, 'config/config_parser'),
]


def main():
  status = 0
  for dir_path in GO_DIRS:
    print('Linting go files in %s' % dir_path)
    p = subprocess.Popen(['gofmt', '-l', dir_path], stdout=subprocess.PIPE)
    out = p.communicate()[0]

    if len(out) > 0:
      status += 1
      print('Errors found in:\n%s' % out)

  return status


if __name__ == '__main__':
  main()

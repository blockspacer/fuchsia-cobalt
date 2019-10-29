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

SKIP_LINT_DIRS = [
    os.path.join(SRC_ROOT_DIR, 'out'),
    os.path.join(SRC_ROOT_DIR, 'sysroot'),
    os.path.join(SRC_ROOT_DIR, 'third_party'),
    os.path.join(SRC_ROOT_DIR, 'src', 'bin', 'config_parser', 'src',
                 'source_generator', 'source_generator_test_files'),
]


# Given a directory's parent path and name, returns a boolean indicating whether
# or not the directory should be skipped for liniting.
def should_skip_dir(parent_path, name):
  if name.startswith('.'):
    return True
  full_path = os.path.join(parent_path, name)
  for p in SKIP_LINT_DIRS:
    if full_path.startswith(p):
      return True
  return False


def main():
  status = 0
  files_to_lint = []
  for root, dirs, files in os.walk(SRC_ROOT_DIR):
    for f in files:
      if f.endswith('.go'):
        files_to_lint.append(os.path.join(root, f))

    dirs_to_skip = [dir for dir in dirs if should_skip_dir(root, dir)]
    for d in dirs_to_skip:
      dirs.remove(d)

  print('Linting %d go files' % len(files_to_lint))
  p = subprocess.Popen(['gofmt', '-l'] + files_to_lint)
  p.wait()

  if p.returncode != 0:
    print('Received non-zero return code (%s)' % p.returncode)
    status += 1

  return status


if __name__ == '__main__':
  exit(main())

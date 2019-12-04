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

import subprocess

import get_files


def main(only_directories=[], all_files=False):
  status = 0
  files_to_lint = get_files.files_to_lint(
      ('.go',), only_directories=only_directories, all_files=all_files)

  print('Linting %d go files' % len(files_to_lint))
  if files_to_lint:
    p = subprocess.Popen(
        ['gofmt', '-d'] + files_to_lint, stdout=subprocess.PIPE)
    out, err = p.communicate()

    if p.returncode != 0:
      print('Received non-zero return code (%s)' % p.returncode)
      status += 1

    if len(out) > 0:
      print('The following differences were found with gofmt:\n%s' % out)
      status += 1

  return status


if __name__ == '__main__':
  exit(main())

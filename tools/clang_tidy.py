#!/usr/bin/env python

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Lints all of Cobalt's C++ files."""

from __future__ import print_function

import os
import subprocess
import re

import get_files

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))
OUT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir, 'out'))
CLANG_TIDY = os.path.join(SRC_ROOT_DIR, 'sysroot', 'share', 'clang',
                          'run-clang-tidy.py')
CHECK_HEADER_GUARDS = os.path.join(SRC_ROOT_DIR, 'tools', 'style',
                                   'check-header-guards.py')

TEST_FILE_REGEX = re.compile('.*_(unit)?tests?.cc$')
TEST_FILE_CLANG_TIDY_CHECKS = [
    '-readability-magic-numbers',
    '-misc-non-private-member-variables-in-classes'
]


def main(only_directories=[], all_files=False):
  status = 0

  clang_tidy_files = []
  clang_tidy_test_files = []

  files_to_lint = get_files.files_to_lint(
      ('.h', '.cc'), only_directories=only_directories, all_files=all_files)

  for full_path in files_to_lint:
    if TEST_FILE_REGEX.match(full_path):
      clang_tidy_test_files.append(full_path)
    else:
      clang_tidy_files.append(full_path)

  header_files = [f for f in files_to_lint
                  if f.endswith('.h') and not os.path.islink(f)]
  print('Running check-header-guards.py on %d files' % len(header_files))
  if len(header_files) > 0:
    try:
      subprocess.check_call([CHECK_HEADER_GUARDS] + header_files)
    except:
      status += 1

  clang_tidy_command = [CLANG_TIDY, '-quiet', '-p', OUT_DIR]
  print('Running clang-tidy on %d source files' % len(clang_tidy_files))
  if clang_tidy_files:
    try:
      subprocess.check_call(clang_tidy_command + clang_tidy_files)
    except:
      status += 1

  print('Running clang-tidy on %d test files' % len(clang_tidy_test_files))
  if clang_tidy_test_files:
    try:
      subprocess.check_call(
          clang_tidy_command +
          ['-checks=%s' % ','.join(TEST_FILE_CLANG_TIDY_CHECKS)] +
          clang_tidy_test_files)
    except:
      status += 1

  return status


if __name__ == '__main__':
  exit(main())

#!/usr/bin/env python

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Lints all of Cobalt's C++ files."""

from __future__ import print_function

import os
import shutil
import subprocess
import sys
import re

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))
OUT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir, 'out'))
CPP_LINT = os.path.join(SRC_ROOT_DIR, 'third_party', 'cpplint', 'cpplint.py')
CLANG_TIDY = os.path.join(SRC_ROOT_DIR, 'sysroot', 'share', 'clang',
                          'run-clang-tidy.py')

# A list of directories which should be skipped while walking the directory
# tree looking for C++ files to be linted. We also skip directories starting
# with a "." such as ".git"
SKIP_LINT_DIRS = [
    os.path.join(SRC_ROOT_DIR, 'out'),
    os.path.join(SRC_ROOT_DIR, 'sysroot'),
    os.path.join(SRC_ROOT_DIR, 'third_party'),
]

CLANG_TIDY_BLACKLIST = [
    # TODO(pesk): Clean up the magic numbers in these test files.
    os.path.join(SRC_ROOT_DIR, 'logger', 'event_aggregator_test.cc'),
    os.path.join(SRC_ROOT_DIR, 'logger', 'logger_test.cc'),
]

TEST_FILE_REGEX = re.compile('.*_(unit)?tests?.cc$')
TEST_FILE_CLANG_TIDY_CHECKS = ['-readability-magic-numbers']


def clang_tidy_blacklisted(path):
  for blacklist in CLANG_TIDY_BLACKLIST:
    if path.startswith(blacklist):
      return True
  return False


def use_clang_tidy(path):
  return not clang_tidy_blacklisted(path)


def main(only_directories=[]):
  status = 0

  clang_tidy_files = []
  clang_tidy_test_files = []

  only_directories = [os.path.join(SRC_ROOT_DIR, d) for d in only_directories]

  print('Running cpplint.py on non-blacklisted files')
  for root, dirs, files in os.walk(SRC_ROOT_DIR):
    for f in files:
      if f.endswith('.h') or f.endswith('.cc'):
        full_path = os.path.join(root, f)

        if only_directories and len(
            [d for d in only_directories if full_path.startswith(d)]) == 0:
          continue

        if use_clang_tidy(full_path):
          if TEST_FILE_REGEX.match(full_path):
            clang_tidy_test_files.append(full_path)
          else:
            clang_tidy_files.append(full_path)
          continue

        print('%s --root %s %s' % (CPP_LINT, SRC_ROOT_DIR, full_path))
        cmd = subprocess.Popen([
            CPP_LINT, '--filter', '-build/include_order', '--linelength', '100',
            '--root', SRC_ROOT_DIR, full_path
        ],
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
        out, err = cmd.communicate()

        if cmd.returncode:
          status += 1
          print('Error %s' % err)

    # Before recursing into directories remove the ones we want to skip.
    dirs_to_skip = [
        dir for dir in dirs
        if dir.startswith('.') or os.path.join(root, dir) in SKIP_LINT_DIRS
    ]
    for d in dirs_to_skip:
      dirs.remove(d)

  clang_tidy_command = [CLANG_TIDY, '-quiet', '-p', OUT_DIR]
  print('Running clang-tidy on %d source files' % len(clang_tidy_files))
  try:
    subprocess.check_call(clang_tidy_command + clang_tidy_files)
  except:
    status += 1

  print('Running clang-tidy on %d test files' % len(clang_tidy_test_files))
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

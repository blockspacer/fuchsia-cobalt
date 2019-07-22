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
    os.path.join(SRC_ROOT_DIR, 'config', 'config_parser', 'src',
                 'source_generator', 'source_generator_test_files'),
    os.path.join(SRC_ROOT_DIR, 'kubernetes'),
    os.path.join(SRC_ROOT_DIR, 'logger', 'test_registries'),
    os.path.join(SRC_ROOT_DIR, 'out'),
    os.path.join(SRC_ROOT_DIR, 'prototype'),
    os.path.join(SRC_ROOT_DIR, 'shuffler'),
    os.path.join(SRC_ROOT_DIR, 'sysroot'),
    os.path.join(SRC_ROOT_DIR, 'third_party'),
]

CLANG_TIDY_WHITELIST = [
    os.path.join(SRC_ROOT_DIR, 'algorithms'),
    os.path.join(SRC_ROOT_DIR, 'config'),
    os.path.join(SRC_ROOT_DIR, 'encoder'),
    os.path.join(SRC_ROOT_DIR, 'keys'),
    os.path.join(SRC_ROOT_DIR, 'manifest'),
    os.path.join(SRC_ROOT_DIR, 'meta'),
]


def use_clang_tidy(path):
  for whitelist in CLANG_TIDY_WHITELIST:
    if path.startswith(whitelist):
      return True
  return False


def main(only_directories=[]):
  status = 0

  clang_tidy_files = []

  only_directories = [os.path.join(SRC_ROOT_DIR, d) for d in only_directories]

  for root, dirs, files in os.walk(SRC_ROOT_DIR):
    print('Linting c++ files in %s' % root)
    for f in files:
      if f.endswith('.h') or f.endswith('.cc'):
        full_path = os.path.join(root, f)

        if only_directories and len(
            [d for d in only_directories if full_path.startswith(d)]) == 0:
          continue

        if use_clang_tidy(full_path):
          clang_tidy_files.append(full_path)
          continue

        cmd = subprocess.Popen([CPP_LINT, '--root', SRC_ROOT_DIR, full_path],
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

  print('Running clang-tidy on %d source files' % len(clang_tidy_files))
  try:
    subprocess.check_call([CLANG_TIDY, '-quiet', '-p', OUT_DIR] +
                          clang_tidy_files)
    return status
  except:
    return status + 1


if __name__ == '__main__':
  exit(main())

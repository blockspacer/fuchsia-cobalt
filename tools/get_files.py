#!/usr/bin/env python

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Finds files to lint amongst all of Cobalt's files."""

from __future__ import print_function

import os
import subprocess
import sys
import re

try:
  from subprocess import DEVNULL # py3k
except ImportError:
  DEVNULL = open(os.devnull, 'wb')

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))

# A list of directories which should be skipped while walking the directory
# tree looking for C++ files to be linted. We also skip directories starting
# with a "." such as ".git"
SKIP_LINT_DIRS = [
    os.path.join(SRC_ROOT_DIR, 'out'),
    os.path.join(SRC_ROOT_DIR, 'sysroot'),
    os.path.join(SRC_ROOT_DIR, 'third_party'),
    os.path.join(SRC_ROOT_DIR, 'build'),
    os.path.join(SRC_ROOT_DIR, 'src', 'bin', 'config_parser', 'src',
                 'source_generator', 'source_generator_test_files'),
]

# In version 3.6 and newer, the subprocess.check_output() function needs to be
# passed an encoding type to return a string, otherwise it returns a bytes type.
def _run_command(*command):
  if sys.version_info >= (3, 6):
    return subprocess.check_output(command, stderr=DEVNULL, encoding="UTF-8")
  else:
    return subprocess.check_output(command, stderr=DEVNULL)


# Given a directory's parent path and name, returns a boolean indicating whether
# or not the directory should be skipped for linting.
def _should_skip_dir(parent_path, name):
  if name.startswith('.'):
    return True
  full_path = os.path.join(parent_path, name)
  for p in SKIP_LINT_DIRS:
    if full_path.startswith(p):
      return True
  return False


# Find the newest parent commit in the upstream branch (or HEAD if no such
# commit is found).
def _get_diff_base():
  try:
    upstream = _run_command(
        'git', 'rev-parse', '--abbrev-ref', '--symbolic-full-name', '"@{u}"').strip()
  except subprocess.CalledProcessError:
    upstream = 'origin/master'
  local_commit = _run_command(
      'git', 'rev-list', 'HEAD', '^' + upstream).strip().split('\n')[-1]
  if not local_commit:
    return 'HEAD'
  return _run_command('git', 'rev-parse', local_commit + '^').strip()


# Get the source files that have been modified in this branch. By default this
# uses `git diff` against the newest parent commit in the upstream branch (or
# against HEAD if no such commit is found).
def _get_git_files():
  diff_base = _get_diff_base()
  return _run_command('git', 'diff', '--name-only', diff_base).strip().split('\n')


def files_to_lint(extensions, only_directories=[], all_files=False):
  files_to_lint = []
  only_directories = [os.path.join(SRC_ROOT_DIR, d) for d in only_directories]
  git_files = []
  if not all_files:
    git_files = [os.path.join(SRC_ROOT_DIR, f) for f in _get_git_files()]

  for root, dirs, files in os.walk(SRC_ROOT_DIR):
    for f in files:
      if f.endswith(extensions):
        full_path = os.path.join(root, f)

        if only_directories and len(
            [d for d in only_directories if full_path.startswith(d)]) == 0:
          continue

        if not all_files and full_path not in git_files:
          continue

        files_to_lint.append(full_path)

    # Before recursing into directories remove the ones we want to skip.
    dirs_to_skip = [dir for dir in dirs if _should_skip_dir(root, dir)]
    for d in dirs_to_skip:
      dirs.remove(d)

  return files_to_lint

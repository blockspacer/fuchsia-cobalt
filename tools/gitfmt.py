#!/usr/bin/env python

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import print_function

import os
import os.path
import subprocess
import sys

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))


# In version 3.6 and newer, the subprocess.check_output() function needs to be
# passed an encoding type to return a string, otherwise it returns a bytes type.
def _run_command(command):
  if sys.version_info >= (3, 6):
    return subprocess.check_output(command, encoding="UTF-8")
  else:
    return subprocess.check_output(command)


class Formatter(object):
  "Formatter formats files of a particular type."

  def __init__(self, supported_extensions, cmd):
    """Creates a Formatter.

    supported_extensions is a list of extensions supported by this Formatter.
    cmd is the command used to format files with the file path appended.
    """
    self._ext = supported_extensions
    self._cmd = cmd
    self._checked = False

  def is_supported(self, filepath):
    "True if the specified filepath has a supported extension."
    _, ext = os.path.splitext(filepath)
    return ext in self._ext

  def format(self, abspath):
    """Try to format the file at the specified absolute path.

    Returns False in case of failure.
    True if success.
    """
    if not self._can_use_binary():
      return False
    cmd = self._cmd[:]
    cmd.append(abspath)
    print(" ".join(cmd))
    _run_command(cmd)
    return True

  def maybe_print_install_msg(self):
    """If the Formatter has been invoked but could not find the binary it

    needs, prompt the user to install the required binary.
    """
    # If _checked is set to True, that means the Formatter has been used at
    # least once.
    if not self._checked or self._can_use_bin:
      return
    print("Unable to format {} files. Try installing {}.".format(
        self._ext, self._cmd[0]))

  def _can_use_binary(self):
    "Check if the binary needed to run the formatter is available."
    # Check to see if this was cached.
    if self._checked:
      return self._can_use_bin
    self._checked = True

    # Check to see if the binary specified in the command can be executed.
    paths = os.getenv("PATH").split(":")
    binary = self._cmd[0]
    for path in paths:
      bin_path = os.path.join(path, binary)
      if os.path.exists(bin_path) and os.access(bin_path, os.X_OK):
        self._can_use_bin = True
        return True

    self._can_use_bin = False
    return False


class FormattingSession(object):
  """FormattingSession holds the information regarding the formatting of many

   files.
  """

  def __init__(self, root_dir):
    self._formatters = [
        Formatter([".h", ".cc", ".proto"],
                  ["clang-format", "-i", "-style=google"]),
        Formatter([".go"], ["gofmt", "-w"]),
        Formatter([".gn"], ["gn", "format", "--in-place"]),
        Formatter([".py"], ["pyformat", "-i"]),
    ]
    # _formatted is number of files that were succesfully formatted.
    self._formatted = 0
    # _failed is the number of files which we tried to format but could not.
    self._failed = 0
    # _skipped is the number of files which did not try to format either because
    # we don't know how to format them (unsupported format) or because they are
    # not tracked files.
    self._skipped = 0
    self._seen_filepaths = set()
    self._root_dir = root_dir

  def _format_file(self, filepath):
    "Attempt to format a file."
    # Skip duplicates.
    if filepath in self._seen_filepaths:
      return
    self._seen_filepaths.add(filepath)
    for formatter in self._formatters:
      if not formatter.is_supported(filepath):
        continue
      abspath = os.path.normpath(os.path.join(self._root_dir, filepath))
      if formatter.format(abspath):
        self._formatted += 1
      else:
        self._failed += 1
    self._skipped += 1

  def format_changed(self, changes):
    "Format the specified changed files. Leaves untracked files unchanged."
    files = set()
    for change in changes:
      op = change[0]
      filename = change[1]
      if filename in files:
        continue
      files.add(filename)
      # For renames, the new file name is the last element on the line.
      if "R" in op:
        filename = change[-1]

      # 'A' means added and 'M' means modified.
      if "A" in op or "M" in op:
        self._format_file(filename)
        continue
      self._skipped += 1

    for f in self._formatters:
      f.maybe_print_install_msg()
    print(("Formatted {} file(s), failed to format {} file(s) and skipped {}"
           " file(s).").format(self._formatted, self._failed, self._skipped))


def _git_status():
  "Get the files that are changed."
  status_str = _run_command(
      ["git", "status", "--ignored=no", "--ignore-submodules", "--porcelain"])
  return [l.split() for l in status_str.split("\n") if len(l.split()) >= 2]


def _git_show():
  "Get the files that changed in the last git commit."
  show_str = _run_command(
      ["git", "show", "--name-status", "--oneline", "--ignore-submodules"])
  lines = show_str.split("\n")
  return [l.split() for l in lines[1:] if len(l.split()) >= 2]


def _git_root_dir():
  "Get the root of the current git repository."
  root_dir = _run_command(["git", "rev-parse", "--show-toplevel"])
  return root_dir.strip()


def fmt(also_fmt_last_commit, repo_path=None):
  """Format changed files in a git repository.

  If also_fmt_last_commit is True, also format files changes in the latest
  commit.
  If repo_path is None, use the current git repository. If repo_path is not
  None, format files in that repository.
  """
  if not repo_path:
    repo_path = _git_root_dir()

  cwd = os.getcwd()
  repo_path = os.path.abspath(repo_path)
  try:
    os.chdir(repo_path)
    changes = _git_status()
    if also_fmt_last_commit:
      changes += _git_show()
    session = FormattingSession(repo_path)
    session.format_changed(changes)
  finally:
    os.chdir(repo_path)


if __name__ == "__main__":
  fmt(True)

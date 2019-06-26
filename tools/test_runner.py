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
"""Runs all executables (tests) in a directory"""

from __future__ import print_function

import logging
import os
import shutil
import subprocess
import sys
import time

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))
SYS_ROOT_DIR = os.path.join(SRC_ROOT_DIR, "sysroot")

_logger = logging.getLogger()


def run_all_tests(test_dir, verbose_count=0, vmodule=None, test_args=None):
  """ Runs the tests in the given directory.

  Optionally also starts various processes that may be needed by the tests.

  Args:
    test_dir {string}: Name of the directory under the "out" directory
      containing test executables to be run.
    vmodule: If this is a non-empty string it will be passed as the value of the
      -vmodule= flag to some of the processes. This flag is used to enable
      per-module verbose logging. See the gLog documentation. Currently we
      support this flag only for the AnalyzerService and the ReportMaster and so
      this flag is ignored unles start_cobatl_processes is True.
    test_args {list of strings}: These will be passed to each test executable.

  Returns:
    A list of strings indicating which tests failed. Returns None or to
    indicate success.
  """
  tdir = os.path.abspath(os.path.join(SRC_ROOT_DIR, "out", test_dir))

  if not os.path.exists(tdir):
    print("\n*************** ERROR ****************")
    print("Directory %s does not exist." % tdir)
    print("Run 'cobaltb.py build' first.")
    return ["Directory %s does not exist." % tdir]

  if test_args is None:
    test_args = []
  test_args.append("-logtostderr")
  if verbose_count > 0:
    test_args.append("-v=%d" % verbose_count)

  print("Running all tests in %s " % tdir)
  print("Test arguments: '%s'" % test_args)
  failure_list = []
  for test_executable in os.listdir(tdir):
    bt_emulator_process = None
    shuffler_process = None
    analyzer_service_process = None
    report_master_process = None
    path = os.path.abspath(os.path.join(tdir, test_executable))
    if os.path.isdir(path):
      continue
    if not os.access(path, os.X_OK):
      continue
    print("Running %s..." % test_executable)
    command = [path] + test_args
    return_code = subprocess.call(command)
    if return_code != 0:
      failure_list.append(test_executable)
    if return_code < 0:
      print("")
      print("****** WARNING Process [%s] terminated by signal %d" %
            (command[0], -return_code))
  if failure_list:
    return failure_list
  else:
    return None

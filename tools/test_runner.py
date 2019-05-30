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

import logging
import os
import shutil
import subprocess
import sys
import time

import tools.process_starter as process_starter

from tools.process_starter import LOCALHOST_TLS_CERT_FILE
from tools.process_starter import LOCALHOST_TLS_KEY_FILE

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))
SYS_ROOT_DIR = os.path.join(SRC_ROOT_DIR, "sysroot")
E2E_DIR = os.path.join(SRC_ROOT_DIR, "end_to_end_tests")
E2E_TEST_ANALYZER_PRIVATE_KEY_PEM = os.path.join(
    E2E_DIR, "analyzer_private_key.pem.e2e_test")
E2E_TEST_ANALYZER_PUBLIC_KEY_PEM = os.path.join(
    E2E_DIR, "analyzer_public_key.pem.e2e_test")
E2E_TEST_SHUFFLER_PRIVATE_KEY_PEM = os.path.join(
    E2E_DIR, "shuffler_private_key.pem.e2e_test")
E2E_TEST_SHUFFLER_PUBLIC_KEY_PEM = os.path.join(
    E2E_DIR, "shuffler_public_key.pem.e2e_test")

_logger = logging.getLogger()


def run_all_tests(test_dir,
                  start_bt_emulator=False,
                  start_cobalt_processes=False,
                  use_tls=False,
                  tls_cert_file=LOCALHOST_TLS_CERT_FILE,
                  tls_key_file=LOCALHOST_TLS_KEY_FILE,
                  bigtable_project_name="",
                  bigtable_instance_id="",
                  verbose_count=0,
                  vmodule=None,
                  test_args=None):
  """ Runs the tests in the given directory.

  Optionally also starts various processes that may be needed by the tests.

  Args:
    test_dir {string}: Name of the directory under the "out" directory
      containing test executables to be run.
    start_bt_emulator{ bool}: If True then an instance of the Cloud Bigtable
      Emulator will be started before each test and killed afterwards.
    start_cobalt_processes {bool}: If True then an instance of the Cobalt
      Shuffler, Analyzer Service and Report Master will be started before each
      test and killed afterwards.
    use_tls {bool}: This is ignored unless start_cobalt_process=True. In that
      case this flag will cause the processes (currently only the Shuffler and
      the ReportMaster, not the Analyzer) to use tls for gRPC communicaton with
      their client.
    tls_cert_file, tls_key_file: If use_tls is True and start_cobalt_process is
      True then these are the tls cert and key files to use when starting the
      local processes.
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
    print "\n*************** ERROR ****************"
    print "Directory %s does not exist." % tdir
    print "Run 'cobaltb.py build' first."
    return ["Directory %s does not exist." % tdir]

  if test_args is None:
    test_args = []
  test_args.append("-logtostderr")
  if verbose_count > 0:
    test_args.append("-v=%d" % verbose_count)

  print "Running all tests in %s " % tdir
  print "Test arguments: '%s'" % test_args
  failure_list = []
  for test_executable in os.listdir(tdir):
    bt_emulator_process = None
    shuffler_process = None
    analyzer_service_process = None
    report_master_process = None
    try:
      if start_bt_emulator:
        bt_emulator_process = process_starter.start_bigtable_emulator(
            wait=False)
      if start_cobalt_processes:
        time.sleep(1)
        analyzer_service_process = process_starter.start_analyzer_service(
            bigtable_instance_id=bigtable_instance_id,
            bigtable_project_name=bigtable_project_name,
            private_key_pem_file=E2E_TEST_ANALYZER_PRIVATE_KEY_PEM,
            verbose_count=verbose_count,
            vmodule=vmodule,
            wait=False)
        time.sleep(1)
        report_master_process = process_starter.start_report_master(
            use_tls=use_tls,
            tls_cert_file=tls_cert_file,
            tls_key_file=tls_key_file,
            bigtable_instance_id=bigtable_instance_id,
            bigtable_project_name=bigtable_project_name,
            verbose_count=verbose_count,
            vmodule=vmodule,
            wait=False)
        time.sleep(1)
        shuffler_process = process_starter.start_shuffler(
            use_tls=use_tls,
            tls_cert_file=tls_cert_file,
            tls_key_file=tls_key_file,
            private_key_pem_file=E2E_TEST_SHUFFLER_PRIVATE_KEY_PEM,
            verbose_count=verbose_count,
            wait=False)
      path = os.path.abspath(os.path.join(tdir, test_executable))
      if os.path.isdir(path):
        continue
      print "Running %s..." % test_executable
      command = [path] + test_args
      return_code = subprocess.call(command)
      if return_code != 0:
        failure_list.append(test_executable)
      if return_code < 0:
        print
        print "****** WARNING Process [%s] terminated by signal %d" % (
            command[0], -return_code)
    finally:
      process_starter.kill_process(shuffler_process, "Shuffler")
      process_starter.kill_process(analyzer_service_process, "Analyzer Service")
      process_starter.kill_process(report_master_process, "Report Master")
      process_starter.kill_process(bt_emulator_process,
                                   "Cloud Bigtable Emulator")
  if failure_list:
    return failure_list
  else:
    return None

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

"""The Cobalt build system command-line interface."""

import argparse
import filecmp
import fileinput
import json
import logging
import os
import shutil
import string
import subprocess
import sys
import tempfile

import tools.container_util as container_util
import tools.cpplint as cpplint
import tools.golint as golint
import tools.process_starter as process_starter
import tools.test_runner as test_runner
import tools.production_util as production_util

from tools.test_runner import E2E_TEST_ANALYZER_PUBLIC_KEY_PEM
from tools.test_runner import E2E_TEST_SHUFFLER_PUBLIC_KEY_PEM

from tools.process_starter import DEFAULT_ANALYZER_PRIVATE_KEY_PEM
from tools.process_starter import DEFAULT_ANALYZER_PUBLIC_KEY_PEM
from tools.process_starter import DEFAULT_SHUFFLER_PRIVATE_KEY_PEM
from tools.process_starter import DEFAULT_SHUFFLER_PUBLIC_KEY_PEM
from tools.process_starter import DEFAULT_ANALYZER_SERVICE_PORT
from tools.process_starter import DEFAULT_SHUFFLER_PORT
from tools.process_starter import DEFAULT_REPORT_MASTER_PORT
from tools.process_starter import DEMO_CONFIG_DIR
from tools.process_starter import LOCALHOST_TLS_CERT_FILE
from tools.process_starter import LOCALHOST_TLS_KEY_FILE
from tools.process_starter import PRODUCTION_CONFIG_DIR
from tools.process_starter import SHUFFLER_DEMO_CONFIG_FILE

THIS_DIR = os.path.abspath(os.path.dirname(__file__))
OUT_DIR = os.path.abspath(os.path.join(THIS_DIR, 'out'))
SYSROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, 'sysroot'))
PERSONAL_BT_ADMIN_SERVICE_ACCOUNT_CREDENTIALS_FILE = os.path.join(THIS_DIR,
    'personal_bt_admin_service_account.json')
PERSONAL_CLUSTER_JSON_FILE = os.path.join(THIS_DIR, 'personal_cluster.json')
BIGTABLE_TOOL_PATH = \
    os.path.join(OUT_DIR, 'tools', 'bigtable_tool', 'bigtable_tool')
OPEN_SSL_CONFIG_FILE = os.path.join(THIS_DIR, 'self-signed-with-ip.cnf')
GRPC_PEM_ROOTS = os.path.join(THIS_DIR, "third_party", "grpc",
                              "etc", "roots.pem")
CONFIG_SUBMODULE_PATH = os.path.join(THIS_DIR, "third_party", "config")
CONFIG_BINARY_PROTO = os.path.join(OUT_DIR, 'third_party', 'config',
    'cobalt_config.binproto')

_logger = logging.getLogger()
_verbose_count = 0

def _initLogging(verbose_count):
  """Ensures that the logger (obtained via logging.getLogger(), as usual) is
  initialized, with the log level set as appropriate for |verbose_count|
  instances of --verbose on the command line."""
  assert(verbose_count >= 0)
  if verbose_count == 0:
    level = logging.WARNING
  elif verbose_count == 1:
    level = logging.INFO
  else:  # verbose_count >= 2
    level = logging.DEBUG
  logging.basicConfig(format="%(relativeCreated).3f:%(levelname)s:%(message)s")
  logger = logging.getLogger()
  logger.setLevel(level)
  logger.debug("Initialized logging: verbose_count=%d, level=%d" %
               (verbose_count, level))

def ensureDir(dir_path):
  """Ensures that the directory at |dir_path| exists. If not it is created.

  Args:
    dir_path{string} The path to a directory. If it does not exist it will be
    created.
  """
  if not os.path.exists(dir_path):
    os.makedirs(dir_path)

def _compound_project_name(args):
  """ Builds a compound project name such as google.com:my-project

  Args:
    args: A namespace object as returned from the parse_args() function. It
          must have one argument named "cloud_project_prefix" and one named
          "cloud_project_name."
  """
  return container_util.compound_project_name(args.cloud_project_prefix,
                                              args.cloud_project_name)
def _setup(args):
  subprocess.check_call(["git", "submodule", "init"])
  subprocess.check_call(["git", "submodule", "update"])
  subprocess.check_call(["./setup.sh"])

def _update_config(args):
  savedDir = os.getcwd()
  try:
    os.chdir(CONFIG_SUBMODULE_PATH)
    subprocess.check_call(["git", "pull", "origin", "master"])
  finally:
    os.chdir(savedDir)

def _build(args):
  ensureDir(OUT_DIR)
  savedir = os.getcwd()
  os.chdir(OUT_DIR)
  subprocess.check_call([args.cmake_path, '-G', 'Ninja','..'])
  subprocess.check_call([args.ninja_path])
  os.chdir(savedir)

def _check_config(args):
  config_parser_bin = os.path.join(OUT_DIR, 'config', 'config_parser',
      'config_parser')
  if not os.path.isfile(config_parser_bin):
    print('%s could not be found. Run \n\n%s setup\n%s build\n\nand try again.'
        % (config_parser_bin, sys.argv[0], sys.argv[0]))
    return
  if not _check_test_configs(args):
    return
  subprocess.check_call([config_parser_bin, '-config_dir', args.config_dir,
      '-check_only'])

def _check_test_configs(args):
  testapp_config_path = os.path.join(args.config_dir, 'fuchsia', 'test_app2',
                                     'config.yaml')
  if not _check_config_exists(testapp_config_path):
    return False

  prober_config_path = os.path.join(args.config_dir, 'fuchsia', 'prober', 'config.yaml')
  if not _check_config_exists(prober_config_path):
    print('Run this command and try again: ./cobaltb.py write_prober_config')
    return False

  _, tmp_path = tempfile.mkstemp()
  _make_prober_config(testapp_config_path, tmp_path)
  is_same_file = filecmp.cmp(tmp_path, prober_config_path)
  os.remove(tmp_path)
  if not is_same_file:
    print ('Testapp config and prober config should be identical except for '
           'names of custom metrics output log types.\n'
           'Run this command and try again: ./cobaltb.py write_prober_config')
  return is_same_file

def _write_prober_config(args):
  testapp_config_path = os.path.join(args.config_dir, 'fuchsia', 'test_app2',
                                     'config.yaml')
  if not _check_config_exists(testapp_config_path):
    return False
  prober_config_path = os.path.join(args.config_dir, 'fuchsia', 'prober',
                                    'config.yaml')
  if os.path.isfile(prober_config_path):
    print('This action will overwrite the file %s.' % prober_config_path)
    answer = raw_input("Continue anyway? (y/N) ")
    if not _parse_bool(answer):
      return
  prober_dir = os.path.dirname(prober_config_path)
  if not os.path.exists(prober_dir):
    os.makedirs(prober_dir)
  _make_prober_config(testapp_config_path, prober_config_path)

def _check_config_exists(config_path):
  if not os.path.isfile(config_path):
    print("Expected config at path %s" % config_path)
    return False
  return True

def _make_prober_config(testapp_config_path, output_path):
  testapp_custom_log_source = "processed/<team_name>-cobalt-dev:custom-proto-test"
  prober_custom_log_source = "processed/<team_name>-cobalt-dev:custom-proto-prober-test"

  with open(testapp_config_path, 'r') as f:
    testapp_config = f.read()

  with open(output_path, 'w') as f:
    f.write(testapp_config.replace(testapp_custom_log_source, prober_custom_log_source))

def _lint(args):
  status = 0
  status += cpplint.main()
  status += golint.main()

  exit(status)

# Specifiers of subsets of tests to run
TEST_FILTERS =['all', 'gtests', 'nogtests', 'gotests', 'nogotests',
               'btemulator', 'nobtemulator', 'e2e', 'noe2e', 'cloud_bt', 'perf',
               'config_parser']

# Returns 0 if all tests pass, otherwise returns 1. Prints a failure or success
# message.
def _test(args):
  # A map from positive filter specifiers to the list of test directories
  # it represents. Note that 'cloud_bt' and 'perf' tests are special. They are
  # not included in 'all'. They are only run if asked for explicitly.
  FILTER_MAP = {
    'all': ['gtests', 'go_tests', 'gtests_btemulator', 'e2e_tests',
            'config_parser_tests'],
    'gtests': ['gtests'],
    'gotests' : ['go_tests'],
    'btemulator': ['gtests_btemulator'],
    'e2e': ['e2e_tests'],
    'cloud_bt' : ['gtests_cloud_bt'],
    'perf' : ['perf_tests'],
    'config_parser' : [ 'config_parser_tests' ]
  }

  # A list of test directories for which the contained tests assume the
  # existence of a running instance of the Bigtable Emulator.
  NEEDS_BT_EMULATOR=['gtests_btemulator', 'e2e_tests']

  # A list of test directories for which the contained tests assume the
  # existence of a running instance of the Cobalt processes (Shuffler,
  # Analyzer Service, Report Master.)
  NEEDS_COBALT_PROCESSES=['e2e_tests']

  # By default try each test just once.
  num_times_to_try = 1

  # Get the list of test directories we should run.
  if args.tests.startswith('no'):
    test_dirs = [test_dir for test_dir in FILTER_MAP['all']
        if test_dir not in FILTER_MAP[args.tests[2:]]]
  else:
    test_dirs = FILTER_MAP[args.tests]

  failure_list = []
  print ("Will run tests in the following directories: %s." %
      ", ".join(test_dirs))

  bigtable_project_name = ''
  bigtable_instance_id = ''
  for test_dir in test_dirs:
    start_bt_emulator = ((test_dir in NEEDS_BT_EMULATOR)
        and not args.use_cloud_bt and not args.cobalt_on_personal_cluster
        and not args.production_dir)
    start_cobalt_processes = ((test_dir in NEEDS_COBALT_PROCESSES)
        and not args.cobalt_on_personal_cluster and not args.production_dir)
    test_args = None
    if (test_dir == 'gtests_cloud_bt'):
      if not os.path.exists(bt_admin_service_account_credentials_file):
        print ('You must first create the file %s.' %
               bt_admin_service_account_credentials_file)
        print 'See the instructions in README.md.'
        return
      bigtable_project_name_from_args = _compound_project_name(args)
      if bigtable_project_name_from_args == '':
        print '--cloud_project_name must be specified'
        failure_list.append('gtests_cloud_bt')
        break
      if args.bigtable_instance_id == '':
        print '--bigtable_instance_id must be specified'
        failure_list.append('gtests_cloud_bt')
        break
      test_args = [
          "--bigtable_project_name=%s" % bigtable_project_name_from_args,
          "--bigtable_instance_id=%s" % args.bigtable_instance_id
      ]
      bigtable_project_name = bigtable_project_name_from_args
      bigtable_instance_id = args.bigtable_instance_id
    if (test_dir == 'gtests_btemulator' and args.tests != 'btemulator'):
      # TODO(rudominer) At the moment the EnableReportScheduling test case is
      # flaky so I am disabling it. If my analysis is correct the flakiness is
      # due to a bug in the bigtable emulator.
      # https://github.com/GoogleCloudPlatform/google-cloud-go/issues/826
      # We disalbe the flaky test in all cases except when explicitly running
      # only the bt emulator tests as requested via the flag --tests=btemulator.
      test_args = [
        "--gtest_filter=-*EnableReportScheduling*"
      ]
    if (test_dir == 'e2e_tests'):
      # The end-to-end test is naturally flaky. Our strategy is to attempt
      # it multiple times.
      num_times_to_try = 3
      analyzer_pk_pem_file=E2E_TEST_ANALYZER_PUBLIC_KEY_PEM
      report_master_uri = "localhost:%d" % DEFAULT_REPORT_MASTER_PORT
      shuffler_pk_pem_file=E2E_TEST_SHUFFLER_PUBLIC_KEY_PEM
      shuffler_uri = "localhost:%d" % DEFAULT_SHUFFLER_PORT
      if args.cobalt_on_personal_cluster or args.production_dir:
        if args.cobalt_on_personal_cluster and args.production_dir:
          print ("Do not specify both --production_dir and "
                 "-cobalt_on_personal_cluster.")
          failure_list.append('e2e_tests')
          break
        public_uris = container_util.get_public_uris(args.cluster_name,
            args.cloud_project_prefix, args.cloud_project_name,
            args.cluster_zone)
        report_master_uri = public_uris["report_master"]
        shuffler_uri = public_uris["shuffler"]
        if args.use_cloud_bt:
          # use_cloud_bt means to use local instances of the Cobalt processes
          # connected to a Cloud Bigtable. cobalt_on_personal_cluster means to
          # use Cloud instances of the Cobalt processes. These two options
          # are inconsistent.
          print ("Do not specify both --use_cloud_bt and "
                 "-cobalt_on_personal_cluster or --production_dir.")
          failure_list.append('e2e_tests')
          break
      if args.cobalt_on_personal_cluster:
        analyzer_pk_pem_file=DEFAULT_ANALYZER_PUBLIC_KEY_PEM
        shuffler_pk_pem_file=DEFAULT_SHUFFLER_PUBLIC_KEY_PEM
      elif args.production_dir:
        pem_directory = os.path.abspath(args.production_dir)
        analyzer_pk_pem_file = os.path.join(pem_directory,
            'analyzer_public.pem')
        shuffler_pk_pem_file = os.path.join(pem_directory,
            'shuffler_public.pem')
      report_master_uri = (args.report_master_preferred_address
          or report_master_uri)
      shuffler_uri = (args.shuffler_preferred_address or shuffler_uri)
      test_args = [
          "-analyzer_pk_pem_file=%s" % analyzer_pk_pem_file,
          "-shuffler_uri=%s" % shuffler_uri,
          "-shuffler_pk_pem_file=%s" % shuffler_pk_pem_file,
          "-report_master_uri=%s" % report_master_uri,
          ("-observation_querier_path=%s" %
              process_starter.OBSERVATION_QUERIER_PATH),
          "-test_app_path=%s" % process_starter.TEST_APP_PATH,
          "-config_bin_proto_path=%s" % CONFIG_BINARY_PROTO,
          "-sub_process_v=%d"%_verbose_count
      ]
      use_tls = _parse_bool(args.use_tls)
      if use_tls:
        test_args += [
            "-use_tls",
            "-report_master_root_certs=%s" % args.report_master_root_certs,
            "-shuffler_root_certs=%s" % args.shuffler_root_certs
        ]
        if (not (args.cobalt_on_personal_cluster or args.production_dir)):
            test_args += ["-skip_oauth"]
      if (args.use_cloud_bt or args.cobalt_on_personal_cluster or
          args.production_dir):
        bigtable_project_name_from_args = _compound_project_name(args)
        test_args = test_args + [
          "-bigtable_tool_path=%s" % BIGTABLE_TOOL_PATH,
          "-bigtable_project_name=%s" % bigtable_project_name_from_args,
          "-bigtable_instance_id=%s" % args.bigtable_instance_id,
        ]
        bigtable_project_name = bigtable_project_name_from_args
        bigtable_instance_id = args.bigtable_instance_id
      if args.production_dir or args.cobalt_on_personal_cluster:
        # Currently thresholding in the Shuffler is disabled when we run the Shuffler in either
        # production or devel because in both cases we deploy two Shuffler instances but the
        # Shuffler thresholding logic was not written to support that. When we run the Shuffler
        # locally on the test machine then we still use and test thresholding.
        test_args = test_args + [
          "-do_shuffler_threshold_test=false",
        ]
    print '********************************************************'
    this_failure_list = []
    for attempt in range(num_times_to_try):
      this_failure_list = test_runner.run_all_tests(
          test_dir, start_bt_emulator=start_bt_emulator,
          start_cobalt_processes=start_cobalt_processes,
          bigtable_project_name=bigtable_project_name,
          bigtable_instance_id=bigtable_instance_id,
          verbose_count=_verbose_count,
          vmodule=_vmodule,
          use_tls=_parse_bool(args.use_tls),
          tls_cert_file=args.tls_cert_file,
          tls_key_file=args.tls_key_file,
          test_args=test_args)
      if  this_failure_list and attempt < num_times_to_try - 1:
        print
        print '***** Attempt %i of %s failed. Retrying...' % (attempt,
            this_failure_list)
        print
      else:
        break;
    if this_failure_list:
      failure_list.append("%s (%s)" % (test_dir, this_failure_list))

  print
  if failure_list:
    print '******************* SOME TESTS FAILED *******************'
    print 'failures = %s' % failure_list
    return 1
  else:
    print '******************* ALL TESTS PASSED *******************'
    return 0

# Files and directories in the out directory to NOT delete when doing
# a partial clean.
TO_SKIP_ON_PARTIAL_CLEAN = [
  'CMakeFiles', 'third_party', '.ninja_deps', '.ninja_log', 'CMakeCache.txt',
  'build.ninja', 'cmake_install.cmake', 'rules.ninja'
]

def _clean(args):
  if args.full:
    print "Deleting the out directory..."
    shutil.rmtree(OUT_DIR, ignore_errors=True)
  else:
    print "Doing a partial clean. Pass --full for a full clean."
    if not os.path.exists(OUT_DIR):
      return
    for f in os.listdir(OUT_DIR):
      if not f in TO_SKIP_ON_PARTIAL_CLEAN:
        full_path = os.path.join(OUT_DIR, f)
        if os.path.isfile(full_path):
          os.remove(full_path)
        else:
           shutil.rmtree(full_path, ignore_errors=True)

def _start_bigtable_emulator(args):
  process_starter.start_bigtable_emulator()


def _start_shuffler(args):
  process_starter.start_shuffler(port=args.port,
                                 analyzer_uri=args.analyzer_uri,
                                 use_memstore=args.use_memstore,
                                 erase_db=(not args.keep_existing_db),
                                 config_file=args.config_file,
                                 use_tls=_parse_bool(args.use_tls),
                                 tls_cert_file=args.tls_cert_file,
                                 tls_key_file=args.tls_key_file,
                                 # Because it makes the demo more interesting
                                 # we use verbose_count at least 3.
                                 verbose_count=max(3, _verbose_count))

def _start_analyzer_service(args):
  bigtable_project_name = ''
  bigtable_instance_id = ''
  if args.use_cloud_bt:
    bigtable_project_name = _compound_project_name(args)
    bigtable_instance_id = args.bigtable_instance_id
  process_starter.start_analyzer_service(port=args.port,
      bigtable_project_name=bigtable_project_name,
      bigtable_instance_id=bigtable_instance_id,
      # Because it makes the demo more interesting
      # we use verbose_count at least 3.
      verbose_count=max(3, _verbose_count))

def _start_report_master(args):
  bigtable_project_name = ''
  bigtable_instance_id = ''
  if args.use_cloud_bt:
    bigtable_project_name = _compound_project_name(args)
    bigtable_instance_id = args.bigtable_instance_id
  process_starter.start_report_master(port=args.port,
      bigtable_project_name=bigtable_project_name,
      bigtable_instance_id=bigtable_instance_id,
      use_tls=_parse_bool(args.use_tls),
      tls_cert_file=args.tls_cert_file,
      tls_key_file=args.tls_key_file,
      verbose_count=_verbose_count)

def _start_test_app(args):
  analyzer_uri = "localhost:%d" % DEFAULT_ANALYZER_SERVICE_PORT
  shuffler_uri = (args.shuffler_preferred_address or
      "localhost:%d" % DEFAULT_SHUFFLER_PORT)
  analyzer_public_key_pem = \
    DEFAULT_ANALYZER_PUBLIC_KEY_PEM
  shuffler_public_key_pem = \
    DEFAULT_SHUFFLER_PUBLIC_KEY_PEM
  use_tls = _parse_bool(args.use_tls)
  if args.production_dir:
    pem_directory = os.path.abspath(args.production_dir)
    analyzer_public_key_pem = os.path.join(pem_directory,
        'analyzer_public.pem')
    shuffler_public_key_pem = os.path.join(pem_directory,
        'shuffler_public.pem')
  if args.cobalt_on_personal_cluster or args.production_dir:
    public_uris = container_util.get_public_uris(args.cluster_name,
        args.cloud_project_prefix, args.cloud_project_name, args.cluster_zone)
    shuffler_uri = args.shuffler_preferred_address or public_uris["shuffler"]
  process_starter.start_test_app(shuffler_uri=shuffler_uri,
      analyzer_uri=analyzer_uri,
      analyzer_pk_pem_file=analyzer_public_key_pem,
      use_tls=use_tls,
      root_certs_pem_file=args.shuffler_root_certs,
      shuffler_pk_pem_file=shuffler_public_key_pem,
      project_id=args.project_id,
      automatic=args.automatic,
      # Because it makes the demo more interesting
      # we use verbose_count at least 3.
      verbose_count=max(3, _verbose_count))

def _start_report_client(args):
  report_master_uri = (args.report_master_preferred_address or
      "localhost:%d" % DEFAULT_REPORT_MASTER_PORT)
  if args.cobalt_on_personal_cluster or args.production_dir:
    public_uris = container_util.get_public_uris(args.cluster_name,
        args.cloud_project_prefix, args.cloud_project_name, args.cluster_zone)
    report_master_uri = (args.report_master_preferred_address
            or public_uris["report_master"])
  process_starter.start_report_client(
      report_master_uri=report_master_uri,
      use_tls=_parse_bool(args.use_tls),
      root_certs_pem_file=args.report_master_root_certs,
      project_id=args.project_id,
      verbose_count=_verbose_count)

def _start_observation_querier(args):
  bigtable_project_name = ''
  bigtable_instance_id = ''
  if args.use_cloud_bt or args.production_dir:
    bigtable_project_name = _compound_project_name(args)
    bigtable_instance_id = args.bigtable_instance_id
  process_starter.start_observation_querier(
      bigtable_project_name=bigtable_project_name,
      bigtable_instance_id=bigtable_instance_id,
      verbose_count=_verbose_count)

def _generate_keys(args):
  path = os.path.join(OUT_DIR, 'tools', 'key_generator', 'key_generator')
  subprocess.check_call([path])

def _generate_self_signed_certificate(key_file, cert_file,
                                      ip_address='127.0.0.1',
                                      hostname='localhost'):
  # First we build an openssl config file by replacing two tokens in our
  # template file with the IP address.
  ip_address_token = '@@@IP_ADDRESS@@@'
  hostname_token = '@@@HOSTNAME@@@'
  openssl_config_file = os.path.join(OUT_DIR, 'self-signed-with-ip.cnf')
  with open(openssl_config_file, 'w+b') as f:
    for line in fileinput.input(OPEN_SSL_CONFIG_FILE):
      if string.find(line, ip_address_token) != -1:
         line = line.replace(ip_address_token, ip_address)
      if string.find(line, hostname_token) != -1:
         line = line.replace(hostname_token, hostname)
      f.write(line)

  # then we invoke openssl and point it at the config file we just generated.
  subprocess.check_call(['openssl', 'req', '-x509', '-nodes', '-days', '365',
    '-config', openssl_config_file,
    '-newkey', 'rsa:2048', '-keyout', key_file, '-out',
    cert_file])

def _generate_cert(args):
  if not (args.path_to_key and args.path_to_cert):
    print '--path-to-key and --path-to-cert are both required.'
    return
  _generate_self_signed_certificate(args.path_to_key, args.path_to_cert,
                                    args.ip_address, args.hostname)

def _invoke_bigtable_tool(args, command):
  if not os.path.exists(bt_admin_service_account_credentials_file):
    print ('You must first create the file %s.' %
           bt_admin_service_account_credentials_file)
    print 'See the instructions in README.md.'
    return
  bigtable_project_name_from_args = _compound_project_name(args)
  if bigtable_project_name_from_args == '':
    print '--cloud_project_name must be specified'
    return
  if args.bigtable_instance_id == '':
    print '--bigtable_instance_id must be specified'
    return
  cmd = [BIGTABLE_TOOL_PATH,
         "-command", command,
         "-bigtable_project_name", bigtable_project_name_from_args,
         "-bigtable_instance_id", args.bigtable_instance_id]
  if command == 'delete_observations':
    if args.customer_id == 0:
      print '--customer_id must be specified'
      return
    if args.project_id == 0:
      print '--project_id must be specified'
      return
    if args.metric_id == 0:
      print '--metric_id must be specified'
      return
    cmd = cmd + ["-customer", str(args.customer_id),
                 "-project", str(args.project_id),
                 "-metric", str(args.metric_id)]
  elif command == 'delete_reports':
    if args.customer_id == 0:
      print '--customer_id must be specified'
      return
    if args.project_id == 0:
      print '--project_id must be specified'
      return
    if args.report_config_id == 0:
      print '--report_config_id must be specified'
      return
    cmd = cmd + ["-customer", str(args.customer_id),
                 "-project", str(args.project_id),
                 "-report_config", str(args.report_config_id)]
    if args.danger_danger_delete_production_reports:
      cmd = cmd + ["-danger_danger_delete_production_reports"]
  subprocess.check_call(cmd)

def _provision_bigtable(args):
  _invoke_bigtable_tool(args, "create_tables")

def _delete_observations(args):
  _invoke_bigtable_tool(args, "delete_observations")

def _delete_reports(args):
  _invoke_bigtable_tool(args, "delete_reports")

def _deploy_show(args):
  container_util.display(args.cloud_project_prefix, args.cloud_project_name,
      args.cluster_zone, args.cluster_name)

def _deploy_login(args):
  container_util.login(args.cloud_project_prefix, args.cloud_project_name)

def _deploy_authenticate(args):
  container_util.authenticate(args.cluster_name, args.cloud_project_prefix,
      args.cloud_project_name, args.cluster_zone)

def _deploy_build(args):
  if args.production_dir:
    print("Production configs should be built using './cobaltb.py deploy "
    "production_build' which will build a clean version of the binaries, then "
    "build the docker images in one step.")
    answer = raw_input("Continue anyway? (y/N) ")
    if not _parse_bool(answer):
      return
  container_util.build_all_docker_images(
      shuffler_config_file=args.shuffler_config_file,
      cobalt_config_dir=args.cobalt_config_dir)
  if not _is_config_up_to_date():
    print("Docker image was built using an older config. You can update the "
          "config using the './cobaltb.py update_config' command.")

def _deploy_production_build(args):
  if not args.production_dir:
    print("Notice that you have not passed the flag --production_dir and so "
      "you will be pushing the built containers to your personal devel "
      "cluster, not a production cluster.")
    answer = raw_input("Continue? (y/N) ")
    if not _parse_bool(answer):
      return
  full_ref = production_util.build_and_push_production_docker_images(
      args.cloud_project_name,
      args.production_dir,
      args.git_revision)

  if full_ref:
    print
    print
    print('Done pushing the new build. To set this as the default build, copy '
          'the following json blob into the versions.json.')
    print
    print '{'
    print '  "shuffler": "%s",' % full_ref
    print '  "report-master": "%s",' % full_ref
    print '  "analyzer-service": "%s"' % full_ref
    print '}'



def _deploy_push(args):
  if args.job == 'shuffler':
    container_util.push_shuffler_to_container_registry(
        args.cloud_project_prefix, args.cloud_project_name)
  elif args.job == 'analyzer-service':
    container_util.push_analyzer_service_to_container_registry(
        args.cloud_project_prefix, args.cloud_project_name)
  elif args.job == 'report-master':
    container_util.push_report_master_to_container_registry(
        args.cloud_project_prefix, args.cloud_project_name)
  else:
    print('Unknown job "%s". I only know how to push "shuffler", '
          '"analyzer-service" and "report-master".' % args.job)

def _parse_bool(bool_string):
  return bool_string.lower() in ['true', 't', 'y', 'yes', '1']

def _load_versions_file(args):
  versions = {
      'shuffler': 'latest',
      'analyzer-service': 'latest',
      'report-master': 'latest',
  }

  try:
    with open(args.deployed_versions_file) as f:
      read_versions_file = {}
      try:
        read_versions_file = json.load(f)
      except ValueError:
        print('%s could not be parsed.' % args.deployed_versions_file)
    for key in read_versions_file:
      if key in versions:
        versions[key] = read_versions_file[key]
  except IOError:
    print('%s could not be found.' % args.deployed_versions_file)

  return versions

def _deploy_start(args):
  version = _load_versions_file(args).get(args.job, 'latest')
  components = [c.strip() for c in args.components.split(',')]

  if args.job == 'shuffler':
    container_util.start_shuffler(
        args.cloud_project_prefix,
        args.cloud_project_name,
        args.cluster_zone, args.cluster_name,
        args.shuffler_static_ip,
        components,
        version,
        use_memstore=_parse_bool(args.shuffler_use_memstore),
        danger_danger_delete_all_data_at_startup=
            args.danger_danger_delete_all_data_at_startup)
  elif args.job == 'analyzer-service':
    if args.bigtable_instance_id == '':
        print '--bigtable_instance_id must be specified'
        return
    container_util.start_analyzer_service(
        args.cloud_project_prefix, args.cloud_project_name,
        args.cluster_zone, args.cluster_name,
        args.bigtable_instance_id,
        args.analyzer_service_static_ip,
        components,
        version)
  elif args.job == 'report-master':
    if args.bigtable_instance_id == '':
        print '--bigtable_instance_id must be specified'
        return
    container_util.start_report_master(
        args.cloud_project_prefix, args.cloud_project_name,
        args.cluster_zone, args.cluster_name,
        args.bigtable_instance_id,
        args.report_master_static_ip,
        components,
        version,
        args.report_master_update_repo_url,
        enable_report_scheduling=_parse_bool(
            args.report_master_enable_scheduling))
  else:
    print('Unknown job "%s". I only know how to start "shuffler", '
          '"analyzer-service" and "report-master".' % args.job)

def _deploy_stop(args):
  version = _load_versions_file(args).get(args.job, 'latest')
  components = [c.strip() for c in args.components.split(',')]

  if args.job == 'shuffler':
    container_util.stop_shuffler(args.cloud_project_prefix,
        args.cloud_project_name, args.cluster_zone, args.cluster_name,
        components, version)
  elif args.job == 'analyzer-service':
    container_util.stop_analyzer_service(args.cloud_project_prefix,
        args.cloud_project_name, args.cluster_zone, args.cluster_name,
        components, version)
  elif args.job == 'report-master':
    container_util.stop_report_master(args.cloud_project_prefix,
        args.cloud_project_name, args.cluster_zone, args.cluster_name,
        components, version)
  else:
    print('Unknown job "%s". I only know how to stop "shuffler", '
          '"analyzer-service" and "report-master".' % args.job)

def _deploy_stopstart(args):
  _deploy_stop(args)
  _deploy_start(args)

def _deploy_upload_secret_keys(args):
  container_util.create_analyzer_private_key_secret(args.cloud_project_prefix,
      args.cloud_project_name, args.cluster_zone, args.cluster_name,
      args.analyzer_private_key_pem)
  container_util.create_shuffler_private_key_secret(args.cloud_project_prefix,
      args.cloud_project_name, args.cluster_zone, args.cluster_name,
      args.shuffler_private_key_pem)

def _deploy_delete_secret_keys(args):
  container_util.delete_analyzer_private_key_secret(args.cloud_project_prefix,
      args.cloud_project_name, args.cluster_zone, args.cluster_name)
  container_util.delete_shuffler_private_key_secret(args.cloud_project_prefix,
      args.cloud_project_name, args.cluster_zone, args.cluster_name)

def _deploy_endpoint(args):
  if args.cloud_project_prefix and args.cloud_project_prefix != 'google.com':
    print('Endpoints cannot be configured by this script for projects with '
          'the prefix: ' % args.cloud_project_prefix)
    return
  if args.job == 'report-master':
    container_util.configure_report_master_endpoint(args.cloud_project_prefix,
        args.cloud_project_name, args.report_master_static_ip)
  elif args.job == 'shuffler':
    container_util.configure_shuffler_endpoint(args.cloud_project_prefix,
        args.cloud_project_name, args.shuffler_static_ip)
  else:
    print('Unknown job "%s". I only know how to configure endpoints for the '
          '"report-master" or the "shuffler".' % args.job)

def _deploy_addresses(args):
  container_util.reserve_static_ip_addresses(args.cloud_project_prefix,
      args.cloud_project_name, args.cluster_zone)

def _deploy_upload_certificate(args):
  path_to_cert = args.path_to_cert
  path_to_key = args.path_to_key

  if not path_to_cert or not path_to_key:
    print('Both --path-to-cert and --path-to-key must be set!')
    return

  path_to_cert = os.path.abspath(path_to_cert)
  path_to_key = os.path.abspath(path_to_key)

  if args.job == 'report-master':
    container_util.create_cert_secret_for_report_master(
        args.cloud_project_prefix, args.cloud_project_name, args.cluster_zone,
        args.cluster_name, path_to_cert, path_to_key)
  elif args.job == 'shuffler':
    container_util.create_cert_secret_for_shuffler(
        args.cloud_project_prefix, args.cloud_project_name, args.cluster_zone,
        args.cluster_name, path_to_cert, path_to_key)
  else:
    print('Unknown job "%s". I only know how to deploy certificates for the '
          '"report-master" or the "shuffler".' % args.job)

def _deploy_delete_certificate(args):
  if args.job == 'report-master':
    container_util.delete_cert_secret_for_report_master(
        args.cloud_project_prefix, args.cloud_project_name, args.cluster_zone,
        args.cluster_name)
  elif args.job == 'shuffler':
    container_util.delete_cert_secret_for_shuffler(
        args.cloud_project_prefix, args.cloud_project_name, args.cluster_zone,
        args.cluster_name)
  else:
    print('Unknown job "%s". I only know how to delete certificates for the '
          '"report-master" or the "shuffler".' % args.job)

def _deploy_upload_service_account_key(args):
  container_util.create_report_master_gcs_service_account_secret(
      args.cloud_project_prefix, args.cloud_project_name, args.cluster_zone,
      args.cluster_name, args.service_account_key_json)

def _deploy_kube_ui(args):
  container_util.kube_ui(args.cloud_project_name, args.cluster_zone,
      args.cluster_name)

def _default_shuffler_config_file(cluster_settings):
  if cluster_settings['shuffler_config_file'] :
    return  os.path.join(THIS_DIR, cluster_settings['shuffler_config_file'])
  return SHUFFLER_DEMO_CONFIG_FILE

def _default_cobalt_config_dir(cluster_settings):
  if cluster_settings['cobalt_config_dir'] :
    return os.path.join(THIS_DIR, cluster_settings['cobalt_config_dir'])
  return DEMO_CONFIG_DIR

def _default_report_master_enable_scheduling(cluster_settings):
  if cluster_settings['report_master_enable_scheduling']:
    return cluster_settings['report_master_enable_scheduling']
  return 'false'

def _default_shuffler_use_memstore(cluster_settings):
  if cluster_settings['shuffler_use_memstore']:
    return cluster_settings['shuffler_use_memstore']
  return 'false'

def _default_use_tls(cobalt_is_running_on_gke, cluster_settings):
  if cobalt_is_running_on_gke:
    return cluster_settings['use_tls'] or 'false'
  return 'false'

def _default_shuffler_root_certs(cobalt_is_running_on_gke, cluster_settings):
  if cobalt_is_running_on_gke:
    return cluster_settings['shuffler_root_certs']
  return LOCALHOST_TLS_CERT_FILE

def _default_report_master_root_certs(cobalt_is_running_on_gke, cluster_settings):
  if cobalt_is_running_on_gke:
    return cluster_settings['report_master_root_certs']
  return LOCALHOST_TLS_CERT_FILE

def _default_shuffler_preferred_address(cobalt_is_running_on_gke,
                                        cluster_settings):
  if cobalt_is_running_on_gke:
    return cluster_settings['shuffler_preferred_address']
  return ''

def _default_report_master_preferred_address(cobalt_is_running_on_gke,
                                             cluster_settings):
  if cobalt_is_running_on_gke:
    return cluster_settings['report_master_preferred_address']
  return ''

def _cluster_settings_from_json(cluster_settings, json_file_path):
  """ Reads cluster settings from a json file and adds them to a dictionary.

  Args:
    cluster_settings: A dictionary of cluster settings whose values will be
    overwritten by any corresponding values in the json file. Any values in
    the json file that do not correspond to a key in this dictionary will
    be ignored.

    json_file_path: The full path to a json file that must exist.
  """
  print ('The GKE cluster settings file being used is: %s.' % json_file_path)
  with open(json_file_path) as f:
    try:
      read_cluster_settings = json.load(f)
    except ValueError:
      print('%s could not be parsed.' % json_file_path)
  for key in read_cluster_settings:
    if key in cluster_settings:
      cluster_settings[key] = read_cluster_settings[key]

  cluster_settings['deployed_versions_file'] = os.path.join(
      os.path.dirname(json_file_path),
      cluster_settings['deployed_versions_file'])

def _add_cloud_project_args(parser, cluster_settings):
  parser.add_argument('--cloud_project_prefix',
      help='The prefix part of name of the Cloud project with which you wish '
           'to work. This is usually an organization domain name if your '
           'Cloud project is associated with one. Pass the empty string for no '
           'prefix. '
           'Default=%s.' %cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project with which you wish '
           'to work. This is the full project name if --cloud_project_prefix '
           'is empty. Otherwise the full project name is '
           '<cloud_project_prefix>:<cloud_project_name>. '
           'Default=%s' % cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])

def _add_cloud_access_args(parser, cluster_settings):
  _add_cloud_project_args(parser, cluster_settings)
  parser.add_argument('--cluster_name',
      help='The GKE "container cluster" within your Cloud project with which '
           'you wish to work. '
           'Default=%s' % cluster_settings['cluster_name'],
      default=cluster_settings['cluster_name'])
  parser.add_argument('--cluster_zone',
      help='The zone in which your GKE "container cluster" is located. '
           'Default=%s' % cluster_settings['cluster_zone'],
      default=cluster_settings['cluster_zone'])

def _add_static_ip_args(parser, cluster_settings):
  parser.add_argument('--shuffler_static_ip',
      help='A static IP address that has been previously reserved on the GKE '
           'cluster for the Shuffler. '
           'Default=%s' % cluster_settings['shuffler_static_ip'],
      default=cluster_settings['shuffler_static_ip'])
  parser.add_argument('--report_master_static_ip',
      help='A static IP address that has been previously reserved on the GKE '
           'cluster for the Report Master. '
           'Default=%s' % cluster_settings['report_master_static_ip'],
      default=cluster_settings['report_master_static_ip'])
  parser.add_argument('--analyzer_service_static_ip',
      help='A static IP address that has been previously reserved on the GKE '
           'cluster for the Analyzer Service. '
           'Default=%s' % cluster_settings['analyzer_service_static_ip'],
      default=cluster_settings['analyzer_service_static_ip'])

def _add_gke_deployment_args(parser, cluster_settings):
  _add_cloud_access_args(parser, cluster_settings)
  _add_static_ip_args(parser, cluster_settings)

def _add_deploy_start_args(parser, cluster_settings):
  parser.add_argument('--bigtable_instance_id',
      help='Specify a Cloud Bigtable instance within the specified Cloud '
           'project that the Analyzer should connect to. This is required '
           'if and only if you are starting one of the two Analyzer jobs. '
           'Default=%s' % cluster_settings['bigtable_instance_id'],
      default=cluster_settings['bigtable_instance_id'])
  default_report_master_enable_scheduling = \
      _default_report_master_enable_scheduling(cluster_settings)
  parser.add_argument('--report_master_enable_scheduling',
      default=default_report_master_enable_scheduling,
      help=('When starting the ReportMaster, should the ReportMaster run all '
            'reports automatically on a schedule? Default=%s.' %
            default_report_master_enable_scheduling))
  parser.add_argument('--report_master_update_repo_url',
      help='URL to a git repository containing a cobalt configuration in '
           'its master branch. If this flag is set, the configuration of '
           'report master will be updated by pulling from the specified '
           'repository before scheduled reports are run. '
           '(e.g. "https://cobalt-analytics.googlesource.com/config/")',
      default=cluster_settings['report_master_update_repo_url'])
  default_shuffler_use_memstore = _default_shuffler_use_memstore(
      cluster_settings)
  parser.add_argument('--shuffler-use-memstore',
      default=default_shuffler_use_memstore,
      help=('When starting the Shuffler, should the Suffler use its in-memory '
            'data store rather than a persistent datastore? Default=%s.' %
            default_shuffler_use_memstore))
  parser.add_argument('-danger_danger_delete_all_data_at_startup',
      help='When starting the Shuffler, should all of the Observations '
      'collected during previous runs of the Shuffler be permanently and '
      'irrecoverably deleted from the Shuffler\'s store upon startup?',
      action='store_true')

def _add_deploy_start_stop_args(parser, cluster_settings, verb):
  parser.add_argument('--deployed_versions_file',
      help='A file with version numbers to use',
      default=cluster_settings['deployed_versions_file'])
  parser.add_argument('--job',
      help='The job you wish to ' + verb + '. Valid choices are "shuffler", '
           '"analyzer-service", "report-master". Required.')
  parser.add_argument('--components',
      help='The components you wish to ' + verb + '. Valid choices are '
           '"service" and "statefulset". Default: "service,statefulset".',
      default='service,statefulset')

def _is_config_up_to_date():
  savedDir = os.getcwd()
  try:
    os.chdir(CONFIG_SUBMODULE_PATH)
    # Get the hash for the latest local revision.
    local_hash = subprocess.check_output(['git', 'rev-parse', '@'])
    # Get the hash for the latest remote revision.
    remote_hash = subprocess.check_output(['git', 'rev-parse', 'origin/master'])
    return (local_hash == remote_hash)
  finally:
    os.chdir(savedDir)

def main():
  if not sys.platform.startswith('linux'):
    print 'Only linux is supported!'
    return 1
  # We parse the command line flags twice. The first time we are looking
  # only for two particular flags, namely --production_dir and
  # --cobalt_on_personal_cluster. This first pass
  # will not print any help and will ignore all other flags.
  parser0 = argparse.ArgumentParser(add_help=False)
  parser0.add_argument('--production_dir', default='')
  parser0.add_argument('-cobalt_on_personal_cluster', action='store_true')
  args0, ignore = parser0.parse_known_args()
  # If the flag --production_dir is passed then it must be the path to
  # a directory containing a file named cluster.json. The contents of this
  # json file is used to set default values for many of the other flags. That
  # explains why we want to parse the flags twice.
  production_cluster_json_file = ''
  if args0.production_dir:
    production_cluster_json_file = os.path.join(args0.production_dir,
                                                'cluster.json')
    if not  os.path.exists(production_cluster_json_file):
       print ('File not found: %s.' % production_cluster_json_file)
       return

  # cluster_settings contains the default values for many of the flags.
  cluster_settings = {
    'analyzer_service_static_ip' : '',
    'bigtable_instance_id': '',
    'cloud_project_name': '',
    'cloud_project_prefix': '',
    'cluster_name': '',
    'cluster_zone': '',
    'cobalt_config_dir': '',
    'deployed_versions_file': 'versions.json',
    'report_master_enable_scheduling': '',
    'report_master_preferred_address': '',
    'report_master_root_certs': '',
    'report_master_static_ip' : '',
    'report_master_update_repo_url': '',
    'shuffler_config_file': '',
    'shuffler_preferred_address': '',
    'shuffler_root_certs': '',
    'shuffler_static_ip' : '',
    'shuffler_use_memstore' : '',
    'use_tls': '',
  }
  if production_cluster_json_file:
    _cluster_settings_from_json(cluster_settings, production_cluster_json_file)
  elif os.path.exists(PERSONAL_CLUSTER_JSON_FILE):
    _cluster_settings_from_json(cluster_settings,
        PERSONAL_CLUSTER_JSON_FILE)

  # We also use the flag --production_dir to find the PEM files.
  analyzer_private_key_pem = DEFAULT_ANALYZER_PRIVATE_KEY_PEM
  shuffler_private_key_pem = DEFAULT_SHUFFLER_PRIVATE_KEY_PEM
  if args0.production_dir:
    pem_directory = os.path.abspath(args0.production_dir)
    analyzer_private_key_pem = os.path.join(pem_directory,
        'analyzer_private.pem')
    shuffler_private_key_pem = os.path.join(pem_directory,
        'shuffler_private.pem')

  cobalt_is_running_on_gke = (args0.cobalt_on_personal_cluster or
      args0.production_dir)
  default_use_tls = _default_use_tls(cobalt_is_running_on_gke, cluster_settings)
  default_shuffler_preferred_address = _default_shuffler_preferred_address(
      cobalt_is_running_on_gke, cluster_settings)
  default_report_master_preferred_address = \
      _default_report_master_preferred_address(cobalt_is_running_on_gke,
                                               cluster_settings)
  default_shuffler_root_certs = _default_shuffler_root_certs(
      cobalt_is_running_on_gke, cluster_settings)
  default_report_master_root_certs = _default_report_master_root_certs(
      cobalt_is_running_on_gke, cluster_settings)

  parser = argparse.ArgumentParser(description='The Cobalt command-line '
      'interface.')

  # Note(rudominer) A note about the handling of optional arguments here.
  # We create |parent_parser| and make it a parent of all of our sub parsers.
  # When we want to add a global optional argument (i.e. one that applies
  # to all sub-commands such as --verbose) we add the optional argument
  # to both |parent_parser| and |parser|. The reason for this is that
  # that appears to be the only way to get the help string  to show up both
  # when the top-level command is invoked and when
  # a sub-command is invoked.
  #
  # In other words when the user types:
  #
  #                python cobaltb.py -h
  #
  # and also when the user types
  #
  #                python cobaltb.py test -h
  #
  # we want to show the help for the --verbose option.
  parent_parser = argparse.ArgumentParser(add_help=False)

  # Even though the flag --production_dir was already parsed by parser0
  # above, we add the flag here too so that we can print help for it
  # and also so that the call to parser.parse_args() below will not complain
  # about --produciton_dir being unrecognized.
  production_dir_help = ("The path to a "
    "Cobalt production directory containing a 'cluster.json' file. The use of "
    "this flag in various commands implies that the command should be applied "
    "against the production Cobalt cluster associated with the specified "
    "directory. The contents of cluster.json will be used to set the "
    "default values for many of the other flags pertaining to production "
    "deployment. Additionally if there is a file named "
    "'bt_admin_service_account.json' in that directory then the environment "
    "variable GOOGLE_APPLICATION_CREDENTIALS will be set to the path to "
    "this file. Also the public and private key PEM files will be looked for "
    "in this directory. See README.md for details.")
  parser.add_argument('--production_dir', default='', help=production_dir_help)
  parent_parser.add_argument('--production_dir', default='',
      help=production_dir_help)

  parser.add_argument('--verbose',
    help='Be verbose (multiple times for more)',
    default=0, dest='verbose_count', action='count')
  parent_parser.add_argument('--verbose',
    help='Be verbose (multiple times for more)',
    default=0, dest='verbose_count', action='count')
  parser.add_argument('--vmodule',
    help='A string to use for the GLog -vmodule flag when running the Cobalt '
    'processes locally. Currently only used for the end-to-end test. '
    'Optional.)',
    default='')
  parent_parser.add_argument('--vmodule',
    help='A string to use for the GLog -vmodule flag when running the Cobalt'
    'processes locally. Currently only used for the end-to-end test. '
    'Optional.)',
    default='')

  subparsers = parser.add_subparsers()

  ########################################################
  # setup command
  ########################################################
  sub_parser = subparsers.add_parser('setup', parents=[parent_parser],
    help='Sets up the build environment.')
  sub_parser.set_defaults(func=_setup)

  ########################################################
  # update_config command
  ########################################################
  sub_parser = subparsers.add_parser('update_config',
    parents=[parent_parser], help="Pulls the current version Cobalt's config "
                                  "from its remote repo.")
  sub_parser.set_defaults(func=_update_config)

  ########################################################
  # write_prober_config command
  ########################################################
  sub_parser = subparsers.add_parser('write_prober_config',
                                     parents=[parent_parser],
                                     help="Copies the test_app2 config to the "
                                     "prober config, replacing the name of the "
                                     "custom metrics output log source.")
  sub_parser.add_argument('--config_dir', help="Path to the configuration "
                          "directory which should contain the prober config. "
                          "Default: %s"
                          % CONFIG_SUBMODULE_PATH,
                          default=CONFIG_SUBMODULE_PATH)
  sub_parser.set_defaults(func=_write_prober_config)

  ########################################################
  # check_config command
  ########################################################
  sub_parser = subparsers.add_parser('check_config',
    parents=[parent_parser], help="Check the validity of the cobalt "
                                  "configuration.")
  sub_parser.add_argument('--config_dir',
      help='Path to the configuration directory to be checked. Default: %s'
      % CONFIG_SUBMODULE_PATH,
      default=CONFIG_SUBMODULE_PATH)
  sub_parser.set_defaults(func=_check_config)

  ########################################################
  # build command
  ########################################################
  sub_parser = subparsers.add_parser('build', parents=[parent_parser],
    help='Builds Cobalt.')
  sub_parser.add_argument('--cmake_path', default='cmake',
                          help='Path to CMake binary')
  sub_parser.add_argument('--ninja_path', default='ninja',
                          help='Path to Ninja binary')
  sub_parser.set_defaults(func=_build)

  ########################################################
  # lint command
  ########################################################
  sub_parser = subparsers.add_parser('lint', parents=[parent_parser],
    help='Run language linters on all source files.')
  sub_parser.set_defaults(func=_lint)

  ########################################################
  # test command
  ########################################################
  sub_parser = subparsers.add_parser('test', parents=[parent_parser],
    help='Runs Cobalt tests. You must build first.')
  sub_parser.set_defaults(func=_test)
  sub_parser.add_argument('--tests', choices=TEST_FILTERS,
      help='Specify a subset of tests to run. Default=all',
      default='all')
  sub_parser.add_argument('-use_cloud_bt',
      help='Causes the end-to-end tests to run using local instances of the '
      'Cobalt processes connected to an instance of Cloud Bigtable. Otherwise '
      'a local instance of the Bigtable Emulator will be used.',
      action='store_true')
  sub_parser.add_argument('-cobalt_on_personal_cluster',
      help='Causes the end-to-end tests to run using the instance of Cobalt '
      'deployed on your personal GKE cluster. Otherwise local instances of the '
      'Cobalt processes are used. This option and -use_cloud_bt are mutually '
      'inconsistent. Do not use both at the same time. Also this option and '
      '--production_dir or mutually inconsistent because --production_dir '
      'implies that the end-to-end tests should be run against the specified '
      'production instance of Cobalt.',
      action='store_true')
  _add_cloud_access_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--bigtable_instance_id',
      help='Specify a Cloud Bigtable instance within the specified Cloud'
      ' project against which to run some of the tests.'
      ' Only used for the cloud_bt tests and e2e tests when either '
      '-use_cloud_bt or -cobalt_on_personal_cluster are specified.'
      ' default=%s'%cluster_settings['bigtable_instance_id'],
      default=cluster_settings['bigtable_instance_id'])
  sub_parser.add_argument('--shuffler_preferred_address',
      default=default_shuffler_preferred_address,
      help='Address of the form <host>:<port> to be used by the '
           'end-to-end test for connecting to the Shuffler. Optional. '
           'If not specified then the default port will be used and, when '
           'running locally, "localhost" will be used and when running on GKE '
           'the IP address of the Shuffler will be automatically discovered.'
           'default=%s'%default_shuffler_preferred_address)
  sub_parser.add_argument('--report_master_preferred_address',
      default=default_report_master_preferred_address,
      help='Address of the form <host>:<port> to be used by the '
           'end-to-end for connecting to the ReportMaster. Optional. '
           'If not specified then the default port will be used and, when '
           'running locally, "localhost" will be used and when running on GKE '
           'the IP address of the ReportMaster will be automatically '
           'discovered. '
           'default=%s'%default_report_master_preferred_address)
  sub_parser.add_argument('--use_tls',
      default=default_use_tls,
      help='Run end to end tests with tls enabled. '
      'default=%s'%default_use_tls)
  sub_parser.add_argument('--shuffler_root_certs',
      default=default_shuffler_root_certs,
      help='When --use_tls=true then use this as the root certs file '
      '(a.k.a the CA file) for the tls connection to the Shuffler. If not '
      'specified then gRPC defaults will be used. '
      'default=%s'%default_shuffler_root_certs)
  sub_parser.add_argument('--report_master_root_certs',
      default=default_report_master_root_certs,
      help='When --use_tls=true then use this as the root certs file '
      '(a.k.a the CA file) for the tls connection to the ReportMaster. If not '
      'specified then gRPC defaults will be used. '
      'default=%s'%default_report_master_root_certs)
  sub_parser.add_argument('--tls_cert_file',
      default=LOCALHOST_TLS_CERT_FILE,
      help='When --use_tls=true and the servers are being run locally, '
      'then use this as the server cert file. '
      'default=%s'%LOCALHOST_TLS_CERT_FILE)
  sub_parser.add_argument('--tls_key_file',
      default=LOCALHOST_TLS_KEY_FILE,
      help='When --use_tls=true and the servers are being run locally, '
      'then use this as the server private key file. '
      'default=%s'%LOCALHOST_TLS_KEY_FILE)

  ########################################################
  # clean command
  ########################################################
  sub_parser = subparsers.add_parser('clean', parents=[parent_parser],
    help='Deletes some or all of the build products.')
  sub_parser.set_defaults(func=_clean)
  sub_parser.add_argument('--full',
      help='Delete the entire "out" directory.',
      action='store_true')

  ########################################################
  # start command
  ########################################################
  start_parser = subparsers.add_parser('start',
    help='Start one of the Cobalt processes running locally.')
  start_subparsers = start_parser.add_subparsers()

  sub_parser = start_subparsers.add_parser('shuffler',
      parents=[parent_parser], help='Start the Shuffler running locally.')
  sub_parser.set_defaults(func=_start_shuffler)
  sub_parser.add_argument('--port',
      help='The port on which the Shuffler should listen. '
           'Default=%s.' % DEFAULT_SHUFFLER_PORT,
      default=DEFAULT_SHUFFLER_PORT)
  sub_parser.add_argument('--analyzer_uri',
      help='Default=localhost:%s'%DEFAULT_ANALYZER_SERVICE_PORT,
      default='localhost:%s'%DEFAULT_ANALYZER_SERVICE_PORT)
  sub_parser.add_argument('-use_memstore',
      help='Default: False, use persistent LevelDB Store.',
      action='store_true')
  sub_parser.add_argument('-keep_existing_db',
      help='When using LevelDB should any previously persisted data be kept? '
      'Default=False, erase the DB before starting the Shuffler.',
      action='store_true')
  sub_parser.add_argument('--config_file',
      help='Path to the Shuffler configuration file. '
           'Default=%s' % SHUFFLER_DEMO_CONFIG_FILE,
      default=SHUFFLER_DEMO_CONFIG_FILE)
  sub_parser.add_argument('--use_tls',
      default='false',
      help='Start the Shuffler with tls enabled. '
      'default=false')
  sub_parser.add_argument('--tls_cert_file',
      default=LOCALHOST_TLS_CERT_FILE,
      help='When --use_tls=true then use this as the cert file. '
      'default=%s'%LOCALHOST_TLS_CERT_FILE)
  sub_parser.add_argument('--tls_key_file',
      default=LOCALHOST_TLS_KEY_FILE,
      help='When --use_tls=true then use this as the server private key file. '
      'default=%s'%LOCALHOST_TLS_KEY_FILE)

  sub_parser = start_subparsers.add_parser('analyzer_service',
      parents=[parent_parser], help='Start the Analyzer Service running locally'
          ' and connected to a local instance of the Bigtable Emulator.')
  sub_parser.set_defaults(func=_start_analyzer_service)
  sub_parser.add_argument('-use_cloud_bt',
      help='Causes the Analyzer service to connect to an instance of Cloud '
           'Bigtable. Otherwise a local instance of the Bigtable Emulator will '
           'be used.', action='store_true')
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the name of the Cloud project containing the '
           'Bigtable instance to which the Analyzer service will connect. Only '
           'used if -use_cloud_bt is set. '
           'This is usually an organization domain name if your Cloud project '
           'is associated with one. Pass the empty string for no prefix. '
           'Default=%s.'%cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project containing the '
           'Bigtable instance to which the Analyzer service will connect. '
           'Only used if -use_cloud_bt is set. This is the full project '
           'name if --cloud_project_prefix is empty. Otherwise the full '
           'project name is <cloud_project_prefix>:<cloud_project_name>. '
           'default=%s'%cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--bigtable_instance_id',
      help='Specify a Cloud Bigtable instance within the specified Cloud '
      'project against which to query. Only used if -use_cloud_bt is set. '
      'default=%s'%cluster_settings['bigtable_instance_id'],
      default=cluster_settings['bigtable_instance_id'])
  sub_parser.add_argument('--port',
      help='The port on which the Analyzer service should listen. '
           'Default=%s.' % DEFAULT_ANALYZER_SERVICE_PORT,
      default=DEFAULT_ANALYZER_SERVICE_PORT,
      )

  sub_parser = start_subparsers.add_parser('report_master',
      parents=[parent_parser], help='Start the Analyzer ReportMaster Service '
          'running locally and connected to a local instance of the Bigtable'
          'Emulator.')
  sub_parser.set_defaults(func=_start_report_master)
  sub_parser.add_argument('-use_cloud_bt',
      help='Causes the Report Master to connect to an instance of Cloud '
           'Bigtable. Otherwise a local instance of the Bigtable Emulator will '
           'be used.', action='store_true')
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the name of the Cloud project containing the '
           'Bigtable instance to which the Report Master will connect. Only '
           'used if -use_cloud_bt is set. '
           'This is usually an organization domain name if your Cloud project '
           'is associated with one. Pass the empty string for no prefix. '
           'Default=%s.'%cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project containing the '
           'Bigtable instance to which the Report Master will connect. '
           'Only used if -use_cloud_bt is set. This is the full project '
           'name if --cloud_project_prefix is empty. Otherwise the full '
           'project name is <cloud_project_prefix>:<cloud_project_name>. '
           'default=%s'%cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--bigtable_instance_id',
      help='Specify a Cloud Bigtable instance within the specified Cloud '
      'project against which to query. Only used if -use_cloud_bt is set. '
      'default=%s'%cluster_settings['bigtable_instance_id'],
      default=cluster_settings['bigtable_instance_id'])
  sub_parser.add_argument('--port',
      help='The port on which the ReportMaster should listen. '
           'Default=%s.' % DEFAULT_REPORT_MASTER_PORT,
      default=DEFAULT_REPORT_MASTER_PORT)
  sub_parser.add_argument('--cobalt_config_dir',
      help='Path of directory containing Cobalt configuration files. '
           'Default=%s' % DEMO_CONFIG_DIR,
      default=DEMO_CONFIG_DIR)
  sub_parser.add_argument('--use_tls',
      default='false',
      help='Start the ReportMaster with tls enabled. '
      'default=False')
  sub_parser.add_argument('--tls_cert_file',
      default=LOCALHOST_TLS_CERT_FILE,
      help='When --use_tls=true then use this as the cert file. '
      'default=%s'%LOCALHOST_TLS_CERT_FILE)
  sub_parser.add_argument('--tls_key_file',
      default=LOCALHOST_TLS_KEY_FILE,
      help='When --use_tls=true then use this as the server private key file. '
      'default=%s'%LOCALHOST_TLS_KEY_FILE)

  sub_parser = start_subparsers.add_parser('test_app',
      parents=[parent_parser], help='Start the Cobalt test client app.')
  sub_parser.set_defaults(func=_start_test_app)
  sub_parser.add_argument('-automatic',
      help='Causes the test_app to run in automatic mode rather than '
      'interactive mode. It runs forever until killed sending Observations '
      'to the Shuffler.',
      action='store_true')
  sub_parser.add_argument('-cobalt_on_personal_cluster',
      help='Causes the test_app to run using an instance of Cobalt '
      'deployed in Google Container Engine. Otherwise local instances of the '
      'Cobalt processes are used.',
      action='store_true')
  default_cobalt_config_dir = _default_cobalt_config_dir(cluster_settings)
  sub_parser.add_argument('--cobalt_config_dir',
      help='Path of directory containing Cobalt configuration files. '
           'Default=%s' % default_cobalt_config_dir,
      default=default_cobalt_config_dir)
  _add_cloud_access_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--project_id',
    help='Specify the Cobalt project ID with which you wish to work. '
         'Must be in the range [1, 99]. Default = 1.',
    default=1)
  sub_parser.add_argument('--shuffler_preferred_address',
      default=default_shuffler_preferred_address,
      help='Address of the form <host>:<port> to be used by the '
           'test_app for connecting to the Shuffler. Optional. '
           'If not specified then the default port will be used and, when '
           'running locally, "localhost" will be used and when running on GKE '
           'the IP address of the Shuffler will be automatically discovered.'
           'default=%s'%default_shuffler_preferred_address)
  sub_parser.add_argument('--use_tls',
      default=default_use_tls,
      help='The test_app will use TLS to communicate with the Shuffler. '
      'default=%s'%default_use_tls)
  sub_parser.add_argument('--shuffler_root_certs',
      default=default_shuffler_root_certs,
      help='When --use_tls=true then use this as the root certs file '
      '(a.k.a the CA file) for the tls connection to the Shuffler. If not '
      'specified then gRPC defaults will be used. '
      'default=%s'%default_shuffler_root_certs)

  sub_parser = start_subparsers.add_parser('report_client',
      parents=[parent_parser], help='Start the Cobalt report client.')
  sub_parser.set_defaults(func=_start_report_client)
  _add_cloud_access_args(sub_parser, cluster_settings)
  sub_parser.add_argument('-cobalt_on_personal_cluster',
      help='Causes the report_client to query the instance of ReportMaster '
      'in your personal cluster rather than one running locally. If '
      '--production_dir is also specified it takes precedence and causes '
      'the report_client to query the instance of ReportMaster in the '
      'specified production cluster.',
      action='store_true')
  sub_parser.add_argument('--project_id',
    help='Specify the Cobalt project ID from which you wish to query. '
         'Default = 1.',
    default=1)
  sub_parser.add_argument('--report_master_preferred_address',
      default=default_report_master_preferred_address,
      help='Address of the form <host>:<port> to be used by the '
           'report_client for connecting to the ReportMaster. Optional. '
           'If not specified then the default port will be used and, when '
           'running locally, "localhost" will be used and when running on GKE '
           'the IP address of the ReportMaster will be automatically '
           'discovered. '
           'default=%s'%default_report_master_preferred_address)
  sub_parser.add_argument('--use_tls',
      default=default_use_tls,
      help='The test_app will use TLS to communicate with the Shuffler. '
      'default=%s'%default_use_tls)
  sub_parser.add_argument('--report_master_root_certs',
      default=default_report_master_root_certs,
      help='When --use_tls=true then use this as the root certs file '
      '(a.k.a the CA file) for the tls connection to the ReportMaster. If not '
      'specified then gRPC defaults will be used. '
      'default=%s'%default_report_master_root_certs)

  sub_parser = start_subparsers.add_parser('observation_querier',
      parents=[parent_parser], help='Start the Cobalt ObservationStore '
                                    'querying tool.')
  sub_parser.set_defaults(func=_start_observation_querier)
  sub_parser.add_argument('-use_cloud_bt',
      help='Causes the query to be performed against an instance of Cloud '
      'Bigtable. Otherwise a local instance of the Bigtable Emulator will be '
      'used.', action='store_true')
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the name of the Cloud project containing the '
           'Bigtable instance against which to '
           'query. Only used if -use_cloud_bt is set. This is '
           'usually an organization domain name if your Cloud project is '
           'associated with one. Pass the empty string for no prefix. '
           'Default=%s.'%cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project containing the '
           'Bigtable instance against which to '
           'query. Only used if -use_cloud_bt is set. This is the full project '
           'name if --cloud_project_prefix is empty. Otherwise the full '
           'project name is <cloud_project_prefix>:<cloud_project_name>. '
           'default=%s'%cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--bigtable_instance_id',
      help='Specify a Cloud Bigtable instance within the specified Cloud '
      'project against which to query. Only used if -use_cloud_bt is set. '
      'default=%s'%cluster_settings['bigtable_instance_id'],
      default=cluster_settings['bigtable_instance_id'])

  sub_parser = start_subparsers.add_parser('bigtable_emulator',
    parents=[parent_parser],
    help='Start the Bigtable Emulator running locally.')
  sub_parser.set_defaults(func=_start_bigtable_emulator)

  ########################################################
  # generate_keys command
  ########################################################

  sub_parser = subparsers.add_parser('generate_keys', parents=[parent_parser],
    help='Generate new public/private key pairs.')
  sub_parser.set_defaults(func=_generate_keys)

  ########################################################
  # generate_cert command
  ########################################################

  sub_parser = subparsers.add_parser('generate_cert', parents=[parent_parser],
    help='Generate a self-signed TLS key/cert pair. You must have '
    'openssl in your path.')
  sub_parser.set_defaults(func=_generate_cert)
  sub_parser.add_argument('--path-to-key',
      default='',
      help='Absolute or relative path to a private key file that should be '
      'generated. Required.')
  sub_parser.add_argument('--path-to-cert',
      default='',
      help='Absolute or relative path to a cert file that should be '
      'generted. Required.')
  sub_parser.add_argument('--ip-address', default='127.0.0.1',
    help='Optional IP address. If specified the self-signed certificate will '
    'include a Subject Alternative Name section containing the given IP '
    'address. This is necessary in order to use the IP address in gRPC '
    'requests.')
  sub_parser.add_argument('--hostname', default='localhost',
    help='Optional HostName. If specified the self-signed certificate will '
    'include a Subject Alternative Name section containing the hostname. '
    'This is necessary in order to use the hostname in gRPC '
    'requests.')

  ########################################################
  # bigtable command
  ########################################################
  bigtable_parser = subparsers.add_parser('bigtable',
    help='Perform an operation on your personal Cloud Bigtable cluster.')
  bigtable_subparsers = bigtable_parser.add_subparsers()

  sub_parser = bigtable_subparsers.add_parser('provision',
    parents=[parent_parser],
    help="Create Cobalt's Cloud BigTables if they don't exit.")
  sub_parser.set_defaults(func=_provision_bigtable)
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the name of the Cloud project containing the '
           'Bigtable instance to be provisioned. This is '
           'usually an organization domain name if your Cloud project is '
           'associated with one. Pass the empty string for no prefix. '
           'Default=%s.'%cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project containing the '
           'Bigtable instance to be provisioned. This is the full project '
           'name if --cloud_project_prefix is empty. Otherwise the full '
           'project name is <cloud_project_prefix>:<cloud_project_name>. '
           'default=%s'%cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--bigtable_instance_id',
    help='Specify the Cloud Bigtable instance within the specified Cloud'
    ' project that is to be provisioned. '
    'default=%s'%cluster_settings['bigtable_instance_id'],
    default=cluster_settings['bigtable_instance_id'])

  sub_parser = bigtable_subparsers.add_parser('delete_observations',
    parents=[parent_parser],
    help='**WARNING: Permanently delete data from Cobalt\'s Observation '
    'store. **')
  sub_parser.set_defaults(func=_delete_observations)
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the name of the Cloud project containing the '
           'Bigtable instance from which all Observation data will be '
           'permanently deleted. This is '
           'usually an organization domain name if your Cloud project is '
           'associated with one. Pass the empty string for no prefix. '
           'Default=%s.'%cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project containing the '
           'Bigtable instance from which all Observation data will be '
           'permanently deleted. This is the full project '
           'name if --cloud_project_prefix is empty. Otherwise the full '
           'project name is <cloud_project_prefix>:<cloud_project_name>. '
           'default=%s'%cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--bigtable_instance_id',
    help='Specify the Cloud Bigtable instance within the specified Cloud'
    ' project from which all Observation data will be permanently deleted. '
    'default=%s'%cluster_settings['bigtable_instance_id'],
    default=cluster_settings['bigtable_instance_id'])
  sub_parser.add_argument('--customer_id',
    help='Specify the Cobalt customer ID from which you wish to delete '
         'observations. Required.',
    default=0)
  sub_parser.add_argument('--project_id',
    help='Specify the Cobalt project ID from which you wish to delete '
         'observations. Required.',
    default=0)
  sub_parser.add_argument('--metric_id',
    help='Specify the Cobalt metric ID from which you wish to delete '
         'observations. Required.',
    default=0)

  sub_parser = bigtable_subparsers.add_parser('delete_reports',
    parents=[parent_parser],
    help='**WARNING: Permanently delete data from Cobalt\'s Report '
    'store. **')
  sub_parser.set_defaults(func=_delete_reports)
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the name of the Cloud project containing the '
           'Bigtable instance from which all Report data will be '
           'permanently deleted. This is '
           'usually an organization domain name if your Cloud project is '
           'associated with one. Pass the empty string for no prefix. '
           'Default=%s.'%cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project containing the '
           'Bigtable instance from which all Report data will be '
           'permanently deleted. This is the full project '
           'name if --cloud_project_prefix is empty. Otherwise the full '
           'project name is <cloud_project_prefix>:<cloud_project_name>. '
           'default=%s'%cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--bigtable_instance_id',
    help='Specify the Cloud Bigtable instance within the specified Cloud'
    ' project from which all Report data will be permanently deleted. '
    'default=%s'%cluster_settings['bigtable_instance_id'],
    default=cluster_settings['bigtable_instance_id'])
  sub_parser.add_argument('--customer_id',
    help='Specify the Cobalt customer ID from which you wish to delete '
         'reports. Required.',
    default=0)
  sub_parser.add_argument('--project_id',
    help='Specify the Cobalt project ID from which you wish to delete '
         'reports. Required.',
    default=0)
  sub_parser.add_argument('--report_config_id',
    help='Specify the Cobalt report config ID for which you wish to delete '
         'all report data. Required.',
    default=0)
  sub_parser.add_argument('-danger_danger_delete_production_reports',
    help='Overrides the default protection that prevents you from deleting '
    'reports from production projects.',
     action='store_true')

  ########################################################
  # deploy command
  ########################################################
  deploy_parser = subparsers.add_parser('deploy',
    parents=[parent_parser],
    help='Build Docker containers. Push to Container Regitry. Deploy to GKE.')
  deploy_subparsers = deploy_parser.add_subparsers()

  sub_parser = deploy_subparsers.add_parser('show',
      parents=[parent_parser], help='Display information about currently '
      'deployed jobs on GKE, including their public URIs.')
  sub_parser.set_defaults(func=_deploy_show)
  _add_gke_deployment_args(sub_parser, cluster_settings)

  sub_parser = deploy_subparsers.add_parser('login', parents=[parent_parser],
      help='Login to gcloud. Invoke if any command fails and asks you to login '
      'to gcloud.')
  sub_parser.set_defaults(func=_deploy_login)
  _add_gke_deployment_args(sub_parser, cluster_settings)

  sub_parser = deploy_subparsers.add_parser('authenticate',
      parents=[parent_parser], help='Refresh your authentication token if '
      'necessary. Also associates your local computer with a particular '
      'GKE cluster to which you will be deploying.')
  sub_parser.set_defaults(func=_deploy_authenticate)
  _add_cloud_access_args(sub_parser, cluster_settings)

  sub_parser = deploy_subparsers.add_parser('build',
      parents=[parent_parser], help='Rebuild all Docker images. '
          'You must have the Docker daemon running.')
  sub_parser.set_defaults(func=_deploy_build)
  _add_cloud_access_args(sub_parser, cluster_settings)
  default_shuffler_config_file = _default_shuffler_config_file(
      cluster_settings)
  sub_parser.add_argument('--shuffler_config_file',
      help='Path to the Shuffler configuration file. '
           'Default=%s' % default_shuffler_config_file,
      default=default_shuffler_config_file)
  default_cobalt_config_dir = _default_cobalt_config_dir(cluster_settings)
  sub_parser.add_argument('--cobalt_config_dir',
      help='Path of directory containing Cobalt configuration files. '
           'Default=%s' % default_cobalt_config_dir,
      default=default_cobalt_config_dir)

  sub_parser = deploy_subparsers.add_parser('production_build',
      parents=[parent_parser], help='Clean rebuild of binaries followed by '
           'a build and push of all Docker images.')
  sub_parser.set_defaults(func=_deploy_production_build)
  _add_cloud_access_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--shuffler_config_file',
      help='Path to the Shuffler configuration file. '
           'Default=%s' % default_shuffler_config_file,
      default=default_shuffler_config_file)
  sub_parser.add_argument('--cobalt_config_dir',
      help='Path of directory containing Cobalt configuration files. '
           'Default=%s' % default_cobalt_config_dir,
      default=default_cobalt_config_dir)
  sub_parser.add_argument('--git_revision',
      help='A git revision to build the binaries off of. If this is not '
      'provided, you will be prompted to select one from a list',
      default='')

  sub_parser = deploy_subparsers.add_parser('push',
      parents=[parent_parser], help='Push a Docker image to the Google'
          'Container Registry.')
  sub_parser.set_defaults(func=_deploy_push)
  sub_parser.add_argument('--job',
      help='The job you wish to push. Valid choices are "shuffler", '
           '"analyzer-service", "report-master". Required.')
  _add_gke_deployment_args(sub_parser, cluster_settings)

  sub_parser = deploy_subparsers.add_parser('start',
      parents=[parent_parser], help='Start one of the jobs on GKE.')
  sub_parser.set_defaults(func=_deploy_start)
  _add_gke_deployment_args(sub_parser, cluster_settings)
  _add_deploy_start_stop_args(sub_parser, cluster_settings, 'start')
  _add_deploy_start_args(sub_parser, cluster_settings)

  sub_parser = deploy_subparsers.add_parser('stop',
      parents=[parent_parser], help='Stop one of the jobs on GKE.')
  sub_parser.set_defaults(func=_deploy_stop)
  _add_gke_deployment_args(sub_parser, cluster_settings)
  _add_deploy_start_stop_args(sub_parser, cluster_settings, 'stop')

  sub_parser = deploy_subparsers.add_parser('stopstart',
      parents=[parent_parser], help='Stop and start a job on GKE.')
  sub_parser.set_defaults(func=_deploy_stopstart)
  _add_gke_deployment_args(sub_parser, cluster_settings)
  _add_deploy_start_stop_args(sub_parser, cluster_settings, 'stopstart')
  _add_deploy_start_args(sub_parser, cluster_settings)

  sub_parser = deploy_subparsers.add_parser('upload_secret_keys',
      parents=[parent_parser], help='Creates |secret| objects in the '
      'cluster to store the private keys for the Analyzer and the Shuffler. '
      'The private keys must first be generated using the "generate_keys" '
      'command (once for the Analyzer and once for the Shuffler). This must be '
      'done at least once before starting the Analyzer Service or the '
      'Shuffler. To replace the keys first delete the old ones using the '
      '"deploy delete_secret_keys" command.')
  sub_parser.set_defaults(func=_deploy_upload_secret_keys)
  _add_gke_deployment_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--analyzer_private_key_pem',
      default=analyzer_private_key_pem)
  sub_parser.add_argument('--shuffler_private_key_pem',
      default=shuffler_private_key_pem)

  sub_parser = deploy_subparsers.add_parser('delete_secret_keys',
      parents=[parent_parser], help='Deletes the |secret| objects in the '
      'cluster that were created using the "deploy upload_secret_keys" '
      'command.')
  sub_parser.set_defaults(func=_deploy_delete_secret_keys)
  _add_gke_deployment_args(sub_parser, cluster_settings)

  sub_parser = deploy_subparsers.add_parser('endpoint', parents=[parent_parser],
      help='Create or configure the Cloud Endpoint for one of Cobalt\'s job.')
  sub_parser.set_defaults(func=_deploy_endpoint)
  _add_cloud_project_args(sub_parser, cluster_settings)
  _add_static_ip_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--job',
      help='The job whose Cloud Endpoint you wish to configure. Valid choices: '
           '"report-master", "shuffler". Required.')

  sub_parser = deploy_subparsers.add_parser('addresses',
      parents=[parent_parser],
      help='Reserves static ip addresses for all the Cobalt jobs.')
  _add_cloud_access_args(sub_parser, cluster_settings)
  sub_parser.set_defaults(func=_deploy_addresses)

  sub_parser = deploy_subparsers.add_parser('upload_certificate',
      parents=[parent_parser],
      help='Deploy certificates and associated private keys')
  _add_gke_deployment_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--job',
      help='The job whose certificate you wish to deploy. Valid choices: '
           '"report-master", "shuffler". Required.')
  sub_parser.add_argument('--path-to-cert',
      help='Path to the certificate to be deployed.')
  sub_parser.add_argument('--path-to-key',
      help='Path to the private key to be deployed.')
  sub_parser.set_defaults(func=_deploy_upload_certificate)

  sub_parser = deploy_subparsers.add_parser('delete_certificate',
      parents=[parent_parser],
      help='Delete certificates and associated private keys')
  _add_gke_deployment_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--job',
      help='The job whose certificate you wish to delete. Valid choices: '
           '"report-master", "shuffler". Required.')
  sub_parser.set_defaults(func=_deploy_delete_certificate)

  sub_parser = deploy_subparsers.add_parser('upload_service_account_key',
      parents=[parent_parser], help='Creates a |secret| object in the '
      'cluster to store the private key for the ReportMaster\'s '
      'service account. This is the credential that allows the ReportMaster '
      'to export reports to Google Cloud Storage. The service account and '
      'private key must first be created manually through the Google Cloud '
      'console for the relevant project. The service account must separately '
      'be granted write permission to any Google Cloud Storage bucket to '
      'which Cobalt will export reports. Then the private key json file '
      'should be temporarily downloaded to your machine. This command is used '
      'to upload it as a Kubernetes secret. After it is uploaded the json '
      'file should be deleted from your local machine.')
  sub_parser.set_defaults(func=_deploy_upload_service_account_key)
  _add_gke_deployment_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--service_account_key_json',
      help='Path to the private key json file to upload. Required.')

  sub_parser = deploy_subparsers.add_parser('kube_ui',
      parents=[parent_parser], help='Launches the kubernetes ui.')
  sub_parser.set_defaults(func=_deploy_kube_ui)
  _add_gke_deployment_args(sub_parser, cluster_settings)

  args = parser.parse_args()
  global _verbose_count
  _verbose_count = args.verbose_count
  _initLogging(_verbose_count)
  global _vmodule
  _vmodule = args.vmodule

  # Add bin dirs from sysroot to the front of the path.
  os.environ["PATH"] = \
      "%s/bin" % SYSROOT_DIR \
      + os.pathsep + "%s/golang/bin" % SYSROOT_DIR \
      + os.pathsep + "%s/gcloud/google-cloud-sdk/bin" % SYSROOT_DIR \
      + os.pathsep + os.environ["PATH"]
  os.environ["LD_LIBRARY_PATH"] = "%s/lib" % SYSROOT_DIR

  global bt_admin_service_account_credentials_file
  bt_admin_service_account_credentials_file = \
      PERSONAL_BT_ADMIN_SERVICE_ACCOUNT_CREDENTIALS_FILE
  if args0.production_dir:
    bt_admin_service_account_credentials_file = os.path.abspath(
        os.path.join(args0.production_dir, 'bt_admin_service_account.json'))

  os.environ["GOOGLE_APPLICATION_CREDENTIALS"] = \
      bt_admin_service_account_credentials_file

  os.environ["GRPC_DEFAULT_SSL_ROOTS_FILE_PATH"] = GRPC_PEM_ROOTS

  os.environ["GOROOT"] = "%s/golang" % SYSROOT_DIR

  return args.func(args)


if __name__ == '__main__':
  sys.exit(main())

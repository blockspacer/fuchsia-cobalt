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

from __future__ import print_function

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

import tools.clang_tidy as clang_tidy
import tools.golint as golint
import tools.gnlint as gnlint
import tools.test_runner as test_runner
import tools.gitfmt as gitfmt

THIS_DIR = os.path.abspath(os.path.dirname(__file__))
SYSROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, 'sysroot'))
CONFIG_SUBMODULE_PATH = os.path.join(THIS_DIR, 'third_party', 'cobalt_config')

_logger = logging.getLogger()
_verbose_count = 0

# In Python3, the `raw_input` Built-in function was renamed to `input`.
try:
  input = raw_input
except NameError:
  pass


def _initLogging(verbose_count):
  """Ensures that the logger (obtained via logging.getLogger(), as usual) is

  initialized, with the log level set as appropriate for |verbose_count|
  instances of --verbose on the command line.
  """
  assert (verbose_count >= 0)
  if verbose_count == 0:
    level = logging.WARNING
  elif verbose_count == 1:
    level = logging.INFO
  else:  # verbose_count >= 2
    level = logging.DEBUG
  logging.basicConfig(format='%(relativeCreated).3f:%(levelname)s:%(message)s')
  logger = logging.getLogger()
  logger.setLevel(level)
  logger.debug('Initialized logging: verbose_count=%d, level=%d' %
               (verbose_count, level))


def ensureDir(dir_path):
  """Ensures that the directory at |dir_path| exists.

  If not it is created.

  Args:
    dir_path{string}: The path to a directory. If it does not exist it will be
      created.
  """
  if not os.path.exists(dir_path):
    os.makedirs(dir_path)


def out_dir(args):
  return os.path.abspath(os.path.join(THIS_DIR, args.out_dir))


def _setup(args):
  subprocess.check_call(['git', 'submodule', 'init'])
  subprocess.check_call(['git', 'submodule', 'update'])
  subprocess.check_call(['./setup.sh'])


def _update_config(args):
  savedDir = os.getcwd()
  try:
    os.chdir(CONFIG_SUBMODULE_PATH)
    subprocess.check_call(['git', 'pull', 'origin', 'master'])
  finally:
    os.chdir(savedDir)


def _compdb(args):
  # Copy the compile_commands.json to the top level for use in IDEs (CLion).
  subprocess.check_call([
      'cp',
      '%s/compile_commands.json' % out_dir(args), './compile_commands.json'
  ])
  # Remove the gomacc references that confuse IDEs.
  subprocess.check_call([
      'perl', '-p', '-i', '-e', 's|/[/\w]+/gomacc *||',
      './compile_commands.json'
  ])


def _build(args):
  gn_args = []
  use_ccache = False
  goma_dir = os.path.expanduser('~/goma')
  if args.goma_dir:
    goma_dir = args.goma_dir
  if args.ccache:
    use_ccache = True
  if args.no_ccache:
    use_ccache = False

  use_goma = os.path.exists(goma_dir)

  if args.no_goma:
    use_goma = False

  if args.release:
    gn_args.append('is_debug=false')
  if use_goma:
    gn_args.append('use_goma=true')
    gn_args.append('goma_dir=\"%s\"' % goma_dir)
  elif use_ccache:
    gn_args.append('use_ccache=true')

  if args.args != '':
    gn_args.append(args.args)

  if vars(args)['with']:
    packages = 'extra_package_labels=['
    for target in vars(args)['with']:
      packages += "\"%s\"," % target
    packages += ']'

    gn_args.append(packages)

  # If goma isn't running, start it.
  if use_goma:
    start_goma = False
    try:
      if not subprocess.check_output(['%s/gomacc' % goma_dir, 'port'
                                     ]).strip().isdigit():
        start_goma = True
    except subprocess.CalledProcessError:
      start_goma = True
    if start_goma:
      subprocess.check_call(['%s/goma_ctl.py' % goma_dir, 'ensure_start'])

  subprocess.check_call([
      args.gn_path,
      'gen',
      out_dir(args),
      '--check',
      '--export-compile-commands=default',
      '--args=%s' % ' '.join(gn_args),
  ])

  subprocess.check_call([args.ninja_path, '-C', out_dir(args)])


def _check_config(args):
  config_parser_bin = os.path.join(out_dir(args), 'config_parser')
  if not os.path.isfile(config_parser_bin):
    print(
        '%s could not be found. Run \n\n%s setup\n%s build\n\nand try again.' %
        (config_parser_bin, sys.argv[0], sys.argv[0]))
    return
  if not _check_test_configs(args):
    return
  subprocess.check_call(
      [config_parser_bin, '-config_dir', args.config_dir, '-check_only'])


def _check_test_configs(args):
  testapp_config_path = os.path.join(args.config_dir, 'fuchsia', 'test_app2',
                                     'metrics.yaml')
  if not _check_config_exists(testapp_config_path):
    return False

  prober_config_path = os.path.join(args.config_dir, 'fuchsia', 'prober',
                                    'metrics.yaml')
  if not _check_config_exists(prober_config_path):
    print('Run this command and try again: ./cobaltb.py write_prober_config')
    return False

  _, tmp_path = tempfile.mkstemp()
  _make_prober_config(testapp_config_path, tmp_path)
  is_same_file = filecmp.cmp(tmp_path, prober_config_path)
  os.remove(tmp_path)
  if not is_same_file:
    print('Testapp config and prober config should be identical except for '
          'names of custom metrics output log types.\n'
          'Run this command and try again: ./cobaltb.py write_prober_config')
  return is_same_file


def _write_prober_config(args):
  testapp_config_path = os.path.join(args.config_dir, 'fuchsia', 'test_app2',
                                     'metrics.yaml')
  if not _check_config_exists(testapp_config_path):
    return False
  prober_config_path = os.path.join(args.config_dir, 'fuchsia', 'prober',
                                    'metrics.yaml')
  if os.path.isfile(prober_config_path):
    print('This action will overwrite the file %s.' % prober_config_path)
    answer = input('Continue anyway? (y/N) ')
    if not _parse_bool(answer):
      return
  prober_dir = os.path.dirname(prober_config_path)
  if not os.path.exists(prober_dir):
    os.makedirs(prober_dir)
  _make_prober_config(testapp_config_path, prober_config_path)


def _check_config_exists(config_path):
  if not os.path.isfile(config_path):
    print('Expected config at path %s' % config_path)
    return False
  return True


def _make_prober_config(testapp_config_path, output_path):
  testapp_custom_log_source = 'processed/<team_name>-cobalt-dev:custom-proto-test'
  prober_custom_log_source = 'processed/<team_name>-cobalt-dev:custom-proto-prober-test'

  with open(testapp_config_path, 'r') as f:
    testapp_config = f.read()

  with open(output_path, 'w') as f:
    f.write(
        testapp_config.replace(testapp_custom_log_source,
                               prober_custom_log_source))


def _fmt(args):
  gitfmt.fmt(args.committed, args.all)


def _lint(args):
  status = 0
  failure_list = []

  result = clang_tidy.main(args.directory)
  failure_list.append(('clang_tidy', result))
  status += result

  result = golint.main()
  failure_list.append(('golint', result))
  status += result

  result = gnlint.main()
  failure_list.append(('gnlint', result))
  status += result


  if status > 0:
    print('')
    print('******************* SOME LINTERS FAILED *******************')
    for linter, result in failure_list:
      print('%s returned: %s' % (linter, result))
  else:
    print('All linters passed')

  exit(status)


# Specifiers of subsets of tests to run
TEST_FILTERS = ['all', 'cpp', 'nocpp', 'go', 'nogo', 'perf', 'perf', 'other']


# Returns 0 if all tests pass, otherwise returns 1. Prints a failure or success
# message.
def _test(args):
  # A map from positive filter specifiers to the list of test directories
  # it represents. Note that 'cloud_bt' and 'perf' tests are special. They are
  # not included in 'all'. They are only run if asked for explicitly.
  FILTER_MAP = {
      'all': ['cpp', 'go', 'other'],
      'cpp': ['cpp'],
      'go': ['go'],
      'perf': ['perf'],
      'other': ['other'],
  }

  # By default try each test just once.
  num_times_to_try = 1

  # Get the list of test directories we should run.
  if args.tests.startswith('no'):
    test_dirs = [
        test_dir for test_dir in FILTER_MAP['all']
        if test_dir not in FILTER_MAP[args.tests[2:]]
    ]
  else:
    test_dirs = FILTER_MAP[args.tests]

  failure_list = []
  print('Will run tests in the following directories: %s.' %
        ', '.join(test_dirs))

  bigtable_project_name = ''
  bigtable_instance_id = ''
  for test_dir in test_dirs:
    test_args = None
    print('********************************************************')
    this_failure_list = []
    for attempt in range(num_times_to_try):
      this_failure_list = test_runner.run_all_tests(
          'tests/' + test_dir,
          verbose_count=_verbose_count,
          vmodule=_vmodule,
          test_args=test_args)
      if this_failure_list and attempt < num_times_to_try - 1:
        print('')
        print('***** Attempt %i of %s failed. Retrying...' %
              (attempt, this_failure_list))
        print('')
      else:
        break
    if this_failure_list:
      failure_list.append('%s (%s)' % (test_dir, this_failure_list))

  print('')
  if failure_list:
    print('******************* SOME TESTS FAILED *******************')
    print('failures = %s' % failure_list)
    return 1
  else:
    print('******************* ALL TESTS PASSED *******************')
    return 0


# Files and directories in the out directory to NOT delete when doing
# a partial clean.
TO_SKIP_ON_PARTIAL_CLEAN = {
    'obj': {
        'third_party': True
    },
    'gen': {
        'third_party': True
    },
    '.ninja_deps': True,
    '.ninja_log': True,
    'build.ninja': True,
    'rules.ninja': True,
    'args.gn': True,
}


def partial_clean(current_dir, exceptions):
  for f in os.listdir(current_dir):
    full_path = os.path.join(current_dir, f)
    if not f in exceptions:
      if os.path.isfile(full_path):
        os.remove(full_path)
      else:
        shutil.rmtree(full_path, ignore_errors=True)
    elif isinstance(exceptions[f], dict):
      partial_clean(full_path, exceptions[f])
    else:
      print('Skipping', full_path)


def _clean(args):
  if args.full:
    print('Deleting the out directory...')
    shutil.rmtree(out_dir(args), ignore_errors=True)
  else:
    print('Doing a partial clean. Pass --full for a full clean.')
    if not os.path.exists(out_dir(args)):
      return
    partial_clean(out_dir(args), TO_SKIP_ON_PARTIAL_CLEAN)


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


def _default_shuffler_config_file(cluster_settings):
  if cluster_settings['shuffler_config_file']:
    return os.path.join(THIS_DIR, cluster_settings['shuffler_config_file'])
  return SHUFFLER_DEMO_CONFIG_FILE


def _default_cobalt_config_dir(cluster_settings):
  if cluster_settings['cobalt_config_dir']:
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


def _default_report_master_root_certs(cobalt_is_running_on_gke,
                                      cluster_settings):
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
      the json file that do not correspond to a key in this dictionary will be
      ignored.
    json_file_path: The full path to a json file that must exist.
  """
  print('The GKE cluster settings file being used is: %s.' % json_file_path)
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
  parser.add_argument(
      '--cloud_project_prefix',
      help='The prefix part of name of the Cloud project with which you wish '
      'to work. This is usually an organization domain name if your '
      'Cloud project is associated with one. Pass the empty string for no '
      'prefix. '
      'Default=%s.' % cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  parser.add_argument(
      '--cloud_project_name',
      help='The main part of the name of the Cloud project with which you wish '
      'to work. This is the full project name if --cloud_project_prefix '
      'is empty. Otherwise the full project name is '
      '<cloud_project_prefix>:<cloud_project_name>. '
      'Default=%s' % cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])


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
    print('Only linux is supported!')
    return 1
  # We parse the command line flags twice. The first time we are looking
  # only for two particular flags, namely --production_dir and
  # --cobalt_on_personal_cluster. This first pass
  # will not print any help and will ignore all other flags.
  parser0 = argparse.ArgumentParser(add_help=False)
  parser0.add_argument('--production_dir', default='')
  parser0.add_argument('-cobalt_on_personal_cluster', action='store_true')
  args0, ignore = parser0.parse_known_args()

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

  parser.add_argument(
      '--verbose',
      help='Be verbose (multiple times for more)',
      default=0,
      dest='verbose_count',
      action='count')
  parent_parser.add_argument(
      '--verbose',
      help='Be verbose (multiple times for more)',
      default=0,
      dest='verbose_count',
      action='count')
  parser.add_argument(
      '--vmodule',
      help='A string to use for the GLog -vmodule flag when running the Cobalt '
      'processes locally. Currently only used for the end-to-end test. '
      'Optional.)',
      default='')
  parent_parser.add_argument(
      '--vmodule',
      help='A string to use for the GLog -vmodule flag when running the Cobalt'
      'processes locally. Currently only used for the end-to-end test. '
      'Optional.)',
      default='')
  parser.add_argument(
      '--out_dir',
      help='Output directory (relative to cobaltb.py)',
      default='out')
  parent_parser.add_argument(
      '--out_dir',
      help='Output directory (relative to cobaltb.py)',
      default='out')

  subparsers = parser.add_subparsers()

  ########################################################
  # setup command
  ########################################################
  sub_parser = subparsers.add_parser(
      'setup', parents=[parent_parser], help='Sets up the build environment.')
  sub_parser.set_defaults(func=_setup)

  ########################################################
  # update_config command
  ########################################################
  sub_parser = subparsers.add_parser(
      'update_config',
      parents=[parent_parser],
      help="Pulls the current version Cobalt's config "
      'from its remote repo.')
  sub_parser.set_defaults(func=_update_config)

  ########################################################
  # write_prober_config command
  ########################################################
  sub_parser = subparsers.add_parser(
      'write_prober_config',
      parents=[parent_parser],
      help='Copies the test_app2 config to the '
      'prober config, replacing the name of the '
      'custom metrics output log source.')
  sub_parser.add_argument(
      '--config_dir',
      help='Path to the configuration '
      'directory which should contain the prober config. '
      'Default: %s' % CONFIG_SUBMODULE_PATH,
      default=CONFIG_SUBMODULE_PATH)
  sub_parser.set_defaults(func=_write_prober_config)

  ########################################################
  # check_config command
  ########################################################
  sub_parser = subparsers.add_parser(
      'check_config',
      parents=[parent_parser],
      help='Check the validity of the cobalt '
      'configuration.')
  sub_parser.add_argument(
      '--config_dir',
      help='Path to the configuration directory to be checked. Default: %s' %
      CONFIG_SUBMODULE_PATH,
      default=CONFIG_SUBMODULE_PATH)
  sub_parser.set_defaults(func=_check_config)

  ########################################################
  # build command
  ########################################################
  sub_parser = subparsers.add_parser(
      'build', parents=[parent_parser], help='Builds Cobalt.')
  sub_parser.add_argument('--gn_path', default='gn', help='Path to GN binary')
  sub_parser.add_argument(
      '--ninja_path', default='ninja', help='Path to Ninja binary')
  sub_parser.add_argument(
      '--args', default='', help='Additional arguments to pass to gn')
  sub_parser.add_argument(
      '--ccache', action='store_true', help='The build should use ccache')
  sub_parser.add_argument(
      '--no-ccache',
      action='store_true',
      help='The build should not use ccache')
  sub_parser.add_argument(
      '--no-goma',
      action='store_true',
      help='The build should not use goma. Otherwise goma is used if found.')
  sub_parser.add_argument(
      '--goma_dir',
      default='',
      help='The dir where goma is installed (defaults to ~/goma')
  sub_parser.add_argument(
      '--release', action='store_true', help='Should build release build')
  sub_parser.add_argument(
      '--with', action='append', help='Additional packages to build')
  sub_parser.set_defaults(func=_build)

  ########################################################
  # lint command
  ########################################################
  sub_parser = subparsers.add_parser(
      'lint',
      parents=[parent_parser],
      help='Run language linters on all source files.')
  sub_parser.add_argument(
      '--all',
      action='store_true',
      default=False,
      help='Currently does nothing, soon will force linting all files.')
  sub_parser.add_argument('directory', nargs='*')
  sub_parser.set_defaults(func=_lint)

  ########################################################
  # fmt command
  ########################################################
  sub_parser = subparsers.add_parser(
      'fmt',
      parents=[parent_parser],
      help='Run language formatter on modified files.')
  sub_parser.add_argument(
      '--committed',
      action='store_true',
      default=False,
      help='Also run on files modified in the latest commit.')
  sub_parser.add_argument(
      '--all',
      action='store_true',
      default=False,
      help='Run on all tracked files.')
  sub_parser.set_defaults(func=_fmt)

  ########################################################
  # test command
  ########################################################
  sub_parser = subparsers.add_parser(
      'test',
      parents=[parent_parser],
      help='Runs Cobalt tests. You must build first.')
  sub_parser.set_defaults(func=_test)
  sub_parser.add_argument(
      '--tests',
      choices=TEST_FILTERS,
      help='Specify a subset of tests to run. Default=all',
      default='all')

  ########################################################
  # clean command
  ########################################################
  sub_parser = subparsers.add_parser(
      'clean',
      parents=[parent_parser],
      help='Deletes some or all of the build products.')
  sub_parser.set_defaults(func=_clean)
  sub_parser.add_argument(
      '--full', help='Delete the entire "out" directory.', action='store_true')

  ########################################################
  # compdb command
  ########################################################
  sub_parser = subparsers.add_parser(
      'compdb',
      parents=[parent_parser],
      help=('Generate a compilation database for the current build '
            'configuration.'))
  sub_parser.set_defaults(func=_compdb)

  args = parser.parse_args()
  global _verbose_count
  _verbose_count = args.verbose_count
  _initLogging(_verbose_count)
  global _vmodule
  _vmodule = args.vmodule

  # Add bin dirs from sysroot to the front of the path.
  os.environ['PATH'] = \
      '%s/bin' % SYSROOT_DIR \
      + os.pathsep + '%s/golang/bin' % SYSROOT_DIR \
      + os.pathsep + os.environ['PATH']
  os.environ['LD_LIBRARY_PATH'] = '%s/lib' % SYSROOT_DIR

  os.environ['GOROOT'] = '%s/golang' % SYSROOT_DIR

  # Until Python3.7 adds the 'required' flag for subparsers, an error occurs
  # when running without specifying a subparser on the command line:
  # https://bugs.python.org/issue16308
  # Work around the issue by checking whether the 'func' attribute has been
  # set.
  try:
    a = getattr(args, 'func')
  except AttributeError:
    parser.print_usage()
    sys.exit(0)
  return args.func(args)


if __name__ == '__main__':
  sys.exit(main())

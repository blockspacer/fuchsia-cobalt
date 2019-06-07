#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors
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
"""A library with functions to start each of the Cobalt processes locally."""

from __future__ import print_function

import os
import shutil
import subprocess

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))
OUT_DIR = os.path.join(SRC_ROOT_DIR, "out")
SYS_ROOT_DIR = os.path.join(SRC_ROOT_DIR, "sysroot")

DEMO_CONFIG_DIR = os.path.abspath(os.path.join(SRC_ROOT_DIR, "config", "demo"))
PRODUCTION_CONFIG_DIR = os.path.abspath(
    os.path.join(SRC_ROOT_DIR, "config", "production"))
CONFIG_BINARY_PROTO = os.path.join(OUT_DIR, "third_party", "config",
                                   "cobalt_config.binproto")
SHUFFLER_DEMO_CONFIG_FILE = os.path.abspath(
    os.path.join(SRC_ROOT_DIR, "shuffler", "src", "config", "config_demo.txt"))
SHUFFLER_DB_DIR = os.path.join("/tmp/cobalt_shuffler")

SHUFFLER_CONFIG_DIR = os.path.abspath(
    os.path.join(SRC_ROOT_DIR, "shuffler", "src", "shuffler_config"))
SHUFFLER_CONFIG_FILE = os.path.join(SHUFFLER_CONFIG_DIR, "config_v0.txt")
SHUFFLER_DEMO_CONFIG_FILE = os.path.join(SHUFFLER_CONFIG_DIR, "config_demo.txt")
SHUFFLER_TMP_DB_DIR = os.path.join("/tmp/cobalt_shuffler")

DEFAULT_SHUFFLER_PORT = 5001
DEFAULT_ANALYZER_SERVICE_PORT = 6001
DEFAULT_REPORT_MASTER_PORT = 7001

DEFAULT_ANALYZER_PUBLIC_KEY_PEM = os.path.join(SRC_ROOT_DIR,
                                               "analyzer_public.pem")
ANALYZER_PRIVATE_KEY_PEM_NAME = "analyzer_private.pem"
DEFAULT_ANALYZER_PRIVATE_KEY_PEM = os.path.join(SRC_ROOT_DIR,
                                                ANALYZER_PRIVATE_KEY_PEM_NAME)
DEFAULT_SHUFFLER_PUBLIC_KEY_PEM = os.path.join(SRC_ROOT_DIR,
                                               "shuffler_public.pem")
SHUFFLER_PRIVATE_KEY_PEM_NAME = "shuffler_private.pem"
DEFAULT_SHUFFLER_PRIVATE_KEY_PEM = os.path.join(SRC_ROOT_DIR,
                                                SHUFFLER_PRIVATE_KEY_PEM_NAME)

LOCALHOST_TLS_CERT_FILE = os.path.join(SRC_ROOT_DIR, "end_to_end_tests",
                                       "localhost.crt")
LOCALHOST_TLS_KEY_FILE = os.path.join(SRC_ROOT_DIR, "end_to_end_tests",
                                      "localhost.key")


def kill_process(process, name):
  """ Kills the given process if it is running and waits for it to terminate.

  Args:
    process {Popen}: A representation of the process to be killed. May be None.
    name {String}: Name of the process for use in a user-facing message.
  """
  if process and process.poll() is None:
    print("Killing %s..." % name)
    process.terminate()
    process.kill()
    process.wait()


def execute_command(cmd, wait):
  """ Executes the given command and optionally waits for it to complete.

  Args:
    cmd {list of strings}: will be passed to Popen().
    wait {bool}: If true we will wait for the command to complete and return the
      result code. If false we will return immediately and return an instance of
      Popen.

  Returns:
    An instance of Popen if wait is false or an integer return code if
    wait is true.
  """
  p = subprocess.Popen(cmd)
  if not wait:
    return p
  return_code = p.wait()
  if return_code < 0:
    print("")
    print("****** WARNING Process [%s] terminated by signal %d" %
          (cmd[0], -return_code))
  return return_code


def start_bigtable_emulator(wait=True):
  # Note(rudominer) We can pass -port=n to cbtemulator to run on a different
  # port.
  print("")
  print("Starting the Cloud Bigtable Emulator...")
  print("")
  path = os.path.abspath(
      os.path.join(SYS_ROOT_DIR, "gcloud", "google-cloud-sdk", "platform",
                   "bigtable-emulator", "cbtemulator"))
  cmd = [path]
  return execute_command(cmd, wait)


SHUFFLER_PATH = os.path.abspath(os.path.join(OUT_DIR, "shuffler", "shuffler"))


def start_shuffler(port=DEFAULT_SHUFFLER_PORT,
                   analyzer_uri="localhost:%d" % DEFAULT_ANALYZER_SERVICE_PORT,
                   use_memstore=False,
                   erase_db=True,
                   db_dir=SHUFFLER_TMP_DB_DIR,
                   config_file=SHUFFLER_DEMO_CONFIG_FILE,
                   private_key_pem_file=DEFAULT_SHUFFLER_PRIVATE_KEY_PEM,
                   use_tls=False,
                   tls_cert_file=LOCALHOST_TLS_CERT_FILE,
                   tls_key_file=LOCALHOST_TLS_KEY_FILE,
                   verbose_count=0,
                   wait=True):
  """Starts the Shuffler.

  Args:
    port {int}: The port on which the Shuffler should listen.
    analyzer_uri {string}: The URI of the Analyzer Service
    use_memstore {bool}: If false the Shuffler will use the LevelDB
    store emtpy_db {bool}: When using the LevelDB store, should the store be
      erased before the shuffler starts?
    config_file {string}: The path to the Shuffler's config file.
  """
  print("")
  cmd = [
      SHUFFLER_PATH, "-port",
      str(port), "-private_key_pem_file", private_key_pem_file, "-analyzer_uri",
      analyzer_uri, "-config_file", config_file, "-logtostderr"
  ]
  if use_tls:
    cmd.append("-tls")
    cmd.append("-cert_file=%s" % tls_cert_file)
    cmd.append("-key_file=%s" % tls_key_file)
  if verbose_count > 0:
    cmd.append("-v=%d" % verbose_count)
  if use_memstore:
    cmd.append("-use_memstore")
  else:
    cmd = cmd + ["-db_dir", db_dir]
    if erase_db:
      print("Erasing Shuffler's LevelDB store at %s." % db_dir)
      shutil.rmtree(db_dir, ignore_errors=True)

  print("Starting the shuffler...")
  print("")
  return execute_command(cmd, wait)


ANALYZER_SERVICE_PATH = os.path.abspath(
    os.path.join(OUT_DIR, "analyzer", "analyzer_service", "analyzer_service"))


def start_analyzer_service(
    port=DEFAULT_ANALYZER_SERVICE_PORT,
    bigtable_project_name="",
    bigtable_instance_id="",
    private_key_pem_file=DEFAULT_ANALYZER_PRIVATE_KEY_PEM,
    verbose_count=0,
    vmodule=None,
    wait=True):
  print("")
  print("Starting the analyzer service...")
  print("")
  cmd = [
      ANALYZER_SERVICE_PATH, "-port",
      str(port), "-private_key_pem_file", private_key_pem_file, "-logtostderr"
  ]
  if bigtable_project_name != "" and bigtable_instance_id != "":
    cmd = cmd + [
        "-bigtable_project_name",
        bigtable_project_name,
        "-bigtable_instance_id",
        bigtable_instance_id,
    ]
  else:
    print("Will connect to a local Bigtable Emulator instance.")
    cmd.append("-for_testing_only_use_bigtable_emulator")
  if verbose_count > 0:
    cmd.append("-v=%d" % verbose_count)
  if vmodule:
    cmd.append("-vmodule=%s" % vmodule)
  return execute_command(cmd, wait)


REPORT_MASTER_PATH = os.path.abspath(
    os.path.join(OUT_DIR, "analyzer", "report_master",
                 "analyzer_report_master"))


def start_report_master(port=DEFAULT_REPORT_MASTER_PORT,
                        bigtable_project_name="",
                        bigtable_instance_id="",
                        cobalt_registry_proto_path=CONFIG_BINARY_PROTO,
                        use_tls=False,
                        tls_cert_file=LOCALHOST_TLS_CERT_FILE,
                        tls_key_file=LOCALHOST_TLS_KEY_FILE,
                        verbose_count=0,
                        vmodule=None,
                        wait=True):
  print("")
  print("Starting the analyzer ReportMaster service...")
  print("")
  cmd = [
      REPORT_MASTER_PATH, "-port",
      str(port), "-cobalt_registry_proto_path", cobalt_registry_proto_path,
      "-logtostderr"
  ]
  if use_tls:
    cmd.append("-use_tls")
    cmd.append("-tls_cert_file=%s" % tls_cert_file)
    cmd.append("-tls_key_file=%s" % tls_key_file)
  if bigtable_project_name != "" and bigtable_instance_id != "":
    cmd = cmd + [
        "-bigtable_project_name",
        bigtable_project_name,
        "-bigtable_instance_id",
        bigtable_instance_id,
    ]
  else:
    print("Will connect to a local Bigtable Emulator instance.")
    cmd.append("-for_testing_only_use_bigtable_emulator")
  if verbose_count > 0:
    cmd.append("-v=%d" % verbose_count)
  if vmodule:
    cmd.append("-vmodule=%s" % vmodule)
  return execute_command(cmd, wait)


TEST_APP_PATH = os.path.abspath(
    os.path.join(OUT_DIR, "tools", "test_app", "cobalt_test_app"))


def start_test_app(shuffler_uri="",
                   analyzer_uri="",
                   use_tls=False,
                   root_certs_pem_file="",
                   analyzer_pk_pem_file=DEFAULT_ANALYZER_PUBLIC_KEY_PEM,
                   shuffler_pk_pem_file=DEFAULT_SHUFFLER_PUBLIC_KEY_PEM,
                   cobalt_registry_proto_path=CONFIG_BINARY_PROTO,
                   project_id=1,
                   automatic=False,
                   verbose_count=0,
                   wait=True):
  cmd = [
      TEST_APP_PATH, "-shuffler_uri", shuffler_uri, "-analyzer_uri",
      analyzer_uri, "-analyzer_pk_pem_file", analyzer_pk_pem_file,
      "-shuffler_pk_pem_file", shuffler_pk_pem_file, "-config_bin_proto_path",
      cobalt_registry_proto_path, "-project",
      str(project_id), "-logtostderr"
  ]
  if (use_tls):
    cmd.append("-use_tls")
    if (root_certs_pem_file):
      cmd.append("-root_certs_pem_file")
      cmd.append(root_certs_pem_file)
  if verbose_count > 0:
    cmd.append("-v=%d" % verbose_count)
  if automatic:
    cmd.append("-mode=automatic")
  return execute_command(cmd, wait)


def start_report_client(report_master_uri="",
                        use_tls=False,
                        root_certs_pem_file=LOCALHOST_TLS_CERT_FILE,
                        project_id=1,
                        verbose_count=0,
                        wait=True):
  path = os.path.abspath(os.path.join(OUT_DIR, "tools", "report_client"))
  cmd = [
      path, "-report_master_uri", report_master_uri, "-project_id",
      str(project_id), "-logtostderr"
  ]
  if (use_tls):
    cmd.append("-tls")
    if (root_certs_pem_file):
      cmd.append("-ca_file")
      cmd.append(root_certs_pem_file)
  if verbose_count > 0:
    cmd.append("-v=%d" % verbose_count)
  return execute_command(cmd, wait)


OBSERVATION_QUERIER_PATH = os.path.abspath(
    os.path.join(OUT_DIR, "tools", "observation_querier", "query_observations"))


def start_observation_querier(bigtable_project_name="",
                              bigtable_instance_id="",
                              verbose_count=0):
  cmd = [OBSERVATION_QUERIER_PATH, "-logtostderr"]
  if not bigtable_project_name or not bigtable_instance_id:
    cmd.append("-for_testing_only_use_bigtable_emulator")
  else:
    cmd = cmd + [
        "-bigtable_project_name", bigtable_project_name,
        "-bigtable_instance_id", bigtable_instance_id
    ]
  if verbose_count > 0:
    cmd.append("-v=%d" % verbose_count)
  return execute_command(cmd, wait=True)

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
"""A library with functions to help work with Docker, Kubernetes and GKE."""

import fileinput
import json
import os
import re
import shutil
import string
import subprocess
import sys
import tempfile

import process_starter
from process_starter import ANALYZER_PRIVATE_KEY_PEM_NAME
from process_starter import ANALYZER_SERVICE_PATH
from process_starter import DEFAULT_ANALYZER_SERVICE_PORT
from process_starter import DEFAULT_REPORT_MASTER_PORT
from process_starter import DEFAULT_SHUFFLER_PORT
from process_starter import DEMO_CONFIG_DIR
from process_starter import REPORT_MASTER_PATH
from process_starter import SHUFFLER_PRIVATE_KEY_PEM_NAME
from process_starter import SHUFFLER_CONFIG_FILE
from process_starter import SHUFFLER_PATH

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.join(THIS_DIR, os.pardir)
OUT_DIR = os.path.abspath(os.path.join(SRC_ROOT_DIR, 'out'))
SYS_ROOT_DIR = os.path.join(SRC_ROOT_DIR, 'sysroot')

# The URI of the Google Container Registry.
CONTAINER_REGISTRY_URI = 'us.gcr.io'

# Dockerfile/Kubernetes source file paths
KUBE_SRC_DIR = os.path.join(SRC_ROOT_DIR, 'kubernetes')
COBALT_COMMON_DOCKER_FILE = os.path.join(KUBE_SRC_DIR, 'cobalt_common',
                                         'Dockerfile')
ANALYZER_SERVICE_DOCKER_FILE = os.path.join(KUBE_SRC_DIR, 'analyzer_service',
                                            'Dockerfile')
REPORT_MASTER_DOCKER_FILE = os.path.join(KUBE_SRC_DIR, 'report_master',
                                         'Dockerfile')
SHUFFLER_DOCKER_FILE = os.path.join(KUBE_SRC_DIR, 'shuffler', 'Dockerfile')

# Binary of the config parser.
CONFIG_PARSER_BINARY = os.path.join(OUT_DIR, 'config', 'config_parser',
                                    'config_parser')

CONFIG_BINARY_PROTO = os.path.join(OUT_DIR, 'third_party', 'config',
                                   'cobalt_config.binproto')

# Kubernetes deployment yaml template files with replaceable tokens.
ANALYZER_SERVICE_DEPLOYMENT_YAML = 'analyzer_service_deployment.yaml'
ANALYZER_SERVICE_SERVICE_YAML = 'analyzer_service_service.yaml'
ANALYZER_SERVICE_DEPLOYMENT_TEMPLATE_FILE = os.path.join(
    KUBE_SRC_DIR, 'analyzer_service', ANALYZER_SERVICE_DEPLOYMENT_YAML)
ANALYZER_SERVICE_SERVICE_TEMPLATE_FILE = os.path.join(
    KUBE_SRC_DIR, 'analyzer_service', ANALYZER_SERVICE_SERVICE_YAML)

REPORT_MASTER_DEPLOYMENT_YAML = 'report_master_deployment.yaml'
REPORT_MASTER_SERVICE_YAML = 'report_master_service.yaml'
REPORT_MASTER_DEPLOYMENT_TEMPLATE_FILE = os.path.join(
    KUBE_SRC_DIR, 'report_master', REPORT_MASTER_DEPLOYMENT_YAML)
REPORT_MASTER_SERVICE_TEMPLATE_FILE = os.path.join(KUBE_SRC_DIR,
                                                   'report_master',
                                                   REPORT_MASTER_SERVICE_YAML)

SHUFFLER_DEPLOYMENT_YAML = 'shuffler_deployment.yaml'
SHUFFLER_SERVICE_YAML = 'shuffler_service.yaml'
SHUFFLER_DEPLOYMENT_TEMPLATE_FILE = os.path.join(KUBE_SRC_DIR, 'shuffler',
                                                 SHUFFLER_DEPLOYMENT_YAML)
SHUFFLER_SERVICE_TEMPLATE_FILE = os.path.join(KUBE_SRC_DIR, 'shuffler',
                                              SHUFFLER_SERVICE_YAML)

# Kubernetes output directory
KUBE_OUT_DIR = os.path.join(OUT_DIR, 'kubernetes')

# Post-processed kubernetes deployment yaml files. These have had their tokens
# replaced and are ready to be used by "kubectl create"
ANALYZER_SERVICE_DEPLOYMENT_FILE = os.path.join(
    KUBE_OUT_DIR, ANALYZER_SERVICE_DEPLOYMENT_YAML)
ANALYZER_SERVICE_SERVICE_FILE = os.path.join(KUBE_OUT_DIR,
                                             ANALYZER_SERVICE_SERVICE_YAML)
REPORT_MASTER_DEPLOYMENT_FILE = os.path.join(KUBE_OUT_DIR,
                                             REPORT_MASTER_DEPLOYMENT_YAML)
REPORT_MASTER_SERVICE_FILE = os.path.join(KUBE_OUT_DIR,
                                          REPORT_MASTER_SERVICE_YAML)
SHUFFLER_DEPLOYMENT_FILE = os.path.join(KUBE_OUT_DIR, SHUFFLER_DEPLOYMENT_YAML)
SHUFFLER_SERVICE_FILE = os.path.join(KUBE_OUT_DIR, SHUFFLER_SERVICE_YAML)

# Yaml file configuring the report master endpoint.
REPORT_MASTER_ENDPOINT_CONFIG_YAML = 'report_master_endpoint.yaml'
REPORT_MASTER_ENDPOINT_CONFIG_TEMPLATE_FILE = os.path.join(
    KUBE_SRC_DIR, 'report_master', REPORT_MASTER_ENDPOINT_CONFIG_YAML)
REPORT_MASTER_ENDPOINT_CONFIG_FILE = os.path.join(
    KUBE_OUT_DIR, REPORT_MASTER_ENDPOINT_CONFIG_YAML)
REPORT_MASTER_PROTO_DESCRIPTOR = os.path.join(OUT_DIR, 'analyzer',
                                              'report_master',
                                              'report_master.descriptor')

# Yaml file configuring the shuffler endpoint.
SHUFFLER_ENDPOINT_CONFIG_YAML = 'shuffler_endpoint.yaml'
SHUFFLER_ENDPOINT_CONFIG_TEMPLATE_FILE = os.path.join(
    KUBE_SRC_DIR, 'shuffler', SHUFFLER_ENDPOINT_CONFIG_YAML)
SHUFFLER_ENDPOINT_CONFIG_FILE = os.path.join(KUBE_OUT_DIR,
                                             SHUFFLER_ENDPOINT_CONFIG_YAML)
SHUFFLER_PROTO_DESCRIPTOR = os.path.join(OUT_DIR, 'shuffler',
                                         'shuffler.descriptor')

# Static IP Names.
REPORT_MASTER_STATIC_IP_NAME = 'report-master'
SHUFFLER_STATIC_IP_NAME = 'shuffler-service'
ANALYZER_STATIC_IP_NAME = 'analyzer-service'

# Docker image deployment directories
COBALT_COMMON_DOCKER_BUILD_DIR = os.path.join(KUBE_OUT_DIR, 'cobalt_common')
ANALYZER_SERVICE_DOCKER_BUILD_DIR = os.path.join(KUBE_OUT_DIR,
                                                 'analyzer_service')
REPORT_MASTER_DOCKER_BUILD_DIR = os.path.join(KUBE_OUT_DIR, 'report_master')
SHUFFLER_DOCKER_BUILD_DIR = os.path.join(KUBE_OUT_DIR, 'shuffler')

# Docker Image Names
COBALT_COMMON_IMAGE_NAME = 'cobalt-common'
ANALYZER_SERVICE_IMAGE_NAME = 'analyzer-service'
REPORT_MASTER_IMAGE_NAME = 'report-master'
SHUFFLER_IMAGE_NAME = 'shuffler'

COBALT_COMMON_SO_FILES = [
    os.path.join(SYS_ROOT_DIR, 'lib', f)
    for f in ['libc++.so.1', 'libc++abi.so.1', 'libunwind.so.1']
]

ROOTS_PEM = os.path.join(SRC_ROOT_DIR, 'third_party', 'grpc', 'etc',
                         'roots.pem')

ANALYZER_CONFIG_FILE_NAMES = [
    'registered_encodings.txt', 'registered_metrics.txt',
    'registered_reports.txt'
]

# The names of Kubernetes secrets.
ANALYZER_PRIVATE_KEY_SECRET_NAME = 'analyzer-private-key'
SHUFFLER_PRIVATE_KEY_SECRET_NAME = 'shuffler-private-key'
SHUFFLER_CERTIFICATE_SECRET_NAME = 'shuffler-certificate-secret'
REPORT_MASTER_CERTIFICATE_SECRET_NAME = 'report-master-certificate-secret'
REPORT_MASTER_GCS_SERVICE_ACCOUNT_SECRET_NAME = \
    'report-master-gcs-service-account-secret'

# This must match the file name that is set in the environment variable
# COBALT_GCS_SERVICE_ACCOUNT_CREDENTIALS in the file
# //kubernetes/report_master/Dockerfile
REPORT_MASTER_GCS_SERVICE_ACCOUNT_JSON_FILE_NAME = 'gcs_service_account.json'


def _ensure_dir(dir_path):
  """Ensures that the directory at |dir_path| exists.

  If not it is created.

  Args:
    dir_path{string}: The path to a directory. If it does not exist it will be
      created.
  """
  if not os.path.exists(dir_path):
    os.makedirs(dir_path)


def _set_contents_of_dir(dir_name, files_to_copy):
  shutil.rmtree(dir_name, ignore_errors=True)
  os.makedirs(dir_name)
  for f in files_to_copy:
    shutil.copy(f, dir_name)


def _build_analyzer_config_file_list(cobalt_config_dir):
  return [
      os.path.join(cobalt_config_dir, f) for f in ANALYZER_CONFIG_FILE_NAMES
  ]


def _build_cobalt_common_deploy_dir():
  files_to_copy = [COBALT_COMMON_DOCKER_FILE, ROOTS_PEM] +  \
                  COBALT_COMMON_SO_FILES
  _set_contents_of_dir(COBALT_COMMON_DOCKER_BUILD_DIR, files_to_copy)


def _build_analyzer_service_deploy_dir():
  files_to_copy = [ANALYZER_SERVICE_DOCKER_FILE, ANALYZER_SERVICE_PATH]
  _set_contents_of_dir(ANALYZER_SERVICE_DOCKER_BUILD_DIR, files_to_copy)


def _build_report_master_deploy_dir(cobalt_config_dir):
  files_to_copy = [REPORT_MASTER_DOCKER_FILE, REPORT_MASTER_PATH,
      CONFIG_PARSER_BINARY, CONFIG_BINARY_PROTO] + \
      _build_analyzer_config_file_list(cobalt_config_dir)
  _set_contents_of_dir(REPORT_MASTER_DOCKER_BUILD_DIR, files_to_copy)


def _build_shuffler_deploy_dir(config_file):
  files_to_copy = [SHUFFLER_DOCKER_FILE, SHUFFLER_PATH, config_file]
  _set_contents_of_dir(SHUFFLER_DOCKER_BUILD_DIR, files_to_copy)


def _build_docker_image(image_name, deploy_dir, extra_args=None):
  cmd = ['docker', 'build']
  if extra_args:
    cmd = cmd + extra_args
  cmd = cmd + ['-t', image_name, deploy_dir]
  subprocess.check_call(cmd)


def build_all_docker_images(shuffler_config_file=SHUFFLER_CONFIG_FILE,
                            cobalt_config_dir=DEMO_CONFIG_DIR,
                            tag='latest'):
  _build_cobalt_common_deploy_dir()
  _build_docker_image(COBALT_COMMON_IMAGE_NAME + ':' + tag,
                      COBALT_COMMON_DOCKER_BUILD_DIR)

  _build_analyzer_service_deploy_dir()
  _build_docker_image(ANALYZER_SERVICE_IMAGE_NAME + ':' + tag,
                      ANALYZER_SERVICE_DOCKER_BUILD_DIR)

  _build_report_master_deploy_dir(cobalt_config_dir)
  _build_docker_image(REPORT_MASTER_IMAGE_NAME + ':' + tag,
                      REPORT_MASTER_DOCKER_BUILD_DIR)

  # Pass the full path of the config file to be copied into the deoply dir.
  _build_shuffler_deploy_dir(shuffler_config_file)

  # But pass only the basename to be found by Docker and copied into the image.
  config_file_name = os.path.basename(shuffler_config_file)
  _build_docker_image(
      SHUFFLER_IMAGE_NAME + ':' + tag,
      SHUFFLER_DOCKER_BUILD_DIR,
      extra_args=['--build-arg',
                  'config_file=%s' % config_file_name])


def _image_registry_uri(cloud_project_prefix,
                        cloud_project_name,
                        image_name,
                        tag='latest'):
  if not cloud_project_prefix:
    return '%s/%s/%s:%s' % (CONTAINER_REGISTRY_URI, cloud_project_name,
                            image_name, tag)
  return '%s/%s/%s/%s:%s' % (CONTAINER_REGISTRY_URI, cloud_project_prefix,
                             cloud_project_name, image_name, tag)


def _validate_image_uri(uri):
  subprocess.check_call(['gcloud', 'container', 'images', 'describe', uri])


def _validate_image_exists(cloud_project_prefix,
                           cloud_project_name,
                           image_name,
                           tag='latest'):
  uri = _image_registry_uri(cloud_project_prefix, cloud_project_name,
                            image_name, tag)
  _validate_image_uri(uri)


def _push_to_container_registry(cloud_project_prefix,
                                cloud_project_name,
                                image_name,
                                tag='latest'):
  registry_tag = _image_registry_uri(cloud_project_prefix, cloud_project_name,
                                     image_name, tag)
  subprocess.check_call(['docker', 'tag', image_name, registry_tag])
  subprocess.check_call(['gcloud', 'docker', '--', 'push', registry_tag])


def push_analyzer_service_to_container_registry(cloud_project_prefix,
                                                cloud_project_name,
                                                tag='latest'):
  _push_to_container_registry(cloud_project_prefix, cloud_project_name,
                              ANALYZER_SERVICE_IMAGE_NAME, tag)


def push_report_master_to_container_registry(cloud_project_prefix,
                                             cloud_project_name,
                                             tag='latest'):
  _push_to_container_registry(cloud_project_prefix, cloud_project_name,
                              REPORT_MASTER_IMAGE_NAME, tag)


def push_shuffler_to_container_registry(cloud_project_prefix,
                                        cloud_project_name,
                                        tag='latest'):
  _push_to_container_registry(cloud_project_prefix, cloud_project_name,
                              SHUFFLER_IMAGE_NAME, tag)


# A special value recognized by the function _replace_tokens_in_template. If
# a line of a template file contains a token $$FOO$$ and if the provided
# token_replacements dictionary contains a value for the token $$FOO$$ that
# is equal to the DELETE_LINE_INDICATOR then _replace_tokens_in_template will
# delete the line from the generated output.
DELETE_LINE_INDICATOR = '$$$$DELETE_LINE$$$'


def _replace_tokens_in_template(template_file, out_file, token_replacements):
  _ensure_dir(os.path.dirname(out_file))
  used_tokens = set()
  with open(out_file, 'w+b') as f:
    for line in fileinput.input(template_file):
      delete_this_line = False
      for token in token_replacements:
        if string.find(line, token) != -1:
          used_tokens.add(token)
          if token_replacements[token] == DELETE_LINE_INDICATOR:
            delete_this_line = True
            break
          else:
            line = line.replace(token, token_replacements[token])
      if not delete_this_line:
        f.write(line)
  for token in token_replacements:
    if not token in used_tokens:
      raise Exception('The token %s was never used.' % token)


def compound_project_name(cloud_project_prefix, cloud_project_name):
  if not cloud_project_prefix:
    return cloud_project_name
  return '%s:%s' % (cloud_project_prefix, cloud_project_name)


def _form_context_name(cloud_project_prefix, cloud_project_name, cluster_zone,
                       cluster_name):
  return 'gke_%s_%s_%s' % (compound_project_name(
      cloud_project_prefix, cloud_project_name), cluster_zone, cluster_name)


def _create_secret_from_files(secret_name, files, context):
  """Creates a Kubernetes secret from a set of files.

  Args:
    secret_name: {string} Name given to the secret.
    files: {dict<string, string>} Maps keys (the reference used to access a
      secret in Kubernetes) to local file paths.
    context: {string} Specifies the project and cluster in which the secret is
      to be created. (See _form_context_name)
  """
  cmd = [
      'kubectl', 'create', 'secret', 'generic', secret_name, '--context',
      context
  ]
  for data_key, file_path in files.items():
    cmd.extend(['--from-file', '%s=%s' % (data_key, file_path)])

  subprocess.check_call(cmd)


def create_cert_secret_for_shuffler(cloud_project_prefix, cloud_project_name,
                                    cluster_zone, cluster_name, path_to_cert,
                                    path_to_key):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)
  # The Cloud Endpoints expects the names "nginx.crt" and "nginx.key".
  _create_secret_from_files(SHUFFLER_CERTIFICATE_SECRET_NAME, {
      'nginx.crt': path_to_cert,
      'nginx.key': path_to_key
  }, context)


def create_cert_secret_for_report_master(cloud_project_prefix,
                                         cloud_project_name, cluster_zone,
                                         cluster_name, path_to_cert,
                                         path_to_key):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)
  # The Cloud Endpoints expects the names "nginx.crt" and "nginx.key".
  _create_secret_from_files(REPORT_MASTER_CERTIFICATE_SECRET_NAME, {
      'nginx.crt': path_to_cert,
      'nginx.key': path_to_key
  }, context)


def delete_cert_secret_for_shuffler(cloud_project_prefix, cloud_project_name,
                                    cluster_zone, cluster_name):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)
  _delete_secret(SHUFFLER_CERTIFICATE_SECRET_NAME, context)


def delete_cert_secret_for_report_master(cloud_project_prefix,
                                         cloud_project_name, cluster_zone,
                                         cluster_name):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)
  _delete_secret(REPORT_MASTER_CERTIFICATE_SECRET_NAME, context)


def _delete_secret(secret_name, context):
  subprocess.call(
      ['kubectl', 'delete', 'secret', secret_name, '--context', context])


def create_analyzer_private_key_secret(cloud_project_prefix, cloud_project_name,
                                       cluster_zone, cluster_name, path_to_pem):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)
  _create_secret_from_files(ANALYZER_PRIVATE_KEY_SECRET_NAME,
                            {ANALYZER_PRIVATE_KEY_PEM_NAME: path_to_pem},
                            context)


def create_shuffler_private_key_secret(cloud_project_prefix, cloud_project_name,
                                       cluster_zone, cluster_name, path_to_pem):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)
  _create_secret_from_files(SHUFFLER_PRIVATE_KEY_SECRET_NAME,
                            {SHUFFLER_PRIVATE_KEY_PEM_NAME: path_to_pem},
                            context)


def delete_analyzer_private_key_secret(cloud_project_prefix, cloud_project_name,
                                       cluster_zone, cluster_name):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)
  _delete_secret(ANALYZER_PRIVATE_KEY_SECRET_NAME, context)


def delete_shuffler_private_key_secret(cloud_project_prefix, cloud_project_name,
                                       cluster_zone, cluster_name):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)
  _delete_secret(SHUFFLER_PRIVATE_KEY_SECRET_NAME, context)


def create_report_master_gcs_service_account_secret(cloud_project_prefix,
                                                    cloud_project_name,
                                                    cluster_zone, cluster_name,
                                                    path_to_secret_json_file):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)
  _create_secret_from_files(REPORT_MASTER_GCS_SERVICE_ACCOUNT_SECRET_NAME, {
      REPORT_MASTER_GCS_SERVICE_ACCOUNT_JSON_FILE_NAME: path_to_secret_json_file
  }, context)


def _start_gke_service(deployment_template_file, deployment_file,
                       token_substitutions, context):
  # Generate the kubernetes deployment file by performing token replacement.
  _replace_tokens_in_template(deployment_template_file, deployment_file,
                              token_substitutions)

  # Invoke "kubectl apply" on the deployment file we just generated.
  subprocess.check_call(
      ['kubectl', 'apply', '-f', deployment_file, '--context', context])


def _build_report_master_endpoint_name(cloud_project_prefix,
                                       cloud_project_name):
  return _build_endpoint_name('reportmaster', cloud_project_prefix,
                              cloud_project_name)


def _build_shuffler_endpoint_name(cloud_project_prefix, cloud_project_name):
  return _build_endpoint_name('shuffler', cloud_project_prefix,
                              cloud_project_name)


def _build_endpoint_name(endpoint_name_prefix, cloud_project_prefix,
                         cloud_project_name):
  if cloud_project_prefix == 'google.com':
    return '%s-%s.googleapis.com' % (endpoint_name_prefix, cloud_project_name)
  elif not cloud_project_prefix:
    return '%s.endpoints.%s.cloud.goog' % (endpoint_name_prefix,
                                           cloud_project_name)
  raise ValueError('Project has a unsupported prefix.')


def _get_endpoint_config_id(cloud_project_prefix, cloud_project_name,
                            endpoint_name):
  """Get the id of the latest endpoint configuration for the specified endpoint.

  There is a many-to-many relationship between endpoint names and endpoint
  configurations. When a new configuration is uploaded for an endpoint, that
  configuration is added to the list of configuration and given an id. This
  function retreives the configuration id for the most recently uploaded config
  and returns it.

  Args:
    cloud_project_prefix {string}: For example "google.com"
    cloud_project_name {string}: For example "shuffler-test". The prefix and
      name are used when forming the URI to the image in the registry and also
      the bigtable project name.
    endpoint_name {string}: For example "cobalt.googleapi.com".  Returns
    {string}: The configuration id. For example "2017-08-21r2"
  """
  config_json = subprocess.check_output([
      'gcloud', 'endpoints', 'services', 'describe', '--format', 'json',
      endpoint_name, '--project',
      compound_project_name(cloud_project_prefix, cloud_project_name)
  ])
  config = json.loads(config_json)
  return config['serviceConfig']['id']


def start_analyzer_service(cloud_project_prefix, cloud_project_name,
                           cluster_zone, cluster_name, bigtable_instance_id,
                           static_ip_address, components, docker_tag):
  """ Starts the analyzer-service deployment and service.

  cloud_project_prefix {sring}: For example "google.com"
  cloud_project_name {sring}: For example "shuffler-test". The prefix and
      name are used when forming the URI to the image in the registry and
      also the bigtable project name.
  bigtable_instance_id {string}: The name of the instance of Cloud Bigtable
      within the specified project to be used by the Analyzer Service.
  static_ip_address {string}: A static IP address that has already been
     reserved on the GKE cluster.
  docker_tag {string}: The docker_tag of the docker image to use. If none is
    provided, latest will be used.
  """
  image_uri = _image_registry_uri(cloud_project_prefix, cloud_project_name,
                                  ANALYZER_SERVICE_IMAGE_NAME, docker_tag)
  _validate_image_uri(image_uri)

  bigtable_project_name = compound_project_name(cloud_project_prefix,
                                                cloud_project_name)

  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)

  if not static_ip_address:
    static_ip_address = _get_analyzer_static_ip(cloud_project_prefix,
                                                cloud_project_name)
  # If a static ip address is not proviced, then delete the static IP specifier
  # from the descriptor.
  if not static_ip_address:
    static_ip_address = DELETE_LINE_INDICATOR

  if 'statefulset' in components:
    # These are the token replacements that must be made inside the deployment
    # template file.
    deployment_substitutions = {
        '$$ANALYZER_SERVICE_IMAGE_URI$$': image_uri,
        '$$BIGTABLE_PROJECT_NAME$$': bigtable_project_name,
        '$$BIGTABLE_INSTANCE_ID$$': bigtable_instance_id,
        '$$ANALYZER_PRIVATE_PEM_NAME$$': ANALYZER_PRIVATE_KEY_PEM_NAME,
        '$$ANALYZER_PRIVATE_KEY_SECRET_NAME$$': ANALYZER_PRIVATE_KEY_SECRET_NAME
    }
    _start_gke_service(ANALYZER_SERVICE_DEPLOYMENT_TEMPLATE_FILE,
                       ANALYZER_SERVICE_DEPLOYMENT_FILE,
                       deployment_substitutions, context)
  if 'service' in components:
    service_substitutions = {
        '$$ANALYZER_STATIC_IP_ADDRESS$$': static_ip_address
    }
    _start_gke_service(ANALYZER_SERVICE_SERVICE_TEMPLATE_FILE,
                       ANALYZER_SERVICE_SERVICE_FILE, service_substitutions,
                       context)


def start_report_master(cloud_project_prefix,
                        cloud_project_name,
                        cluster_zone,
                        cluster_name,
                        bigtable_instance_id,
                        static_ip_address,
                        components,
                        docker_tag,
                        update_repo_url,
                        enable_report_scheduling=False):
  """ Starts the report-master deployment and service.

  cloud_project_prefix {string}: For example "google.com"
  cloud_project_name {string}: For example "shuffler-test". The prefix and
      name are used when forming the URI to the image in the registry and
      also the bigtable project name.
  bigtable_instance_id {string}: The name of the instance of Cloud Bigtable
      within the specified project to be used by the Report Master.
  static_ip_address {string}: A static IP address that has already been
      reserved on the GKE cluster.
  docker_tag {string}: The docker_tag of the docker image to use. If none is
    provided, latest will be used.
  update_repo_url {string}: URL to a git repository containing a cobalt
    configuration in its master branch. If this arg is not empty, the
    configuration of report master will be updated by pulling from the specified
    repository before scheduled reports are run.
    (e.g. "https://cobalt-analytics.googlesource.com/config/")
  enable_report_scheduling {bool}: Should report scheduling be enabled?
  """
  image_uri = _image_registry_uri(cloud_project_prefix, cloud_project_name,
                                  REPORT_MASTER_IMAGE_NAME, docker_tag)
  _validate_image_uri(image_uri)

  bigtable_project_name = compound_project_name(cloud_project_prefix,
                                                cloud_project_name)

  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)

  endpoint_name = _build_report_master_endpoint_name(cloud_project_prefix,
                                                     cloud_project_name)
  endpoint_config_id = _get_endpoint_config_id(cloud_project_prefix,
                                               cloud_project_name,
                                               endpoint_name)

  if not static_ip_address:
    static_ip_address = _get_report_master_static_ip(cloud_project_prefix,
                                                     cloud_project_name)
  # If a static ip address is not proviced, then delete the static IP specifier
  # from the descriptor.
  if not static_ip_address:
    static_ip_address = DELETE_LINE_INDICATOR

  enable_scheduling_flag = ("'-enable_report_scheduling'" if
                            enable_report_scheduling else DELETE_LINE_INDICATOR)

  if 'statefulset' in components:
    # These are the token replacements that must be made inside the deployment
    # template file.
    token_substitutions = {
        '$$REPORT_MASTER_IMAGE_URI$$':
            image_uri,
        '$$BIGTABLE_PROJECT_NAME$$':
            bigtable_project_name,
        '$$BIGTABLE_INSTANCE_ID$$':
            bigtable_instance_id,
        '$$REPORT_MASTER_ENABLE_REPORT_SCHEDULING_FLAG$$':
            enable_scheduling_flag,
        '$$ENDPOINT_NAME$$':
            endpoint_name,
        '$$ENDPOINT_CONFIG_ID$$':
            endpoint_config_id,
        '$$REPORT_MASTER_CERTIFICATE_SECRET_NAME$$':
            REPORT_MASTER_CERTIFICATE_SECRET_NAME,
        '$$REPORT_MASTER_CONFIG_UPDATE_REPO_URL$$':
            update_repo_url,
        '$$REPORT_MASTER_GCS_SERVICE_ACCOUNT_SECRET_NAME$$':
            REPORT_MASTER_GCS_SERVICE_ACCOUNT_SECRET_NAME
    }
    _start_gke_service(REPORT_MASTER_DEPLOYMENT_TEMPLATE_FILE,
                       REPORT_MASTER_DEPLOYMENT_FILE, token_substitutions,
                       context)
  if 'service' in components:
    service_substitutions = {
        '$$REPORT_MASTER_STATIC_IP_ADDRESS$$': static_ip_address
    }
    _start_gke_service(REPORT_MASTER_SERVICE_TEMPLATE_FILE,
                       REPORT_MASTER_SERVICE_FILE, service_substitutions,
                       context)


def start_shuffler(cloud_project_prefix,
                   cloud_project_name,
                   cluster_zone,
                   cluster_name,
                   static_ip_address,
                   components,
                   docker_tag,
                   use_memstore=False,
                   danger_danger_delete_all_data_at_startup=False):
  """ Starts the shuffler deployment and service.

  cloud_project_prefix {sring}: For example "google.com"
  cloud_project_name {sring}: For example "shuffler-test". The prefix and
      name are used when forming the URI to the image in the registry.
  static_ip_address {string}: A static IP address that has already been
      reserved on the GKE cluster.
  docker_tag {string}: The docker_tag of the docker image to use. If none is
    provided, latest will be used.
  """
  image_uri = _image_registry_uri(cloud_project_prefix, cloud_project_name,
                                  SHUFFLER_IMAGE_NAME, docker_tag)
  _validate_image_uri(image_uri)
  # These are the token replacements that must be made inside the deployment
  # template file.
  use_memstore_string = 'false'
  if use_memstore:
    use_memstore_string = 'true'
  delete_all_data = 'false'
  if danger_danger_delete_all_data_at_startup:
    delete_all_data = 'true'

  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)
  endpoint_name = _build_shuffler_endpoint_name(cloud_project_prefix,
                                                cloud_project_name)
  endpoint_config_id = _get_endpoint_config_id(cloud_project_prefix,
                                               cloud_project_name,
                                               endpoint_name)

  if not static_ip_address:
    static_ip_address = _get_shuffler_static_ip(cloud_project_prefix,
                                                cloud_project_name)
  # If a static ip address is not proviced, then delete the static IP specifier
  # from the descriptor.
  if not static_ip_address:
    static_ip_address = DELETE_LINE_INDICATOR

  if 'statefulset' in components:
    # These are the token replacements that must be made inside the deployment
    # template file.
    token_substitutions = {
        '$$SHUFFLER_IMAGE_URI$$':
            image_uri,
        '$$SHUFFLER_USE_MEMSTORE$$':
            use_memstore_string,
        '$$SHUFFLER_PRIVATE_PEM_NAME$$':
            SHUFFLER_PRIVATE_KEY_PEM_NAME,
        '$$SHUFFLER_PRIVATE_KEY_SECRET_NAME$$':
            SHUFFLER_PRIVATE_KEY_SECRET_NAME,
        '$$DANGER_DANGER_DELETE_ALL_DATA_AT_STARTUP$$':
            delete_all_data,
        '$$ENDPOINT_NAME$$':
            endpoint_name,
        '$$ENDPOINT_CONFIG_ID$$':
            endpoint_config_id,
        '$$SHUFFLER_CERTIFICATE_SECRET_NAME$$':
            SHUFFLER_CERTIFICATE_SECRET_NAME
    }
    _start_gke_service(SHUFFLER_DEPLOYMENT_TEMPLATE_FILE,
                       SHUFFLER_DEPLOYMENT_FILE, token_substitutions, context)
  if 'service' in components:
    service_substitutions = {
        '$$SHUFFLER_STATIC_IP_ADDRESS$$': static_ip_address
    }
    _start_gke_service(SHUFFLER_SERVICE_TEMPLATE_FILE, SHUFFLER_SERVICE_FILE,
                       service_substitutions, context)


def _stop_gke_service(name, context, components=['service', 'statefulset']):
  subprocess.check_call(
      ['kubectl', 'delete', ','.join(components), name, '--context', context])


def stop_analyzer_service(cloud_project_prefix, cloud_project_name,
                          cluster_zone, cluster_name, components, docker_tag):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)

  _validate_image_exists(cloud_project_prefix, cloud_project_name,
                         ANALYZER_SERVICE_IMAGE_NAME, docker_tag)
  _stop_gke_service(ANALYZER_SERVICE_IMAGE_NAME, context, components)


def stop_report_master(cloud_project_prefix, cloud_project_name, cluster_zone,
                       cluster_name, components, docker_tag):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)

  _validate_image_exists(cloud_project_prefix, cloud_project_name,
                         REPORT_MASTER_IMAGE_NAME, docker_tag)
  _stop_gke_service(REPORT_MASTER_IMAGE_NAME, context, components)


def stop_shuffler(cloud_project_prefix, cloud_project_name, cluster_zone,
                  cluster_name, components, docker_tag):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)

  _validate_image_exists(cloud_project_prefix, cloud_project_name,
                         SHUFFLER_IMAGE_NAME, docker_tag)
  _stop_gke_service(SHUFFLER_IMAGE_NAME, context, components)


def login(cloud_project_prefix, cloud_project_name):
  subprocess.check_call([
      'gcloud', 'auth', 'login', '--project',
      compound_project_name(cloud_project_prefix, cloud_project_name)
  ])


def authenticate(cluster_name, cloud_project_prefix, cloud_project_name,
                 cluster_zone):
  cmd = [
      'gcloud', 'container', 'clusters', 'get-credentials', cluster_name,
      '--project',
      compound_project_name(cloud_project_prefix, cloud_project_name)
  ]
  if cluster_zone:
    cmd.extend(['--zone', cluster_zone])
  subprocess.check_call(cmd)


def display(cloud_project_prefix, cloud_project_name, cluster_zone,
            cluster_name):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)
  print 'Kubernetes Services'
  print '-------------------'
  subprocess.check_call(['kubectl', 'get', 'services', '--context', context])
  print 'Google Cloud Endpoints Services'
  print '-------------------------------'
  subprocess.check_call([
      'gcloud', 'endpoints', 'services', 'list', '--project',
      compound_project_name(cloud_project_prefix, cloud_project_name)
  ])


def get_public_uris(cluster_name, cloud_project_prefix, cloud_project_name,
                    cluster_zone):
  """ Returns a dictionary of the public URIs of the deployed services.

  The returned dictionary will have keys "analyzer",
  "report_master", "shuffler". The value for each key will either
  be None or a string containing an <ip-address>:<port> pair.

  """

  context = _form_context_name(cloud_project_prefix, cloud_project_name,
                               cluster_zone, cluster_name)
  output = subprocess.check_output(
      ['kubectl', 'get', 'services', '--context', context])
  # This is an example of what the output of this command might look like.
  # We are going to parse it and extract the relevent data.
  #
  # NAME               CLUSTER-IP       EXTERNAL-IP       PORT(S)          AGE
  # analyzer-service   10.127.253.149   146.148.107.126   6001:30238/TCP   22h
  # kubernetes         10.127.240.1     <none>            443/TCP          6d
  # report-master      10.127.251.229   35.184.143.91     7001:30285/TCP   6d
  # shuffler           10.127.254.94    104.197.122.2     5001:31703/TCP   27m
  #
  values = {}
  dotted_quad = re.compile('^\d+\.\d+\.\d+\.\d+$')
  numeric = re.compile('^\d+$')
  for line in output.split('\n')[1:]:
    columns = line.split()
    if len(columns) != 5:
      continue
    ip_address = columns[2]
    if dotted_quad.match(ip_address) is None:
      continue
    fields = columns[3].split(':')
    if len(fields) < 2:
      continue
    port = fields[0]
    if numeric.match(port) is None:
      continue
    values[columns[0]] = '%s:%s' % (ip_address, port)
  uris = {
      'analyzer': values.get(ANALYZER_SERVICE_IMAGE_NAME),
      'report_master': values.get(REPORT_MASTER_IMAGE_NAME),
      'shuffler': values.get(SHUFFLER_IMAGE_NAME),
  }
  return uris


def _configure_endpoint(cloud_project_prefix, cloud_project_name, endpoint_name,
                        static_ip_address, config_template_file, config_file,
                        proto_descriptor):
  # If a static ip address is not proviced, then delete the static IP specifier
  # from the descriptor.
  if not static_ip_address:
    static_ip_address = DELETE_LINE_INDICATOR
  token_substitutions = {
      '$$ENDPOINT_NAME$$': endpoint_name,
      '$$STATIC_IP_ADDRESS$$': static_ip_address
  }
  _replace_tokens_in_template(config_template_file, config_file,
                              token_substitutions)
  subprocess.check_call([
      'gcloud', 'endpoints', 'services', 'deploy', proto_descriptor,
      config_file, '--project',
      compound_project_name(cloud_project_prefix, cloud_project_name)
  ])


def configure_report_master_endpoint(cloud_project_prefix, cloud_project_name,
                                     static_ip_address):
  if not static_ip_address:
    static_ip_address = _get_report_master_static_ip(cloud_project_prefix,
                                                     cloud_project_name)
  endpoint_name = _build_report_master_endpoint_name(cloud_project_prefix,
                                                     cloud_project_name)
  _configure_endpoint(cloud_project_prefix, cloud_project_name, endpoint_name,
                      static_ip_address,
                      REPORT_MASTER_ENDPOINT_CONFIG_TEMPLATE_FILE,
                      REPORT_MASTER_ENDPOINT_CONFIG_FILE,
                      REPORT_MASTER_PROTO_DESCRIPTOR)


def configure_shuffler_endpoint(cloud_project_prefix, cloud_project_name,
                                static_ip_address):
  if not static_ip_address:
    static_ip_address = _get_shuffler_static_ip(cloud_project_prefix,
                                                cloud_project_name)
  endpoint_name = _build_shuffler_endpoint_name(cloud_project_prefix,
                                                cloud_project_name)
  _configure_endpoint(cloud_project_prefix, cloud_project_name, endpoint_name,
                      static_ip_address, SHUFFLER_ENDPOINT_CONFIG_TEMPLATE_FILE,
                      SHUFFLER_ENDPOINT_CONFIG_FILE, SHUFFLER_PROTO_DESCRIPTOR)


def reserve_static_ip_addresses(cloud_project_prefix, cloud_project_name,
                                cluster_zone):
  project_name = compound_project_name(cloud_project_prefix, cloud_project_name)
  # The format for a zone name is: '<region>-<zone>'. For instance:
  # zone: us-central1-a
  # region: us-central1
  # We extract the region since the IPs should be located in the same region as
  # the cluster.
  region = '-'.join(cluster_zone.split('-')[:-1])
  subprocess.check_call([
      'gcloud', 'compute', 'addresses', 'create', REPORT_MASTER_STATIC_IP_NAME,
      SHUFFLER_STATIC_IP_NAME, '--project', project_name, '--region', region
  ])


def _get_static_ips(cloud_project_prefix, cloud_project_name):
  project_name = compound_project_name(cloud_project_prefix, cloud_project_name)
  ip_list_json = subprocess.check_output([
      'gcloud', 'compute', 'addresses', 'list', '--project', project_name,
      '--format', 'json'
  ])
  ip_list = json.loads(ip_list_json)
  return {ip['name']: ip['address'] for ip in ip_list}


def _get_report_master_static_ip(cloud_project_prefix, cloud_project_name):
  ips = _get_static_ips(cloud_project_prefix, cloud_project_name)
  return ips.get(REPORT_MASTER_STATIC_IP_NAME)


def _get_shuffler_static_ip(cloud_project_prefix, cloud_project_name):
  ips = _get_static_ips(cloud_project_prefix, cloud_project_name)
  return ips.get(SHUFFLER_STATIC_IP_NAME)


def _get_analyzer_static_ip(cloud_project_prefix, cloud_project_name):
  ips = _get_static_ips(cloud_project_prefix, cloud_project_name)
  return ips.get(ANALYZER_STATIC_IP_NAME)


def kube_ui(cloud_project_name, cluster_zone, cluster_name):
  context = _form_context_name(None, cloud_project_name, cluster_zone,
                               cluster_name)
  proxy = subprocess.Popen(['kubectl', 'proxy', '--context', context])
  subprocess.check_call(['xdg-open', 'http://127.0.0.1:8001/ui'])
  proxy.wait()


def main():
  print get_public_uris()


if __name__ == '__main__':
  main()

#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""A library to help with building/deploying production cobalt packages."""

import subprocess
import tempfile
import shutil
import os

import container_util

COBALT_REPO_CLONE_URL = 'https://fuchsia.googlesource.com/cobalt'


def _cobaltb(*args):
  cmd = ['./cobaltb.py']
  cmd.extend(args)
  subprocess.check_call(cmd)


def _select_git_revision():
  tags = subprocess.check_output(
      ['git', 'tag', '-l', '--sort=-version:refname']).strip().split('\n')[:5]
  tags.append('HEAD')

  while True:
    print
    print
    print('Which version would you like to build for?')
    for i, tag in enumerate(tags):
      print('({}) {}'.format(i, tag))

    selection = raw_input('? ')
    try:
      selection = int(selection)
      if selection < len(tags):
        return tags[selection]
    except:
      print('Invalid selection')


def _build_and_test_cobalt_locally(git_revision):
  """ Assumes that the current working directory is a Cobalt repo.

  Checks out Cobalt at the given |git_revision| and then builds and tests
  Cobalt. Throws an exception if any step fails.
  """
  subprocess.check_call(['git', 'checkout', git_revision])
  _cobaltb('setup')
  _cobaltb('clean', '--full')
  _cobaltb('build')
  _cobaltb('test')


def build_and_push_production_docker_images(cloud_project_name,
                                            production_dir,
                                            git_revision,
                                            work_dir=None,
                                            skip_build=False):
  """ Builds and pushes production-ready docker images from a clean git repo.

  Returns a tag string that may be used to reference the versions of the docker
  images that were upload in the Google Cloud Docker registry.

  cloud_project_name {string}: For example "fuchsia-cobalt". The name is used
      when forming the URI to the image in the registry and also the bigtable
      project name.
  production_dir {string}: The directory of the production config files.
  git_revision {string}: A git revision passed in from the command line, if none
    is provided, the user will be prompted to select one.
    latest will be used.
  work_dir {string} The working directory to use. If not provided a temporary
    directory will be created and later deleted.
  skip_build {boolean} Should we skip building Cobalt and assume the correct
    version of Cobalt has previously been successfully built in |work_dir|?
    optional, defaults to False. Note that it only makes sense to set this True
    if |work_dir| has been provided. If you do this it is your responsibility
    to ensure it really does contain a build of the right version of Cobalt.
    This is useful primarily for testing this Python script. It is dangerous
    to use this when really deploying to producttion.
  """
  wd = os.getcwd()

  try:
    if work_dir is not None:
      clean_repo_dir = work_dir
    else:
      clean_repo_dir = tempfile.mkdtemp('-cobalt-production-build')

    if not skip_build:
      shutil.rmtree(clean_repo_dir, True)
      subprocess.check_call(
          ['git', 'clone', COBALT_REPO_CLONE_URL, clean_repo_dir])

    os.chdir(clean_repo_dir)

    if not git_revision:
      git_revision = _select_git_revision()

    if git_revision is 'HEAD':
      git_revision = subprocess.check_output(['git', 'rev-parse',
                                              'HEAD']).strip()

    if not skip_build:
      _build_and_test_cobalt_locally(git_revision)

    print "\nInvoking 'cobaltb.py deploy build'..."
    p = subprocess.Popen([
        './cobaltb.py', 'deploy', 'build',
        '--production_dir=%s' % production_dir
    ],
                         stdin=subprocess.PIPE)
    p.communicate('yes')
    if p.wait() != 0:
      raise Exception("Invocation of 'cobaltb.py deploy build' failed.")
    print "Invocation of 'cobaltb.py deploy build' succeeded.\n"

    full_rev = subprocess.check_output(['git', 'rev-parse', 'HEAD']).strip()
    tags_to_apply = ['latest', full_rev]
    describe = subprocess.check_output(['git', 'describe']).strip()
    if describe is not git_revision:
      tags_to_apply.append(describe)
    subrev = ''
    # This will construct a series of tags based on the version. e.g. if the
    # version is v1.2.3, it would create the tags 'v1', 'v1.2', and 'v1.2.3'
    for part in git_revision.split('.'):
      if subrev is not '':
        subrev += '.'
      subrev += part
      tags_to_apply.append(subrev)

    for tag in tags_to_apply:
      print 'Pushing Shuffler to container registry at %s with tag=%s.\n' % (
          cloud_project_name, tag)
      container_util.push_shuffler_to_container_registry(
          '', cloud_project_name, tag)
      print(
          'Pushing Analyzer Service to container registry at '
          '%s with tag=%s.\n' % (cloud_project_name, tag))
      container_util.push_analyzer_service_to_container_registry(
          '', cloud_project_name, tag)
      print(
          'Pushing ReportMaster  to container registry at '
          '%s with tag=%s.\n' % (cloud_project_name, tag))
      container_util.push_report_master_to_container_registry(
          '', cloud_project_name, tag)

    return full_rev
  finally:
    os.chdir(wd)
    if work_dir is None:
      raw_input('Press enter to delete temp directory...')
      print('Cleaning up')
      shutil.rmtree(clean_repo_dir)


def main():
  # Note(rudominer) It may be useful to directly run this script in order to
  # either debug this script itself or else to debug the use of Docker on your
  # computer. To do so set the following variables as appropriate for your
  # computer and your personal devel cluster. You may run one time with
  # skip_build=False in order to perform the build and after that run with
  # skip_build=True in order to debug Docker (or debug this script) without
  # needing to redo the build.
  cloud_project_name = '<your project name>'
  production_dir = ''
  git_revision = None
  work_dir = '<some directory>'
  skip_build = True
  build_and_push_production_docker_images(cloud_project_name, production_dir,
                                          git_revision, work_dir, skip_build)


if __name__ == '__main__':
  main()

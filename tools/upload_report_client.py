#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Uploads report_client to Google Cloud Storage.

We use the script upload_to_google_storage.py from depot_tools. You must
have this in your path.

We tar and gzip compress the binary report_client forming a .tgz file. We then
upload this .tgz file to the Google Cloud Storage bucket
fuchsia-build/cobalt/report_client/<platform>
using the sha1 of the .tgz file as the filename.

Some typical values of <platform> are linux64 and darwin64.

Instructions:
(1) Build a new version of report_client locally via
./cobaltb.py build.
(2) Run this script
(3) In addition to uploading a new report_client this script will also
update the file tools/go/report_client.<platform>.tgz.sha1. Submit
this change in a new CL.
(4) In the Fuchsia repo update the file
//apps/cobalt_client/download_report_client.py with the new sha1.

"""

from __future__ import print_function

import os
import platform
import shutil
import subprocess
import sys
import tarfile

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))
OUT_DIR = os.path.join(SRC_ROOT_DIR, 'out')
OUT_TOOLS_DIR = os.path.join(OUT_DIR, 'tools')
BINARY_NAME = 'report_client'
TOOLS_GO_DIR = os.path.join(SRC_ROOT_DIR, 'tools', 'go')


def _write_compressed_tarfile(tarfile_to_create):
  saved_cwd = os.getcwd()
  os.chdir(OUT_TOOLS_DIR)
  with tarfile.open(tarfile_to_create, 'w:gz') as tar:
    tar.add(BINARY_NAME)
  os.chdir(saved_cwd)


def _platform_string():
  return '%s%s' % (platform.system().lower(), platform.architecture()[0][:2])


def _upload(file_to_upload, platform_string):
  bucket_name = 'fuchsia-build/cobalt/report_client/%s' % platform_string
  cmd = ['upload_to_google_storage.py', '-b', bucket_name, file_to_upload]
  subprocess.check_call(cmd)


def main():
  report_client_path = os.path.join(OUT_DIR, 'tools', 'report_client')
  if not os.path.exists(report_client_path):
    print('File not found: %s.' % report_client_path)
    print('You must build first.')
    return 1
  platform_string = _platform_string()
  tgz_file = 'report_client.%s.tgz' % platform_string
  cwd = os.getcwd()
  temp_tgz_file_path = os.path.join(cwd, tgz_file)
  print('Compressing %s to temporary file %s...' %
        (report_client_path, temp_tgz_file_path))
  _write_compressed_tarfile(temp_tgz_file_path)
  _upload(temp_tgz_file_path, platform_string)
  os.remove(temp_tgz_file_path)
  temp_sha1_file = '%s.sha1' % temp_tgz_file_path
  target_sha1_file_name = '%s.sha1' % tgz_file
  final_sha1_file = os.path.join(TOOLS_GO_DIR, target_sha1_file_name)
  shutil.move(temp_sha1_file, final_sha1_file)


if __name__ == '__main__':
  main()

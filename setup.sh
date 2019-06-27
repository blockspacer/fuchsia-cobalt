#!/bin/bash
#
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
#
# Installs cobalt dependencies.

set -e

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PREFIX="${SCRIPT_DIR}/sysroot"
readonly GCLOUD_DIR="${PREFIX}/gcloud"

export PATH="${PREFIX}/bin:${GCLOUD_DIR}/google-cloud-sdk/bin:${PATH}"
export LD_LIBRARY_PATH="${PREFIX}/lib"

# Main entry point.
while getopts "eh" o; do
    case "${o}" in
      e)
        exec /bin/bash
        ;;
      h)
        echo "Usage: $0 <opts>"
        echo "-h    help"
        echo "-e    launch a shell with PATHs set"
        exit 0
        ;;
      *)
        ;;
    esac
done

echo Ensuring cipd
${SCRIPT_DIR}/cipd ensure -ensure-file cobalt.ensure -root sysroot ||
  echo -e "\033[0;31m\n\nTry deleting .cipd_client and running ./setup.sh again.\033[0m\n\n"

# Build and install protoc-gen-go binary
export GOROOT="${PREFIX}/golang"
export GOPATH="${SCRIPT_DIR}/third_party/go"
# XXX Don't know how to check the version so always build it.  The build is
# quick.
echo Building protoc-gen-go
${GOROOT}/bin/go build -o ${PREFIX}/bin/protoc-gen-go github.com/golang/protobuf/protoc-gen-go

echo Installing additional gcloud components
${GCLOUD_DIR}/google-cloud-sdk/bin/gcloud --quiet components install bigtable kubectl

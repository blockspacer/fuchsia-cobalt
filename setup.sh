#!/bin/bash
#
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# ound in the LICENSE file.
#
# Installs cobalt dependencies.

set -e

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PREFIX="${SCRIPT_DIR}/sysroot"

export PATH="${PREFIX}/bin:${PATH}"

src_sym=${SCRIPT_DIR}/third_party/boringssl/src
if [ -h $src_sym ]; then
  rm $src_sym
fi
echo Linking $src_sym to ../boringssl-src
ln -s ../boringssl-src $src_sym

echo Ensuring cipd
${SCRIPT_DIR}/cipd ensure -ensure-file cobalt.ensure -root sysroot ||
  echo -e "\033[0;31m\n\nTry deleting .cipd_client and running ./setup.sh again.\033[0m\n\n"

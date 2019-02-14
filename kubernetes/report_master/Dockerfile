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

FROM cobalt-common

# If this fails, look for troubleshooting information in the instructions
# for executing cobaltb.py deploy build.
RUN apt-get update
RUN apt-get install -y git

COPY analyzer_report_master /usr/local/bin/

# The path to where we are copying the config files must match the path
# specified in the -cobalt_config_dir flag in report_master_deployment.yaml.
COPY registered_encodings.txt /etc/cobalt/
COPY registered_metrics.txt /etc/cobalt/
COPY registered_reports.txt /etc/cobalt/
COPY cobalt_config.binproto /etc/cobalt

# Install the config parser.
COPY config_parser /usr/local/bin/

# The directory part of this path must match the mountPath entry
# in report_master_deployment.yaml corresponding to the name
# report-master-gcs-service-account-key-storage. The filename part must match
# the variable REPORT_MASTER_GCS_SERVICE_ACCOUNT_JSON_FILE_NAME in the
# file //util/container_util.py
ENV COBALT_GCS_SERVICE_ACCOUNT_CREDENTIALS \
    /var/lib/cobalt/gcskey/gcs_service_account.json

ENTRYPOINT ["analyzer_report_master"]
EXPOSE 7000

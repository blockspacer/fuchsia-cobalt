# Cobalt
An analytics pipeline with built-in user-privacy.

[TOC]

## Purpose of this document
You may encounter this document in one of two scenarios:

* Because you are working with the stand-alone Cobalt repo found at
`https://fuchsia.googlesource.com/cobalt`, or
* because you are working with a checkout of the Fuchsia operating system
in which Cobalt has been imported into `//third_party/cobalt.`

This document should be used only in the first context. It describes how to
build and test Cobalt independently of Fuchsia. Stand-alone Cobalt
includes server-side components that run in Linux on Google Kubernetes Engine
and a generic client library that is compiled for Linux using the build
described in this document..

When imported into `//third_party/cobalt` within a Fuchsia checkout, the Cobalt
client library is compiled for Fuchsia and accessed via a FIDL wrapper. If you
are trying to use Cobalt from an application running on Fuchsia, stop reading
this document and instead read Cobalt's
[README.md](https://fuchsia.googlesource.com/garnet/+/master/bin/cobalt/README.md)
in the Garnet repo.

## Requirements
  1. We only develop on Linux. You may or may not have success on other
  systems.
  2. Python 2.7
  3. libstdc++
On a new Linux glaptop you may not have libstdc++ installed. A command similar
to the following appears to work:
`sudo apt-get install libstdc++-8-dev`

## Fetch the code
For example via
* `git clone https://fuchsia.googlesource.com/cobalt`
* `cd cobalt`

## Run setup
 `./cobaltb.py setup`. This will take a few minutes the first time. It does
 the following:
  * Fetches third party code into the `third_party` dir via git submodules.
  * Fetches the sysroot and puts it in the `sysroot` dir. This uses a tool
  called `cipd` or *Chrome Infrastructure Package Deployer*.
  * Updates the Google Cloud SDK (which was included in sysroot)
  * Other miscellaneous stuff

## cobaltb.py
The Python script cobaltb.py in the root directory is used to orchestrate
building, testing and deploying to Google Kubernetes Engine. It was already
used above in `./cobaltb.py setup`.

* `./cobaltb.py -h` for general help
* `cobaltb.py <command> -h` for help on a command
* `cobaltb.py <command> <subcommand> -h` for help on a sub-command

### The --verbose flag
If you pass the flag `--verbose` to various `cobaltb.py` commands you will see
more verbose output. Pass it multiple times to increase the verbosity
further.

## Build
  * `./cobaltb.py clean`
  * `./cobaltb.py build`

The Cobalt build uses CMake.

## Run the Tests
  * `./cobaltb.py test` This runs the whole suite of tests finally running the
  the end-to-end test. The tests should all pass.
  * It is possible to run subsets of the tests by passing the `--tests=`
  argument.
  * See `./cobaltb.py test -h` for documentation about the `--tests=` argument.

## Contributing
Cobalt uses the Gerrit code review tool. See the
[submitting-changes](https://fuchsia.googlesource.com/docs/+/master/CONTRIBUTING.md#submitting-changes)
section of the Fuchsia contributing doc for more info about using Gerrit.
But note that the stand-alone Cobalt build currently does not use Jiri.
Use the command `git push origin HEAD:refs/for/master`. Also the other sections
of that document are not relevant to stand-alone Cobalt.

## Source Layout
The source layout is related to Cobalt's process architecture. Here we
describe the source layout and process architecture together. Most of
Cobalt's code is C++. The Shuffler is written in Go.

### The root directory
The most interesting contents of the root directory are the .proto files
`observation.proto`, which contains the definitions of *Observation* and
*Envelope*, and `encrypted_message.proto`, which contains the definition
of *EncryptedMessage.* Observations are the basic units of data captured by
a Cobalt client application. Each Observation is encrypted and the bytes are
stored in an *EncryptedMessage.* Multiple EncryptedMessages are stored in an
*ObservationBatch*. Multiple ObservationBatches are stored in an Envelope.
Envelopes are sent via gRPC from the *Encoder* to the *Shuffler*.

### encoder
This directory contains the code for Cobalt's Encoder, which is a
client library whose job is to encode Observations using one of several
privacy-preserving encodings, and send Envelopes to the Shuffler using gRPC.

### shuffler
This directory contains the code for one of Cobalt's server-side components,
the Shuffler. The Shuffler receives encrypted Observations from the Encoder,
stores them briefly, shuffles them, and forwards the shuffled ObservationBatches
on to the Analyzer Service. The purpose of the Shuffler is to break linkability
with individual users. It is one of the privacy tools in Cobalt's arsenal.
The Shuffler is written in Go.

### analyzer
This directory contains the code for more of Cobalt's server side components.
Conceptually the Analyzer is a single entity responsible for receiving
Observations from the Shuffler, storing them in a database, and later
analyzing them to produce published reports. The analyzer is actually composed
of two different processes.

#### analyzer/analyzer_service
This directory contains the code for Cobalt's
Analyzer Service process. This is a gRPC server that receives ObservationBatches
from the Shuffler and writes the Observations to Cobalt's ObservationStore
in Bigtable.

#### analyzer/report_master
This directory contains the code for Cobalt's
Report Master process. This is a server that generates periodic reports
on a schedule. It analyzes the Observations in the ObservationStore,
decodes them if they were encoded using a privacy-preserving algorithm,
and aggregates them into reports. The reports are stored in Cobalt's ReportStore
in Bigtable and also published as CSV files to Google Cloud Storage. The
ReportMaster also exposes a gRPC API so that one-off reports may be requested.

#### analyzer/store
This directory contains the implementations of the ObservationStore and
ReportStore using Bigtable.

### algorithms
This directory contains the implementations of Cobalt's privacy-
preserving algorithms. This code is linked into both the Encoder, which
uses it to encode Observations, and the ReportMaster, which uses it to
decode Observations.

### config
This directory contains the implementation of Cobalt's config registration
system. A client that wants to use Cobalt starts by registering configurations
of their *Metrics*, *Encodings* and *Reports*.

### end_to_end_tests
Contains our end-to-end test, written in Go.

### infra
This directory contains hooks used by Fuchsia's CI (continuous integration)
and CQ (commit queue) systems.

### kubernetes
This directory contains data files related to deploying Cobalt to Google
Kubernetes Engine. It is used only at deploy time.

### manifest
This directory contains a Jiri manifest. It is used to integrate Cobalt into
the rest of the Fuchsia build when Cobalt is imported into third_party/cobalt.
This is not used at all in Cobalt's stand-alone build.

### production
This directory contains configuration about the production data centers
where the Cobalt servers are deployed. It is used only at deploy time.

### prototype
This directory contains a rather old and obsolete early Python prototype
of Cobalt.

### tools
This directory contains build, test and deployment tooling.

### util
This directory contains utility libraries used by the Encoder and Analyzer.

## Running a local Cobalt System
You can run a complete Cobalt system locally as follows. Open seven different
command-line console windows and run the following commands in each one
respectively:
* `./cobaltb.py start bigtable_emulator`
* ` ./cobaltb.py start analyzer_service`
* ` ./cobaltb.py start shuffler`
* `./cobaltb.py start report_master`
* ` ./cobaltb.py start test_app`
* `./cobaltb.py start observation_querier`
* `./cobaltb.py start report_client`

It is a good idea to label the tabs so you can keep track of them.
You can safely ignore warnings about private and public key PEM files missing
and about using insecure connections. This means that encryption will not be
enabled. We discuss Cobalt's use of encryption later in this document.

Note that the `./cobaltb.py start` command automatically sets the flag `-v=3`
on all of the started processes. This sets the virtual logging level to 3.
The Cobalt log messages have been specifically tuned to give interesting
output during a demo at this virtual logging level. For example the Analyzer
service will log each time it receives a batch of Observations.

### Demo Script
Here is a demo script you may run through if you wish:

* Use the test_app to send Forculus Observations through the Shuffler to the
Analyzer
  * `encode 19 www.AAAA`
  * `encode 20 www.BBBB`
  * `send`
  * See that the Shuffler says it received the Observations but it has not yet
  forwarded them to the Analyzer because the number of Observations have not
  yet crossed the Shuffler's threshold (100).
  * `encode 100 www.CCCC`
  * `send`
  * See that the Shuffler says it received another batch of Observations and
  that it has now forwarded all of the Observations to the Analyzer.
  * See that the Analyzer says it has received the Observations

* Use the observation_querier to inspect the state of the Observation store.
  * `query 50`
  * See the debug output that shows the Forculus Observations.
  * Note that the Observations also include a SystemProfile that describes
  the OS and cpu of the client.

* Use the report_client to generate a report
  * `run full 1`
  * See that www.BBBB and www.CCCC were decrypted because the number of them
  exceeded the Forculus threshold of 20 but www.AAAA was not decrypted because
  the number of them did not exceed the threshold.

* Use the test_app to send Basic RAPPOR Observations through the Shuffler to the
Analyzer
  * `set metric 2`
  * `set encoding 2`
  * `encode 500 11`
  * `encode 1000 12`
  * `encode 500 13`
  * `send`
  * See that the Shuffler says it received the Observations and forwarded them
  to the Analyzer.
  * See that the Analyzer says it received the Observations.

* Use the observation_querier to inspect the state of the Observation store.
  * `set metric 2`
  * `query 50`
  * See the debug output that shows the Basic RAPPOR Observations
  * Again note that the Observations also include a SystemProfile that describes
  the OS and cpu of the client.

* Use the report_client to generate a report
  * `run full 2`
  * See the histogram generated by the Basic RAPPOR Observations. The
  values that were actually encoded by the client, 11, 12 and 13, show estimated
  counts that are close to their true counts of 500, 1000 and 500 respectively.
  The other values may show estimated counts of zero or a small number.

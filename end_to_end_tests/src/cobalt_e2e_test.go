// Copyright 2017 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
An end-to-end test of Cobalt. This test assumes the existence of a running
Cobalt system. The URIs of the Shuffler, the Analyzer Service and the
ReportMaster are passed in to the test as flags. Usually this test is
invoked via the command "cobaltb.py test --tests=e2e" which invokes
"tools/test_runner.py" which uses "tools/process_starter.py" to start the
Cobalt processes prior to invoking this test.

We support several diferent configurations of running this test. When invoked
as described above, the Cobalt Analyzer Service, Shuffler and Report Master
are started locally using a local Bigtable Emulator. These processes are
started via the script "tools/process_starter.py". If the invocation of
this test from cobaltb.py includes the flag "-use_cloud_bt" then a local
instance of Analyzer Service, Shuffler and Report Master are started, but
rather than using a local instance of the Bigtable Emulator, we use a
real instance of Cloud Bigtable in the current user's devel cluster
(if configured.) If the invocation of this test from cobaltb.py instead includes
the flag "-cobalt_on_personal_cluster" then the local services are not started
and instead this test uses the Cobalt server processes from the current user's
devel cluster (if configured.) Finally if the invocation of this test from
cobaltb.py instead includes the flag "--production_dir=<dir>" then
this test is run against the production instance of Cobalt using a special
test-only project ID. See the README.md file for more details about all
of this.

The test uses the |cobalt_test_app| program in order to encode values into
observations and send those observations to the Shuffler. It then uses
the |query_observations| tool to query the Observation Store and it waits
until all of the observations have arrived at the Observation Store. It
then uses the report_client to contact the ReportMaster and request
that a report be generated and wait for the report to be generated. It then
validates the report.

The test assumes a particular contents of the Cobalt registration system.
The relevent registration files are located in the config/demo directory
for all test configurations other than the one that runs against production
Cobalt--in that case the relevent registration files are located in the
config/production directory. We reserve (customerID=1, projectID=1) for the
end-to-end test. Therefore it is important that the configuration of
(customerID=1, projectID=1) in the config/demo and config/production directories
be kept in sync with this test.

Below we copy the subset of the config registration from those files that is
actually used by this test.

#### Metric (1, 1, 1)
element {
  customer_id: 1
  project_id: 1
  id: 1
  name: "Fuchsia Popular URLs"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "url"
    value {
      description: "A URL."
      data_type: STRING
    }
  }
}

#### Metric (1, 1, 2)
element {
  customer_id: 1
  project_id: 1
  id: 2
  name: "Fuchsia Usage by Hour"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "hour"
    value {
      description: "An integer from 0 to 23 representing the hour of the day."
      data_type: INT
    }
  }
}

#### Metric (1, 1, 4)
element {
  customer_id: 1
  project_id: 1
  id: 4
  name: "Rare Events"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "event"
    value {
      description: "The index of a rare event. See report config (1,1,4) for the labels corresponding to each index."
      data_type: INDEX
    }
  }
}

#### Metric (1, 1, 5)
element {
  customer_id: 1
  project_id: 1
  id: 5
  name: "Fuchsia Module Usage"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "module"
    value {
      description: "A module identifier"
      data_type: STRING
    }
  }
}

#### Metric (1, 1, 6)
element {
  customer_id: 1
  project_id: 1
  id: 6
  name: "Device Type"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "device"
    value {
      description: "Which type of device is Fuchsia running on"
      data_type: INDEX
    }
  }
}

#### Encoding (1, 1, 1)
element {
  customer_id: 1
  project_id: 1
  id: 1
  forculus {
    threshold: 20
    epoch_type: DAY
  }
}

#### Encoding (1, 1, 2)
element {
  customer_id: 1
  project_id: 1
  id: 2
  basic_rappor {
    prob_0_becomes_1: 0.1
    prob_1_stays_1: 0.9
    int_range_categories: {
      first: 0
      last:  23
    }
  }
}

#### Encoding (1, 1, 5)
element {
  customer_id: 1
  project_id: 1
  id: 5
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    indexed_categories: {
      num_categories: 100
    }
  }
}

# Encoding (1, 1, 6) is the No-Op encoding.
element {
  customer_id: 1
  project_id: 1
  id: 6
  no_op_encoding {
  }
}

#### ReportConfig (1, 1, 1)
element {
  customer_id: 1
  project_id: 1
  id: 1
  name: "Fuchsia Popular URLs"
  description: "This is a fictional report used for the development of Cobalt."
  metric_id: 1
  variable {
    metric_part: "url"
  }
  scheduling {
    report_finalization_days: 1
    aggregation_epoch_type: DAY
  }
}

#### ReportConfig (1, 1, 2)
element {
  customer_id: 1
  project_id: 1
  id: 2
  name: "Fuchsia Usage by Hour"
  description: "This is a fictional report used for the development of Cobalt."
  metric_id: 2
  variable {
    metric_part: "hour"
  }
  scheduling {
    report_finalization_days: 5
    aggregation_epoch_type: WEEK
  }
}

#### ReportConfig (1, 1, 4)
element {
  customer_id: 1
  project_id: 1
  id: 4
  name: "Fuschsia Daily System Event Counts"
  description: "This is a fictional report used for the development of Cobalt."
  metric_id: 4
  variable {
    metric_part: "event"
    index_labels {
      labels {
         key: 0
         value: "Event A"
      }
      labels {
         key: 1
         value: "Event B"
      }
      labels {
         key: 25
         value: "Event Z"
      }
    }
  }
}

#### ReportConfig (1, 1, 5)
element {
  customer_id: 1
  project_id: 1
  id: 5
  name: "Fuchsia Module Usage"
  description: "This is a fictional report used for the development of Cobalt."
  metric_id: 5
  variable {
    metric_part: "module"
  }
  scheduling {
    report_finalization_days: 1
    aggregation_epoch_type: DAY
  }
}

#### ReportConfig (1, 1, 6)
element {
  customer_id: 1
  project_id: 1
  id: 6
  name: "Fuschsia Device Start Counts"
  description: "This is a fictional report used for the development of Cobalt."
  metric_id: 6
  variable {
    metric_part: "device"
    index_labels {
      labels {
         key: 0
         value: "Type A"
      }
      labels {
         key: 1
         value: "Type B"
      }
      labels {
         key: 25
         value: "Type Z"
      }
    }
  }
}

*/

package main

import (
	"bytes"
	"flag"
	"fmt"
	"os/exec"
	"strconv"
	"testing"
	"time"

	"analyzer/report_master"
	"github.com/golang/glog"
	"report_client"
)

const (
	// IMPORTANT NOTE: This end-to-end test may be executed against the real production instance of Cobalt running on GKE.
	// Doing this has the potential of destroying real customer data. Our mechanims for preventing this from happening
	// is to partition our projectIds. Project IDs from 0 through 99 are test-only and must never be used for real projects.
	// Conversely project IDs greater than or equal to 100 are for real projects and must never be used in tests.
	// Consquently the projectId below MUST always be less than 100.
	customerId = 1
	projectId  = 1

	urlMetricId     = 1
	hourMetricId    = 2
	eventMetricId   = 4
	moduleMetricId  = 5
	deviceMetricId  = 6
	moduleMetricId2 = 7

	forculusEncodingConfigId           = 1
	basicRapporStringsEncodingConfigId = 2
	basicRapporIndexEncodingConfigId   = 5
	noOpEncodingConfigId               = 6
	stringRapporEncodingConfigId       = 7

	urlReportConfigId         = 1
	hourReportConfigId        = 2
	eventReportConfigId       = 4
	moduleReportConfigId      = 5
	deviceReportConfigId      = 6
	groupedUrlReportConfigId  = 7
	largeModuleReportConfigId = 8

	hourMetricPartName   = "hour"
	urlMetricPartName    = "url"
	eventMetricPartName  = "event"
	moduleMetricPartName = "module"
	deviceMetricPartName = "device"
)

var (
	observationQuerierPath = flag.String("observation_querier_path", "", "The full path to the Observation querier binary")
	testAppPath            = flag.String("test_app_path", "", "The full path to the Cobalt test app binary")
	bigtableToolPath       = flag.String("bigtable_tool_path", "", "The full path to the Cobalt bigtable_tool binary")
	configBinProtoPath     = flag.String("config_bin_proto_path", "", "The full path to the serialized CobaltRegistry proto from which the configuration is to be read.")

	reportMasterUri = flag.String("report_master_uri", "", "The URI of the Report Master")
	shufflerUri     = flag.String("shuffler_uri", "", "The URI of the Shuffler")

	useTls            = flag.Bool("use_tls", false, "Use TLS for gRPC connections (currently only to the Shuffler and ReportMaster.")
	skipOauth         = flag.Bool("skip_oauth", false, "Do not attempt to authenticate with the ReportMaster using OAuth.")
	shufflerRootCerts = flag.String("shuffler_root_certs", "", "Optional. A file containning the root CA certificates to be used for "+
		"the tls connection to the Shuffler.")
	reportMasterRootCerts = flag.String("report_master_root_certs", "", "Optional. A file containning the root CA certificates to be used "+
		"for the tls connection to the ReportMaster.")

	analyzerPkPemFile = flag.String("analyzer_pk_pem_file", "", "Path to a file containing a PEM encoding of the public key of the Analyzer")
	shufflerPkPemFile = flag.String("shuffler_pk_pem_file", "", "Path to a file containing a PEM encoding of the public key of the Shuffler")

	subProcessVerbosity = flag.Int("sub_process_v", 0, "-v verbosity level to pass to sub-processes")

	bigtableProjectName = flag.String("bigtable_project_name", "", "If specified use an instance Cloud Bigtable from this project instead of "+
		"a local Bigtable Emulator. -bigtable_instance_id must also be specified.")

	bigtableInstanceId = flag.String("bigtable_instance_id", "", "If specified use this instance of Cloud Bigtable instead of a local "+
		"Bigtable Emulator. -bigtable_project_name must also be specified.")

	doShufflerThresholdTest = flag.Bool("do_shuffler_threshold_test", true, "By defalt this test assumes that the Shuffler is configured "+
		"to use a threshold of 100 and it tests that if fewer than 100 Observations are sent to the Shuffler then the Shuffler does not forward "+
		"the Observations on to the Analyzer. If the Shuffler has been configured to use a threshold other than 100 then set this flag to false "+
		"and we will skip that part of the test.")

	reportClient *report_client.ReportClient
)

// Prints a big warning banner on the console and counts down 10 seconds
// allowing the user to hit conrol-c and cancel. Uses ANSI control characters
// in order to achieve color and animation.
func printWarningAndWait() {
	// There is a natural race condition because other processes have been started
	// that may also be writing to the console. We sleep for 2 seconds here in
	// order to minimize the chances of pixel collision.
	time.Sleep(2 * time.Second)
	// The control sequences \x1b[31;1m and \x1b[0m have the effect of displaying
	// the enclosed text in red.
	fmt.Println("\n********************************************************")
	fmt.Println("              W A R N I N G\n")
	fmt.Println("In 10 seconds I will permanently delete data from Bigtable.")
	fmt.Println()
	fmt.Printf("\x1b[31;1m%s  %s.\x1b[0m\n", *bigtableProjectName, *bigtableInstanceId)
	fmt.Println()
	fmt.Printf("\x1b[31;1mcustomer: %d,  project: %d\x1b[0m\n", customerId, projectId)
	fmt.Println()
	fmt.Println()
	fmt.Println()
	fmt.Println("ctr-c now or forever hold your peace.")
	fmt.Println("*********************************************************\n")
	// Move the cursor back up 5 lines.
	fmt.Printf("\033[5A")
	// Print "10" in red.
	fmt.Printf("\b\x1b[31;1m10\x1b[0m")
	// Sleep for 1 second.
	time.Sleep(time.Second)
	// Delete the "0" character. "\b" is the backspace character.
	fmt.Printf("\b \b")
	// Animate counting down 9, 8, 7, ... We use "\b" to overwrite the previous digit to
	// achieve an animation effect.
	for i := 9; i > 0; i-- {
		fmt.Printf("\b\x1b[31;1m%d\x1b[0m", i)
		time.Sleep(time.Second)
	}
	// Move the cursor back down 5 lines.
	fmt.Printf("\033[5B")
}

func init() {
	flag.Parse()

	reportClient = report_client.NewReportClient(customerId, projectId, *reportMasterUri, *useTls, *skipOauth, *reportMasterRootCerts)

	if *bigtableToolPath != "" {
		// Since we are about to delete data from a real bigtable let's give a user a chance
		// to cancel if something horrible has gone wrong.
		printWarningAndWait()
		fmt.Printf("*** Deleting observations from the Observation Store at %s;%s for project (%d, %d), metrics %d, %d, %d, %d and %d.\n",
			*bigtableProjectName, *bigtableInstanceId, customerId, projectId, urlMetricId, hourMetricId, eventMetricId, moduleMetricId, deviceMetricId)
		for _, metricId := range []int{urlMetricId, hourMetricId, eventMetricId, moduleMetricId, deviceMetricId} {
			if err := invokeBigtableTool("delete_observations", metricId, 0); err != nil {
				panic(fmt.Sprintf("Error deleting observations for metric [%v].", err))
			}
		}
		fmt.Printf("*** Deleting reports from the Report Store at %s;%s for project (%d, %d), report configs %d, %d, %d, %d, %d and %d.\n",
			*bigtableProjectName, *bigtableInstanceId, customerId, projectId,
			urlReportConfigId, hourReportConfigId, eventReportConfigId, moduleReportConfigId, deviceReportConfigId, groupedUrlReportConfigId)
		for _, reportConfigId := range []int{urlReportConfigId, hourReportConfigId, eventReportConfigId, moduleReportConfigId, deviceReportConfigId, groupedUrlReportConfigId} {
			if err := invokeBigtableTool("delete_reports", 0, reportConfigId); err != nil {
				panic(fmt.Sprintf("Error deleting reports [%v].", err))
			}
		}
	}
}

// A ValuePart represents part of an input to the Cobalt encoder. It specifies
// that the given integer, string or index should be encoded using the given
// EncodingConfig and associated with the given metric part name.
type ValuePart struct {
	// The name of the metric part this value is for.
	partName string

	// The string representation of the value. If the value is of integer
	// type this should be the representation using strconv.Itoa.
	repr string

	// The EncodingConfig id.
	encoding uint32
}

// String returns a string representation of the ValuePart in the form
// <partName>:<repr>:<encoding>. This is the form accepted as a flag to
// the Cobalt test application.
func (p *ValuePart) String() string {
	return p.partName + ":" + p.repr + ":" + strconv.Itoa(int(p.encoding))
}

// FlagString builds a string appropriate to use as a flag value to the Cobalt
// test application.
func flagString(values []ValuePart) string {
	var buffer bytes.Buffer
	for i := 0; i < len(values); i++ {
		if i > 0 {
			buffer.WriteString(",")
		}
		buffer.WriteString(values[i].String())
	}
	return buffer.String()
}

func invokeBigtableTool(command string, metricId, reportConfigId int) error {
	arguments := []string{
		"-logtostderr", fmt.Sprintf("-v=%d", *subProcessVerbosity),
		"-command", command,
		"-customer", strconv.Itoa(customerId),
		"-project", strconv.Itoa(projectId),
		"-metric", strconv.Itoa(metricId),
		"-report_config", strconv.Itoa(reportConfigId),
		"-bigtable_instance_id", *bigtableInstanceId,
		"-bigtable_project_name", *bigtableProjectName,
	}
	cmd := exec.Command(*bigtableToolPath, arguments...)
	stdoutStderr, err := cmd.CombinedOutput()
	message := string(stdoutStderr)
	if len(message) > 0 {
		fmt.Printf("%s", stdoutStderr)
	}
	if err != nil {
		stdErrMessage := ""
		if exitError, ok := err.(*exec.ExitError); ok {
			stdErrMessage = string(exitError.Stderr)
		}
		return fmt.Errorf("Error returned from bigtable_tool process: [%v] %s", err, stdErrMessage)
	}
	return nil
}

// Invokes the "query_observations" command in order to query the ObservationStore
// to determine the number of Observations contained in the store for the
// given metric. |maxNum| bounds the query so that the returned value will
// always be less than or equal to maxNum.
func getNumObservations(metricId uint32, maxNum uint32) (uint32, error) {
	arguments := []string{
		"-nointeractive",
		"-logtostderr", fmt.Sprintf("-v=%d", *subProcessVerbosity),
		"-metric", strconv.Itoa(int(metricId)),
		"-max_num", strconv.Itoa(int(maxNum)),
	}
	if *bigtableInstanceId != "" && *bigtableProjectName != "" {
		arguments = append(arguments, "-bigtable_instance_id", *bigtableInstanceId)
		arguments = append(arguments, "-bigtable_project_name", *bigtableProjectName)
	} else {
		arguments = append(arguments, "-for_testing_only_use_bigtable_emulator")
	}
	cmd := exec.Command(*observationQuerierPath, arguments...)
	out, err := cmd.Output()
	if err != nil {
		stdErrMessage := ""
		if exitError, ok := err.(*exec.ExitError); ok {
			stdErrMessage = string(exitError.Stderr)
		}
		return 0, fmt.Errorf("Error returned from query_observations process: [%v] %s", err, stdErrMessage)
	}
	num, err := strconv.Atoi(string(out))
	if err != nil {
		return 0, fmt.Errorf("Unable to parse output of query_observations as an integer: error=[%v] output=[%v]", err, out)
	}
	if num < 0 {
		return 0, fmt.Errorf("Expected non-negative integer received %d", num)
	}
	return uint32(num), nil
}

// Queries the Observation Store for the number of observations for the given
// metric. If there is an error or the number of observations found is greater
// then the |expectedNum| returns a non-nil error. If the number of observations
// found is equal to the |expectedNum| returns nil (indicating success.) Otherwise
// the number of observations found is less than the |expectedNum|. In this case
// this function sleeps for one second and then tries again, repeating that for
// up to 30 attempts and finally returns a non-nil error.
func waitForObservations(metricId uint32, expectedNum uint32) error {
	for trial := 0; trial < 30; trial++ {
		num, err := getNumObservations(metricId, expectedNum+1)
		if err != nil {
			return err
		}
		if num == expectedNum {
			return nil
		}
		if num > expectedNum {
			return fmt.Errorf("Expected %d got %d", expectedNum, num)
		}
		glog.V(3).Infof("Observation store has %d observations. Waiting for %d...", num, expectedNum)
		time.Sleep(time.Second)
	}
	return fmt.Errorf("After 30 attempts the number of observations was still not the expected number of %d", expectedNum)
}

// sendObservations calls sendObservationGroup with 2 different board names, to
// simulate getting some observations from different boards.
func sendObservations(metricId uint32, values []ValuePart, numClients uint, repeatCount uint) error {
	err := sendObservationGroup(metricId, values, numClients/2, repeatCount, "CobaltE2EBoardName")
	if err != nil {
		return err
	}
	err = sendObservationGroup(metricId, values, numClients-(numClients/2), repeatCount, "CobaltE2EBoardName2")
	return err
}

// sendObservationGroup uses the cobalt_test_app to encode the given values into
// observations and send the observations to the Shuffler.
func sendObservationGroup(metricId uint32, values []ValuePart, numClients uint, repeatCount uint, boardName string) error {
	cmd := exec.Command(*testAppPath,
		"-mode", "send-once",
		"-config_bin_proto_path", *configBinProtoPath,
		"-analyzer_pk_pem_file", *analyzerPkPemFile,
		"-shuffler_uri", *shufflerUri,
		"-shuffler_pk_pem_file", *shufflerPkPemFile,
		"-logtostderr", fmt.Sprintf("-v=%d", *subProcessVerbosity),
		"-metric", strconv.Itoa(int(metricId)),
		"-override_board_name", boardName,
		"-num_clients", strconv.Itoa(int(numClients)),
		// We perform the generate-add-send operation repeateCount times. This
		// allows us to test re-using the same instance of the Cobalt Client
		// for multiple send attempts. Note that this differs from
		// -num_adds_per_observation below in that this flag actually causes
		// additional Observations to be saved in the ObservationStore but
		// -num_adds_per_observation does not.
		"-repeat", strconv.Itoa(int(repeatCount)),
		// Each obervation is sent to the Shuffler 3 times. This allows us to test
		// that the add-observation operation is idempotent.
		"-num_adds_per_observation", "3",
		"-values", flagString(values))
	if *useTls {
		cmd.Args = append(cmd.Args, "-use_tls")
		if *shufflerRootCerts != "" {
			cmd.Args = append(cmd.Args, "-root_certs_pem_file", *shufflerRootCerts)
		}
	}
	stdoutStderr, err := cmd.CombinedOutput()
	message := string(stdoutStderr)
	if len(message) > 0 {
		fmt.Printf("%s", stdoutStderr)
	}
	return err
}

// sendStringObservations sends Observations of the given string |value| to the Shuffler,
// for the specified metric part, using the specified encoding. |numClients| different, independent
// observations will be sent. The process of adding and sending will be repeated
// |repeatCount| times.
func sendStringObservations(metricId uint32, partName string, encodingConfigId uint32, value string, numClients uint, repeatCount uint, t *testing.T) {
	values := []ValuePart{
		ValuePart{
			partName,
			value,
			encodingConfigId,
		},
	}
	if err := sendObservations(metricId, values, numClients, repeatCount); err != nil {
		t.Fatalf("url=%s, numClient=%d, err=%v", value, numClients, err)
	}
}

// sendIntObservations sends Observations of the given integer |value| to the Shuffler,
// for the specified metric part, using the specified encoding. |numClients| different, independent
// observations will be sent. The process of adding and sending will be repeated |repeatCount| times.
func sendIntObservations(metricId uint32, partName string, encodingConfigId uint32, value int, numClients uint, repeatCount uint, t *testing.T) {
	values := []ValuePart{
		ValuePart{
			hourMetricPartName,
			strconv.Itoa(value),
			basicRapporStringsEncodingConfigId,
		},
	}
	if err := sendObservations(hourMetricId, values, numClients, repeatCount); err != nil {
		t.Fatalf("hour=%d, numClient=%d, err=%v", value, numClients, err)
	}
}

// sendIndexedObservations sends Observations of type index to the Shuffler with the given data.
// |numClients| different, independent observations will be sent. The process of adding and sending will be repeated
// |repeatCount| times.
func sendIndexedObservations(metricId uint32, partName string, encodingConfigId uint32, index int, numClients uint, repeatCount uint, t *testing.T) {
	values := []ValuePart{
		ValuePart{
			partName,
			fmt.Sprintf("index=%d", index),
			encodingConfigId,
		},
	}
	if err := sendObservations(metricId, values, numClients, repeatCount); err != nil {
		t.Fatalf("index=%d, numClient=%d, err=%v", index, numClients, err)
	}
}

// sendUrlObservations sends Observations of the given |url| to the Shuffler,
// using the specified encoding. |numClients| different, independent
// observations will be sent. The process of adding and sending will be repeated
// |repeatCount| times.
func sendUrlObservations(encodingConfigId uint32, url string, numClients uint, repeatCount uint, t *testing.T) {
	sendStringObservations(urlMetricId, urlMetricPartName, encodingConfigId, url, numClients, repeatCount, t)
}

// sendModuleObservations sends Observations of the given |moudle| to the Shuffler,
// using the specified metric and encoding. |numClients| different, independent
// observations will be sent. The process of adding and sending will be repeated
// |repeatCount| times.
func sendModuleObservations(metricId uint32, encodingConfigId uint32, module string, numClients uint, repeatCount uint, t *testing.T) {
	sendStringObservations(metricId, moduleMetricPartName, encodingConfigId, module, numClients, repeatCount, t)
}

// sendForculusUrlObservations sends Observations containing a Forculus encryption of the
// given |url| to the Shuffler. |numClients| different, independent
// observations will be sent. The process of adding and sending will be repeated
// |repeatCount| times.
func sendForculusUrlObservations(url string, numClients uint, repeatCount uint, t *testing.T) {
	sendUrlObservations(forculusEncodingConfigId, url, numClients, repeatCount, t)
}

// sendBasicRapporHourObservations sends Observations containing a Basic RAPPOR encoding of the
// given |hour| to the Shuffler. |numClients| different, independent observations
// will be sent. The process of adding and sending will be repeated |repeatCount| times.
func sendBasicRapporHourObservations(hour int, numClients uint, repeatCount uint, t *testing.T) {
	sendIntObservations(hourMetricId, hourMetricPartName, basicRapporStringsEncodingConfigId, hour, numClients, repeatCount, t)
}

// sendBasicRapporEventObservations sends Observations containing a Basic RAPPOR encoding of the
// given |index| to the Shuffler. |numClients| different, independent observations
// will be sent. The process of adding and sending will be repeated |repeatCount| times.
func sendBasicRapporEventObservations(index int, numClients uint, repeatCount uint, t *testing.T) {
	sendIndexedObservations(eventMetricId, eventMetricPartName, basicRapporIndexEncodingConfigId, index, numClients, repeatCount, t)
}

// sendStringRapporModuleObservations sends encoded Observations of the
// given |module| to the Shuffler. |numClients| different, independent
// observations will be sent. The process of adding and sending will be repeated
// |repeatCount| times.
func sendStringRapporModuleObservations(module string, numClients uint, repeatCount uint, t *testing.T) {
	sendModuleObservations(moduleMetricId2, stringRapporEncodingConfigId, module, numClients, repeatCount, t)
}

// sendUnencodedModuleObservations sends unencoded Observations of the
// given |module| to the Shuffler. |numClients| different, independent
// observations will be sent. The process of adding and sending will be repeated
// |repeatCount| times.
func sendUnencodedModuleObservations(module string, numClients uint, repeatCount uint, t *testing.T) {
	sendModuleObservations(moduleMetricId, noOpEncodingConfigId, module, numClients, repeatCount, t)
}

// sendUnencodedDeviceObservations sends unencoded Observations containing the given |index| to the Shuffler.
// |numClients| different, independent observations will be sent. The process of adding and sending will be
// repeated |repeatCount| times.
func sendUnencodedDeviceObservations(index int, numClients uint, repeatCount uint, t *testing.T) {
	sendIndexedObservations(deviceMetricId, deviceMetricPartName, noOpEncodingConfigId, index, numClients, repeatCount, t)
}

// getReport asks the ReportMaster to start a new report for the given |reportConfigId|
// that spans all day indices. It then waits for the report generation to complete
// and returns the Report.
func getReport(reportConfigId uint32, includeStdErr bool, t *testing.T) *report_master.Report {
	reportId, err := reportClient.StartCompleteReport(reportConfigId)
	if err != nil {
		t.Fatalf("reportConfigId=%d, err=%v", reportConfigId, err)
	}

	report, err := reportClient.GetReport(reportId, 10*time.Second)
	if err != nil {
		t.Fatalf("reportConfigId=%d, err=%v", reportConfigId, err)
	}

	return report
}

// getCSVReport asks the ReportMaster to start a new report for the given |reportConfigId|
// that spans all day indices. It then waits for the report generation to complete
// and returns the report in CSV format.
func getCSVReport(reportConfigId uint32, includeStdErr bool, t *testing.T) string {
	report := getReport(reportConfigId, includeStdErr, t)

	csv, err := report_client.WriteCSVReportToString(report, includeStdErr)
	if err != nil {
		t.Fatalf("reportConfigId=%d, err=%v", reportConfigId, err)
	}
	return csv
}

// We run the full Cobalt pipeline using Metric 1, Encoding Config 1 and
// Report Config 1. This uses Forculus with a threshold of 20 to count
// URLs.
func TestForculusEncodingOfUrls(t *testing.T) {
	t.Skip("Skipping e2e tests for Cobalt 0.1.")
	fmt.Println("TestForculusEncodingOfUrls")
	// We send some observations to the Shuffler.
	sendForculusUrlObservations("www.AAAA.com", 18, 1, t)
	sendForculusUrlObservations("www.BBBB.com", 19, 1, t)
	sendForculusUrlObservations("www.CCCC.com", 20, 1, t)
	sendForculusUrlObservations("www.DDDD.com", 21, 1, t)

	if *doShufflerThresholdTest {
		// We have not yet sent 100 observations and the Shuffler's threshold is
		// set to 100 so we except no observations to have been sent to the
		// Analyzer yet.
		numObservations, err := getNumObservations(1, 10)
		if err != nil {
			t.Fatalf("Error returned from getNumObservations[%v]", err)
		}
		if numObservations != 0 {
			t.Fatalf("Expected no observations in the Observation store yet but got %d", numObservations)
		}
	}

	// We send additional observations to the Shuffler. This crosses the Shuffler's
	// threshold and so all observations should now be sent to the Analyzer.
	// Note that the third parameter below is repeatCount meaning that we
	// ask the test_app to repeat the generate-add-send operation that many
	// times.
	sendForculusUrlObservations("www.EEEE.com", 22, 2, t)
	sendForculusUrlObservations("www.FFFF.com", 23, 3, t)

	// There should now be 18+19+20+21+22+22+23+23+23 = 191 Observations sent to
	// the Analyzer for metric 1. We wait for them.
	if err := waitForObservations(urlMetricId, 191); err != nil {
		t.Fatalf("%s", err)
	}

	// Finally we will run a report. This is the expected output of the report.
	const expectedCSV = `www.CCCC.com,20.000
www.DDDD.com,21.000
www.EEEE.com,44.000
www.FFFF.com,69.000
`

	// Generate the report, fetch it as a CSV, check it.
	csv := getCSVReport(urlReportConfigId, false, t)
	if csv != expectedCSV {
		t.Errorf("Got csv:[%s]", csv)
	}

	const groupedExpectedCSV = `www.EEEE.com,CobaltE2EBoardName,22.000
www.EEEE.com,CobaltE2EBoardName2,22.000
www.FFFF.com,CobaltE2EBoardName,33.000
www.FFFF.com,CobaltE2EBoardName2,36.000
`
	csv = getCSVReport(groupedUrlReportConfigId, false, t)
	if csv != groupedExpectedCSV {
		t.Errorf("Got csv:[%s]", csv)
	}
}

// We run the full Cobalt pipeline using Metric 2, Encoding Config 2 and
// Report Config 2. This uses Basic RAPPOR with integer categories for the
// 24 hours of the day.
func TestBasicRapporEncodingOfHours(t *testing.T) {
	t.Skip("Skipping e2e tests for Cobalt 0.1.")
	fmt.Println("TestBasicRapporEncodingOfHours")
	sendBasicRapporHourObservations(8, 501, 1, t)
	sendBasicRapporHourObservations(9, 1002, 1, t)
	sendBasicRapporHourObservations(10, 503, 1, t)
	sendBasicRapporHourObservations(16, 504, 1, t)
	sendBasicRapporHourObservations(17, 1005, 1, t)
	sendBasicRapporHourObservations(18, 506, 1, t)

	// There should now be 4021 Observations sent to the Analyzer for metric 2.
	// We wait for them.
	if err := waitForObservations(hourMetricId, 4021); err != nil {
		t.Fatalf("%s", err)
	}

	report := getReport(hourReportConfigId, true, t)
	if report.Metadata.State != report_master.ReportState_COMPLETED_SUCCESSFULLY {
		t.Fatalf("report.Metadata.State=%v", report.Metadata.State)
	}
	includeStdErr := true
	supressEmptyRows := false
	rows := report_client.ReportToStrings(report, includeStdErr, supressEmptyRows)
	if rows == nil {
		t.Fatalf("rows is nil")
	}
	if len(rows) != 24 {
		t.Fatalf("len(rows)=%d", len(rows))
	}

	for hour := 0; hour <= 23; hour++ {
		if len(rows[hour]) != 3 {
			t.Fatalf("len(rows[hour])=%d", len(rows[hour]))
		}
		if rows[hour][0] != strconv.Itoa(hour) {
			t.Errorf("Expected %s, got %s", strconv.Itoa(hour), rows[hour][0])
		}
		val, err := strconv.ParseFloat(rows[hour][1], 32)
		if err != nil {
			t.Errorf("Error parsing %s as float: %v", rows[hour][1], err)
			continue
		}
		switch hour {
		case 8:
			fallthrough
		case 10:
			fallthrough
		case 16:
			fallthrough
		case 18:
			if val < 10.0 || val > 1000.0 {
				t.Errorf("For hour %d unexpected val: %v", hour, val)
			}
		case 9:
			fallthrough
		case 17:
			if val < 500.0 || val > 2000.0 {
				t.Errorf("For hour %d unexpected val: %v", hour, val)
			}
		default:
			if val > 100.0 {
				t.Errorf("Val larger than expected: %v", val)
				continue
			}
		}
		if rows[hour][2] != "23.779" {
			t.Errorf("rows[hour][2]=%s", rows[hour][2])
		}
	}
}

// We run the full Cobalt pipeline using Metric 4, Encoding Config 5 and
// Report Config 4. This uses Basic RAPPOR with 100 indexed categories, in
// which some of the indices have been associated with labels in the
// report config.
func TestBasicRapporEncodingOfEvents(t *testing.T) {
	t.Skip("Skipping e2e tests for Cobalt 0.1.")
	fmt.Println("TestBasicRapporEncodingOfEvents")
	// Send observations for indices 0 through 29.
	for index := 0; index < 30; index++ {
		numClients := index + 1
		sendBasicRapporEventObservations(index, uint(numClients), 1, t)
	}

	// There should 30*31/2 = 465 Observations sent to the Analyzer for metric 4.
	// We wait for them.
	if err := waitForObservations(eventMetricId, 465); err != nil {
		t.Fatalf("%s", err)
	}

	report := getReport(eventReportConfigId, true, t)
	if report.Metadata.State != report_master.ReportState_COMPLETED_SUCCESSFULLY {
		t.Fatalf("report.Metadata.State=%v", report.Metadata.State)
	}
	includeStdErr := true
	supressEmptyRows := false
	rows := report_client.ReportToStrings(report, includeStdErr, supressEmptyRows)
	if rows == nil {
		t.Fatalf("rows is nil")
	}
	if len(rows) != 100 {
		t.Fatalf("len(rows)=%d", len(rows))
	}

	for index := 0; index < 100; index++ {
		if len(rows[index]) != 3 {
			t.Fatalf("len(rows[index])=%d", len(rows[index]))
		}
		var expectedRowKey string
		switch index {
		case 0:
			expectedRowKey = "Event A"
			break
		case 1:
			expectedRowKey = "Event B"
			break
		case 25:
			expectedRowKey = "Event Z"
			break
		default:
			expectedRowKey = fmt.Sprintf("<index %d>", index)
		}
		if rows[index][0] != expectedRowKey {
			t.Errorf("Expected %s, got %s", expectedRowKey, rows[index][0])
			continue
		}

		val, err := strconv.ParseFloat(rows[index][1], 32)
		if err != nil {
			t.Errorf("Error parsing %s as float: %v", rows[index][1], err)
			continue
		}

		expectedCount := index + 1
		if index >= 30 {
			expectedCount = 0
		}
		if int(val) != expectedCount {
			t.Errorf("Expected %d, got %d", expectedCount, int(val))
			continue
		}

	}
}

// We run the full Cobalt pipeline using Metric 5, Encoding Config 6 and
// Report Config 5. This uses the NoOp encoding with module names as strings.
func TestUnencodedModules(t *testing.T) {
	t.Skip("Skipping e2e tests for Cobalt 0.1.")
	fmt.Println("TestUnencodedModules")
	// We send some observations to the Shuffler.
	// Note that the third parameter below is repeatCount meaning that we
	// ask the test_app to repeat the generate-add-send operation that many
	// times.
	sendUnencodedModuleObservations("Module A", 18, 1, t)
	sendUnencodedModuleObservations("Module B", 19, 1, t)
	sendUnencodedModuleObservations("Module C", 20, 1, t)
	sendUnencodedModuleObservations("Module D", 21, 1, t)
	sendUnencodedModuleObservations("Module E", 22, 2, t)
	sendUnencodedModuleObservations("Module F", 23, 3, t)

	// There should now be 18+19+20+21+22+22+23+23+23 = 191 Observations sent to
	// the Analyzer for metric 5. We wait for them.
	if err := waitForObservations(moduleMetricId, 191); err != nil {
		t.Fatalf("%s", err)
	}

	// Finally we will run a report. This is the expected output of the report.
	const expectedCSV = `Module A,18.000
Module B,19.000
Module C,20.000
Module D,21.000
Module E,44.000
Module F,69.000
`

	// Generate the report, fetch it as a CSV, check it.
	csv := getCSVReport(moduleReportConfigId, false, t)
	if csv != expectedCSV {
		t.Errorf("Got csv:[%s]", csv)
	}
}

// We run the full Cobalt pipeline using Metric 6, Encoding Config 6 and
// Report Config 6. This uses the NoOp encoding. Indices 0, 1 and 25 have
// been given labels in the report config.
func TestUnencodedDeviceIndexes(t *testing.T) {
	t.Skip("Skipping e2e tests for Cobalt 0.1.")
	fmt.Println("TestUnencodedDeviceIndexes")
	// Send observations for indices 0 through 29.
	for index := 0; index < 30; index++ {
		numClients := index + 1
		sendUnencodedDeviceObservations(index, uint(numClients), 1, t)
	}

	// There should 30*31/2 = 465 Observations sent to the Analyzer for metric 6.
	// We wait for them.
	if err := waitForObservations(deviceMetricId, 465); err != nil {
		t.Fatalf("%s", err)
	}

	report := getReport(deviceReportConfigId, true, t)
	if report.Metadata.State != report_master.ReportState_COMPLETED_SUCCESSFULLY {
		t.Fatalf("report.Metadata.State=%v", report.Metadata.State)
	}
	includeStdErr := true
	supressEmptyRows := false
	rows := report_client.ReportToStrings(report, includeStdErr, supressEmptyRows)
	if rows == nil {
		t.Fatalf("rows is nil")
	}
	if len(rows) != 30 {
		t.Fatalf("len(rows)=%d", len(rows))
	}

	for index := 0; index < 30; index++ {
		if len(rows[index]) != 3 {
			t.Fatalf("len(rows[index])=%d", len(rows[index]))
		}
		var expectedRowKey string
		switch index {
		case 0:
			expectedRowKey = "Type A"
			break
		case 1:
			expectedRowKey = "Type B"
			break
		case 25:
			expectedRowKey = "Type Z"
			break
		default:
			expectedRowKey = fmt.Sprintf("<index %d>", index)
		}
		if rows[index][0] != expectedRowKey {
			t.Errorf("Expected %s, got %s", expectedRowKey, rows[index][0])
			continue
		}

		val, err := strconv.ParseFloat(rows[index][1], 32)
		if err != nil {
			t.Errorf("Error parsing %s as float: %v", rows[index][1], err)
			continue
		}

		expectedCount := index + 1
		if int(val) != expectedCount {
			t.Errorf("Expected %d, got %d", expectedCount, int(val))
			continue
		}

	}
}

// We run the full Cobalt pipeline using Metric 7, Encoding Config 7 and
// Report Config 8. This uses String RAPPOR with N modules (where N = 2000 is
// specified in the config file -- it is a special case where a certain
// keyword prompts the creation of modules of the form "Module_XXXXX"
// where XXXXX is a zero-padded number).
func TestStringRapporEncodingOfModules(t *testing.T) {
	t.Skip("Skipping e2e tests for Cobalt 0.1.")
	fmt.Println("TestStringRapporEncodingOfModules")
	// Send observations for selected modules.
	sendStringRapporModuleObservations("Module_00201", 300, 1, t)
	sendStringRapporModuleObservations("Module_00511", 200, 1, t)
	sendStringRapporModuleObservations("Module_00736", 100, 1, t)

	// There should be 600 Observations sent to the Analyzer for metric 7.
	// We wait for them.
	if err := waitForObservations(moduleMetricId2, 600); err != nil {
		t.Fatalf("%s", err)
	}

	report := getReport(largeModuleReportConfigId, true, t)
	if report.Metadata.State != report_master.ReportState_COMPLETED_SUCCESSFULLY {
		t.Fatalf("report.Metadata.State=%v", report.Metadata.State)
	}
	includeStdErr := true
	supressEmptyRows := true
	rows := report_client.ReportToStrings(report, includeStdErr, supressEmptyRows)
	if rows == nil {
		t.Fatalf("rows is nil")
	}
	if len(rows) != 2000 {
		t.Fatalf("len(rows)=%d", len(rows))
	}

	for index := 0; index < 2000; index++ {
		if len(rows[index]) != 3 {
			t.Fatalf("len(rows[index])=%d", len(rows[index]))
		}

		var expectedRowKey string
		expectedRowKey = fmt.Sprintf("Module_%05d", index)

		if rows[index][0] != expectedRowKey {
			t.Errorf("Expected %s, got %s", expectedRowKey, rows[index][0])
			continue
		}

		val, err := strconv.ParseFloat(rows[index][1], 32)
		if err != nil {
			t.Errorf("Error parsing %s as float: %v", rows[index][1], err)
			continue
		}
		// Check if the sent observations are identified at least at a level of 50%
		// of their real counts, and that no other count corresponds to more than 5%
		// of the total. Note: This is a heuristic test.
		switch index {
		case 201:
			if int(val) < 150 {
				t.Errorf("Expected %d, got %d, which less than %d", 300, int(val), 150)
				break
			}
		case 511:
			if int(val) < 100 {
				t.Errorf("Expected %d, got %d, which is less than %d", 200, int(val), 100)
				break
			}
		case 736:
			if int(val) < 50 {
				t.Errorf("Expected %d, got %d, which is less than %d", 100, int(val), 50)
				break
			}
		default:
			if int(val) > 30 {
				t.Errorf("Expected %d, got %d, which is more than %d", 0, int(val), 30)
				break
			}
		}
	}
}

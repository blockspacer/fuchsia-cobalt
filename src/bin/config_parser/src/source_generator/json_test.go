// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests the constructors for formatting JSON output.

package source_generator

import (
	"config"
	"reflect"
	"testing"
)

func TestConstructorsHandleNil(t *testing.T) {
	r := makeJSONReport(nil)
	if reflect.DeepEqual(r, (jsonReport{})) == false {
		t.Errorf("makeJSONReport failed to return empty report got = %#v", r)
	}
	m := makeJSONMetric(nil)
	if reflect.DeepEqual(m, (jsonMetric{})) == false {
		t.Errorf("makeJSONMetric failed to return empty metric got = %#v", m)
	}
	p := makeJSONProject(nil)
	if reflect.DeepEqual(p, (jsonProject{})) == false {
		t.Errorf("makeJSONProject failed to return empty project got = %#v", p)
	}
	c := makeJSONCustomer(nil)
	if reflect.DeepEqual(c, (jsonCustomer{})) == false {
		t.Errorf("makeJSONCustomer failed to return empty customer got = %#v", c)
	}
	rg := makeJSONRegistry(nil)
	if reflect.DeepEqual(rg, (jsonRegistry{})) == false {
		t.Errorf("makeJSONRegistry failed to return empty registry got = %#v", rg)
	}
}

func TestMakeJSONReport(t *testing.T) {
	name := "test_name"
	id := uint32(123456789)
	candidate_file := "test_file_name"
	candidate_list := []string{"candidate1", "candidate2"}
	system_profile := []config.SystemProfileField{config.SystemProfileField_OS, config.SystemProfileField_ARCH}
	aggregation_percentile := uint32(80)

	r := config.ReportDefinition{
		ReportName:                           name,
		Id:                                   id,
		ReportType:                           config.ReportDefinition_SIMPLE_OCCURRENCE_COUNT,
		LocalPrivacyNoiseLevel:               config.ReportDefinition_NOISE_LEVEL_UNSET,
		CandidateFile:                        candidate_file,
		CandidateList:                        candidate_list,
		SystemProfileField:                   system_profile,
		AggregationType:                      config.ReportDefinition_SUM,
		WindowSize:                           []config.WindowSize{config.WindowSize_WINDOW_7_DAYS},
		LocalAggregationPeriod:               config.WindowSize_WINDOW_7_DAYS,
		LocalAggregationProcedure:            config.ReportDefinition_LOCAL_AGGREGATION_PROCEDURE_SUM,
		LocalAggregationProcedurePercentileN: aggregation_percentile,
	}

	want := jsonReport{
		Name:                                 name,
		Id:                                   id,
		ReportType:                           "SIMPLE_OCCURRENCE_COUNT",
		ReportTypeId:                         int32(config.ReportDefinition_SIMPLE_OCCURRENCE_COUNT),
		LocalPrivacyNoiseLevel:               "NOISE_LEVEL_UNSET",
		CandidateFile:                        candidate_file,
		CandidateList:                        candidate_list,
		SystemProfileField:                   []string{"OS", "ARCH"},
		AggregationType:                      "SUM",
		WindowSize:                           []string{"WINDOW_7_DAYS"},
		LocalAggregationPeriod:               7,
		LocalAggregationProcedure:            "LOCAL_AGGREGATION_PROCEDURE_SUM",
		LocalAggregationProcedurePercentileN: aggregation_percentile,
	}

	got := makeJSONReport(&r)
	if reflect.DeepEqual(want, got) == false {
		t.Errorf("makeJSONReport(%#v)\n\n GOT: %#v\nWANT: %#v", r, got, want)
	}
}

func TestMakeJSONMetric(t *testing.T) {
	name := "test_name"
	id := uint32(123456789)
	owners := []string{"owners", "owner2"}
	expirationDate := "2019/11/05"
	reports := []*config.ReportDefinition{nil, nil}
	meta := config.MetricDefinition_Metadata{
		ExpirationDate: expirationDate,
		Owner:          owners,
	}
	dimension := []*config.MetricDefinition_MetricDimension{
		&config.MetricDefinition_MetricDimension{
			Dimension:    "test_dimension",
			MaxEventCode: 3,
		},
	}
	eventCode := uint32(987654321)
	metricUnitOther := "test_metric_unit"
	candidateFile := "test_candidate_file_string"
	stringBufferMax := uint32(1234)
	protoName := "test_proto_name"

	m := config.MetricDefinition{
		MetricName:       name,
		Id:               id,
		MetricType:       config.MetricDefinition_EVENT_OCCURRED,
		MetaData:         &meta,
		MetricDimensions: dimension,
		MetricSemantics: []config.MetricSemantics{
			config.MetricSemantics_CPU,
			config.MetricSemantics_DATA_SIZE,
		},
		MetricUnits:         config.MetricUnits_NANOSECONDS,
		MetricUnitsOther:    metricUnitOther,
		EventCodeBufferMax:  eventCode,
		StringCandidateFile: candidateFile,
		StringBufferMax:     stringBufferMax,
		ProtoName:           protoName,
		Reports:             reports,
	}

	emptyReports := []jsonReport{jsonReport{}, jsonReport{}}
	want := jsonMetric{
		Name:                name,
		Id:                  id,
		MetricType:          "EVENT_OCCURRED",
		Owners:              owners,
		ExpirationDate:      expirationDate,
		Dimensions:          []string{"test_dimension"},
		MaxEventCode:        []uint32{3},
		Semantics:           []string{"CPU", "DATA_SIZE"},
		Units:               "NANOSECONDS",
		UnitsOther:          metricUnitOther,
		EventCodeBufferMax:  eventCode,
		StringCandidateFile: candidateFile,
		StringBufferMax:     stringBufferMax,
		ProtoName:           protoName,
		Reports:             emptyReports,
	}

	got := makeJSONMetric(&m)
	if reflect.DeepEqual(want, got) == false {
		t.Errorf("makeJSONMetric(%#v)\n\n GOT: %#v\nWANT: %#v", m, got, want)
	}
}

func TestMakeJSONMetricEmptyMetadata(t *testing.T) {
	name := "test_name"
	reports := []*config.ReportDefinition{nil, nil}
	m := config.MetricDefinition{
		MetricName: name,
		MetaData:   nil,
		Reports:    reports,
	}

	emptyReports := []jsonReport{jsonReport{}, jsonReport{}}
	want := jsonMetric{
		Name:       name,
		MetricType: "UNSET",
		Units:      "METRIC_UNITS_OTHER",
		Reports:    emptyReports,
	}

	got := makeJSONMetric(&m)
	if reflect.DeepEqual(want, got) == false {
		t.Errorf("makeJSONMetric(%#v)\n\n GOT: %#v\nWANT: %#v", m, got, want)
	}
}

func TestMakeJSONProject(t *testing.T) {
	name := "test_name"
	id := uint32(123456789)
	metrics := []*config.MetricDefinition{nil, nil}
	contact := "contact_test_string"
	p := config.ProjectConfig{
		ProjectName:    name,
		ProjectId:      id,
		Metrics:        metrics,
		ProjectContact: contact,
	}

	emptyMetrics := []jsonMetric{jsonMetric{}, jsonMetric{}}
	want := jsonProject{
		Name:    name,
		Id:      id,
		Contact: contact,
		Metrics: emptyMetrics,
	}

	got := makeJSONProject(&p)
	if reflect.DeepEqual(want, got) == false {
		t.Errorf("makeJSONProject(%#v)\n\n GOT: %#v\nWANT: %#v", p, got, want)
	}
}

func TestMakeJSONCustomer(t *testing.T) {
	name := "test_name"
	id := uint32(123456789)
	projects := []*config.ProjectConfig{nil, nil}
	c := config.CustomerConfig{
		CustomerName: name,
		CustomerId:   id,
		Projects:     projects,
	}

	emptyProjects := []jsonProject{jsonProject{}, jsonProject{}}
	want := jsonCustomer{
		Name:     name,
		Id:       id,
		Projects: emptyProjects,
	}

	got := makeJSONCustomer(&c)
	if reflect.DeepEqual(want, got) == false {
		t.Errorf("makeJSONCustomer(%#v)\n\n GOT: %#v\nWANT: %#v", c, got, want)
	}
}

func TestMakeJSONRegistry(t *testing.T) {
	customers := []*config.CustomerConfig{nil, nil}
	r := config.CobaltRegistry{
		Customers: customers,
	}

	emptyProjects := []jsonCustomer{jsonCustomer{}, jsonCustomer{}}
	want := jsonRegistry{
		Customers: emptyProjects,
	}

	got := makeJSONRegistry(&r)
	if reflect.DeepEqual(want, got) == false {
		t.Errorf("makeJSONRegistry(%#v)\n\n GOT: %#v\nWANT: %#v", r, got, want)
	}
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"fmt"
	"net/mail"
	"regexp"
	"time"
)

// This file contains logic to validate lists of MetricDefinition protos.

// Date format is yyyy/mm/dd. This is done by specifying in the desired format
// Mon Jan 2 15:04:05 MST 2006. See time module documentation.
const dateFormat = "2006/01/02"

// Valid names are 1-64 characters long. They have the same syntactic requirements
// as a C variable name.
var validNameRegexp = regexp.MustCompile("^[a-zA-Z][_a-zA-Z0-9]{1,65}$")

// Valid proto names are 1-64 characters long. They have the same syntactic
// requirements as a C variable name with the exceptions that they can include
// '.' for package names and '$' for '$team_name'.
var validProtoNameRegexp = regexp.MustCompile("^[$a-zA-Z][_.$a-zA-Z0-9]{1,64}[a-zA-Z0-9]$")

// Validate a list of MetricDefinitions.
func validateConfiguredMetricDefinitions(metrics []*config.MetricDefinition) (err error) {
	metricIds := map[uint32]int{}
	for i, metric := range metrics {
		if ci, ok := metricIds[metric.Id]; ok {
			return fmt.Errorf("Metrics named '%s' and '%s' hash to the same metric ids. One must be renamed.", metric.MetricName, metrics[ci].MetricName)
		}
		metricIds[metric.Id] = i

		if err = validateMetricDefinition(*metric); err != nil {
			return fmt.Errorf("Error validating metric '%s': %v", metric.MetricName, err)
		}
	}

	return nil
}

// Validate a single MetricDefinition.
func validateMetricDefinition(m config.MetricDefinition) (err error) {
	if !validNameRegexp.MatchString(m.MetricName) {
		return fmt.Errorf("Invalid metric name. Metric names must match the regular expression '%v'.", validNameRegexp)
	}

	if m.Id == 0 {
		return fmt.Errorf("Metric ID of zero is not allowed. Please specify a positive metric ID.")
	}

	if m.MetricType == config.MetricDefinition_UNSET {
		return fmt.Errorf("metric_type is not set.")
	}

	if m.MetaData == nil {
		return fmt.Errorf("meta_data is not set.")
	}

	if err := validateMetadata(*m.MetaData); err != nil {
		return fmt.Errorf("Error in meta_data: %v", err)
	}

	if m.MaxEventCode != 0 && m.MetricType != config.MetricDefinition_EVENT_OCCURRED {
		return fmt.Errorf("Metric %s has max_event_code set. max_event_code can only be set for metrics for metric type EVENT_OCCURRED.", m.MetricName)
	}

	if m.IntBuckets != nil && m.MetricType != config.MetricDefinition_INT_HISTOGRAM {
		return fmt.Errorf("Metric %s has int_buckets set. int_buckets can only be set for metrics for metric type INT_HISTOGRAM.", m.MetricName)
	}

	// TODO(ninai): Remove when we have migrated metrics from using parts.
	if len(m.Parts) > 0 && m.MetricType != config.MetricDefinition_CUSTOM {
		return fmt.Errorf("Metric %s has parts set. parts can only be set for metrics for metric type CUSTOM.", m.MetricName)
	}

	if m.ProtoName != "" && m.MetricType != config.MetricDefinition_CUSTOM {
		return fmt.Errorf("Metric %s has proto_name set. proto_name can only be set for metrics for metric type CUSTOM.", m.MetricName)
	}

	if err := validateMetricDefinitionForType(m); err != nil {
		return fmt.Errorf("Metric %s: %v", m.MetricName, err)
	}

	return validateReportDefinitions(m)
}

// Validate a single instance of Metadata.
func validateMetadata(m config.MetricDefinition_Metadata) (err error) {
	if len(m.ExpirationDate) == 0 {
		return fmt.Errorf("No expiration_date set.")
	}

	var exp time.Time
	exp, err = time.ParseInLocation(dateFormat, m.ExpirationDate, time.UTC)
	if err != nil {
		return fmt.Errorf("Invalid expiration_date '%v'. expiration_date must use the yyyy/mm/dd format.", m.ExpirationDate)
	}

	// Date one year and one day from today.
	maxExp := time.Now().AddDate(1, 0, 0)
	if exp.After(maxExp) {
		return fmt.Errorf("The expiration_date must be set no more than 1 year in the future.")
	}

	for _, o := range m.Owner {
		if _, err = mail.ParseAddress(o); err != nil {
			return fmt.Errorf("'%v' is not a valid email address in owner field", o)
		}
	}

	if m.MaxReleaseStage == config.ReleaseStage_RELEASE_STAGE_NOT_SET {
		return fmt.Errorf("No max_release_stage set.")
	}

	return nil
}

// Validate the event_codes and max_event_code fields.
func validateEventCodes(m config.MetricDefinition) error {
	if len(m.EventCodes) == 0 {
		return fmt.Errorf("no event_codes listed for metric of type %s.", m.MetricType)
	}

	if m.MaxEventCode >= 1024 {
		return fmt.Errorf("max_event_code must be less than 1024.")
	}

	if m.MaxEventCode == 0 {
		return nil
	}

	for i, _ := range m.EventCodes {
		if i > m.MaxEventCode {
			return fmt.Errorf("Event index %v is greater than max_event_code %v.", i, m.MaxEventCode)
		}
	}

	return nil
}

///////////////////////////////////////////////////////////////
// Validation for specific metric types:
///////////////////////////////////////////////////////////////

func validateMetricDefinitionForType(m config.MetricDefinition) error {
	switch m.MetricType {
	case config.MetricDefinition_EVENT_OCCURRED:
		return validateEventOccurred(m)
	case config.MetricDefinition_EVENT_COUNT:
		return validateEventCodes(m)
	case config.MetricDefinition_ELAPSED_TIME:
		return validateEventCodes(m)
	case config.MetricDefinition_FRAME_RATE:
		return validateEventCodes(m)
	case config.MetricDefinition_MEMORY_USAGE:
		return validateEventCodes(m)
	case config.MetricDefinition_INT_HISTOGRAM:
		return validateIntHistogram(m)
	case config.MetricDefinition_STRING_USED:
		return validateStringUsed(m)
	case config.MetricDefinition_CUSTOM:
		return validateCustom(m)
	}

	return fmt.Errorf("Unknown MetricType: %v", m.MetricType)
}

func validateEventOccurred(m config.MetricDefinition) error {
	if m.MaxEventCode == 0 {
		return fmt.Errorf("No max_event_code specified for metric of type EVENT_OCCURRED.")
	}

	return validateEventCodes(m)
}
func validateIntHistogram(m config.MetricDefinition) error {
	if m.IntBuckets == nil {
		return fmt.Errorf("No int_buckets specified for metric of type INT_HISTOGRAM.")
	}

	// TODO(azani): Validate bucket definition.

	return validateEventCodes(m)
}

func validateStringUsed(m config.MetricDefinition) error {
	if len(m.EventCodes) > 0 {
		return fmt.Errorf("event_codes must not be set for metrics of type STRING_USED")
	}
	return nil
}

// TODO(ninai): remove this function when Cobalt 1.0 is done.
func validateCustomOld(m config.MetricDefinition) error {
	if len(m.EventCodes) > 0 {
		return fmt.Errorf("event_codes must not be set for metrics of type CUSTOM")
	}
	if len(m.Parts) == 0 {
		return fmt.Errorf("No parts specified for metric of type CUSTOM.")
	}
	for n := range m.Parts {
		if !validNameRegexp.MatchString(n) {
			return fmt.Errorf("Invalid part name '%s'. Part names must match the regular expression '%v'", n, validNameRegexp)
		}
		// TODO(azani): Validate the MetricPart itself.
	}
	return nil
}

func validateCustom(m config.MetricDefinition) error {
	if err := validateCustomOld(m); err == nil {
		return nil
	}

	if len(m.EventCodes) > 0 {
		return fmt.Errorf("event_types must not be set for metrics of type CUSTOM")
	}

	if m.ProtoName == "" {
		return fmt.Errorf("Missing proto_name. Proto names are required for metrics of type CUSTOM.")
	}

	if !validProtoNameRegexp.MatchString(m.ProtoName) {
		return fmt.Errorf("Invalid proto_name. Proto names must match the regular expression '%v'.", validProtoNameRegexp)
	}

	return nil
}

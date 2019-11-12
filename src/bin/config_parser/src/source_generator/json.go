// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements outputLanguage for JSON.
package source_generator

import (
	"config"
	"encoding/json"
)

// JSON export structure
// go tags set the field name when exported to JSON data.
type jsonRegistry struct {
	Customers []jsonCustomer `json:"customers"`
}

type jsonCustomer struct {
	Name     string        `json:"name"`
	Id       uint32        `json:"id"`
	Projects []jsonProject `json:"projects"`
}

type jsonProject struct {
	Name    string       `json:"name"`
	Id      uint32       `json:"id"`
	Contact string       `json:"project_contact"`
	Metrics []jsonMetric `json:"metrics"`
}

type jsonMetric struct {
	Name           string       `json:"name"`
	Id             uint32       `json:"id"`
	MetricType     string       `json:"metric_type"`
	Owners         []string     `json:"owners"`
	ExpirationDate string       `json:"expiration_date"`
	Reports        []jsonReport `json:"reports"`
}

type jsonReport struct {
	Name                   string `json:"name"`
	Id                     uint32 `json:"id"`
	ReportType             string `json:"report_type"`
	LocalPrivacyNoiseLevel string `json:"local_privacy_noise_level"`
}

// JSON struct constructors
func makeJSONReport(report *config.ReportDefinition) jsonReport {
	if report == nil {
		return jsonReport{}
	}

	return jsonReport{
		Name:                   report.GetReportName(),
		Id:                     report.GetId(),
		ReportType:             report.GetReportType().String(),
		LocalPrivacyNoiseLevel: report.GetLocalPrivacyNoiseLevel().String(),
	}
}

func makeJSONMetric(metric *config.MetricDefinition) jsonMetric {
	if metric == nil {
		return jsonMetric{}
	}

	var expirationDate string
	var owners []string
	if metric.GetMetaData() != nil {
		expirationDate = metric.GetMetaData().GetExpirationDate()
		owners = metric.GetMetaData().GetOwner()
	}
	var reports []jsonReport
	for _, r := range metric.GetReports() {
		reports = append(reports, makeJSONReport(r))
	}

	return jsonMetric{
		Name:           metric.GetMetricName(),
		Id:             metric.GetId(),
		MetricType:     metric.GetMetricType().String(),
		Owners:         owners,
		ExpirationDate: expirationDate,
		Reports:        reports,
	}
}

func makeJSONProject(project *config.ProjectConfig) jsonProject {
	if project == nil {
		return jsonProject{}
	}

	var metrics []jsonMetric
	for _, m := range project.GetMetrics() {
		metrics = append(metrics, makeJSONMetric(m))
	}

	return jsonProject{
		Name:    project.GetProjectName(),
		Id:      project.GetProjectId(),
		Contact: project.GetProjectContact(),
		Metrics: metrics,
	}
}

func makeJSONCustomer(customer *config.CustomerConfig) jsonCustomer {
	if customer == nil {
		return jsonCustomer{}
	}

	var projects []jsonProject
	for _, p := range customer.GetProjects() {
		projects = append(projects, makeJSONProject(p))
	}

	return jsonCustomer{
		Name:     customer.GetCustomerName(),
		Id:       customer.GetCustomerId(),
		Projects: projects,
	}
}

func makeJSONRegistry(registry *config.CobaltRegistry) jsonRegistry {
	if registry == nil {
		return jsonRegistry{}
	}

	var customers []jsonCustomer
	for _, c := range registry.GetCustomers() {
		customers = append(customers, makeJSONCustomer(c))
	}

	return jsonRegistry{
		Customers: customers,
	}
}

// Outputs the registry contents in JSON
func JSONOutput(_, filtered *config.CobaltRegistry) (outputBytes []byte, err error) {
	jsonRegistry := makeJSONRegistry(filtered)

	prefix := ""
	indent := "  "
	jsonBytes, err := json.MarshalIndent(jsonRegistry, prefix, indent)
	if err != nil {
		return nil, err
	}

	return jsonBytes, nil
}

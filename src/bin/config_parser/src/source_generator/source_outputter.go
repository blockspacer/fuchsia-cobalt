// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements output formatting for the cobalt config parser.

package source_generator

import (
	"bytes"
	"config"
	"fmt"
	"sort"
	"strconv"
	"strings"
)

type outputLanguage interface {
	getCommentPrefix() string

	writeExtraHeader(so *sourceOutputter, projectName, customerName string, namespaces []string)
	writeExtraFooter(so *sourceOutputter, projectName, customerName string, namespaces []string)
	writeEnumBegin(so *sourceOutputter, name ...string)
	writeEnumEntry(so *sourceOutputter, value uint32, name ...string)
	writeEnumAliasesBegin(so *sourceOutputter, name ...string)
	writeEnumAlias(so *sourceOutputter, name, from, to []string)
	writeEnumEnd(so *sourceOutputter, name ...string)
	writeEnumExport(so *sourceOutputter, enumName, name []string)
	writeNamespaceBegin(so *sourceOutputter, name ...string)
	writeNamespaceEnd(so *sourceOutputter)
	writeConstInt(so *sourceOutputter, value uint32, name ...string)
	writeStringConstant(so *sourceOutputter, value string, name ...string)
}

type OutputFormatter func(c, filtered *config.CobaltRegistry) (outputBytes []byte, err error)

type sourceOutputter struct {
	buffer     *bytes.Buffer
	language   outputLanguage
	varName    string
	namespaces []string
	forTesting bool
}

func newSourceOutputter(language outputLanguage, varName string, forTesting bool) *sourceOutputter {
	return newSourceOutputterWithNamespaces(language, varName, []string{}, forTesting)
}

func newSourceOutputterWithNamespaces(language outputLanguage, varName string, namespaces []string, forTesting bool) *sourceOutputter {
	return &sourceOutputter{
		buffer:     new(bytes.Buffer),
		language:   language,
		varName:    varName,
		namespaces: namespaces,
		forTesting: forTesting,
	}
}

func (so *sourceOutputter) writeLine(str string) {
	so.buffer.WriteString(str + "\n")
}

func (so *sourceOutputter) writeLineFmt(format string, args ...interface{}) {
	so.writeLine(fmt.Sprintf(format, args...))
}

func (so *sourceOutputter) writeComment(comment string) {
	for _, comment_line := range strings.Split(comment, "\n") {
		so.writeLineFmt("%s %s", so.language.getCommentPrefix(), strings.TrimLeft(comment_line, " "))
	}
}

func (so *sourceOutputter) writeCommentFmt(format string, args ...interface{}) {
	so.writeComment(fmt.Sprintf(format, args...))
}

func (so *sourceOutputter) writeGenerationWarning() {
	so.writeCommentFmt(`This file was generated by Cobalt's Config parser based on the configuration
YAML in the cobalt_config repository. Edit the YAML there to make changes.`)
}

// writeIdConstants prints out a list of constants to be used in testing. It
// uses the Name attribute of each Metric, Report, and Encoding to construct the
// constants.
//
// For a metric named "SingleString" the constant would be kSingleStringMetricId
// For a report named "Test" the constant would be kTestReportId
// For an encoding named "Forculus" the canstant would be kForculusEncodingId
func (so *sourceOutputter) writeIdConstants(constType string, entries map[uint32]string) {
	if len(entries) == 0 {
		return
	}
	var keys []uint32
	for k := range entries {
		keys = append(keys, k)
	}
	sort.Slice(keys, func(i, j int) bool { return keys[i] < keys[j] })

	so.writeCommentFmt("%s ID Constants", constType)
	for _, id := range keys {
		name := entries[id]
		so.writeComment(name)
		so.language.writeConstInt(so, id, name, constType, "id")
	}
	so.writeLine("")
}

// writeEnum prints out an enum with a for list of EventCodes (cobalt v1.0 only)
//
// It prints out the event_code string using toIdent, (event_code => EventCode).
// In c++ and Dart, it also creates a series of constants that effectively
// export the enum values. For a metric called "foo_bar" with a event named
// "baz", it would generate the constant:
// "FooBarEventCode_Baz = FooBarEventCode::Baz"
func (so *sourceOutputter) writeEnum(prefix string, suffix string, entries map[uint32]string, aliases map[string]string) {
	if len(entries) == 0 {
		return
	}

	var keys []uint32
	for k := range entries {
		keys = append(keys, k)
	}
	sort.Slice(keys, func(i, j int) bool { return keys[i] < keys[j] })

	so.writeCommentFmt("Enum for %s (%s)", prefix, suffix)
	so.language.writeEnumBegin(so, prefix, suffix)
	for _, id := range keys {
		name := entries[id]
		so.language.writeEnumEntry(so, id, name)
	}
	if len(aliases) > 0 {
		so.language.writeEnumAliasesBegin(so, prefix, suffix)
		for from, to := range aliases {
			so.language.writeEnumAlias(so, []string{prefix, suffix}, []string{from}, []string{to})
		}
	}
	so.language.writeEnumEnd(so, prefix, suffix)
	for _, id := range keys {
		so.language.writeEnumExport(so, []string{prefix, suffix}, []string{entries[id]})
	}
	if len(aliases) > 0 {
		for _, to := range aliases {
			so.language.writeEnumExport(so, []string{prefix, suffix}, []string{to})
		}
	}

	so.writeLine("")
}

func (so *sourceOutputter) Bytes() []byte {
	return so.buffer.Bytes()
}

func (so *sourceOutputter) writeLegacyConstants(c *config.CobaltRegistry) {
	metrics := make(map[uint32]string)
	for _, metric := range c.MetricConfigs {
		if metric.Name != "" {
			metrics[metric.Id] = metric.Name
		}
	}
	// write out the 'Metric' constants (e.g. kTestMetricId)
	so.writeIdConstants("Metric", metrics)

	reports := make(map[uint32]string)
	for _, report := range c.ReportConfigs {
		if report.Name != "" {
			reports[report.Id] = report.Name
		}
	}
	// write out the 'Report' constants (e.g. kTestReportId)
	so.writeIdConstants("Report", reports)

	encodings := make(map[uint32]string)
	for _, encoding := range c.EncodingConfigs {
		if encoding.Name != "" {
			encodings[encoding.Id] = encoding.Name
		}
	}
	// write out the 'Encoding' constants (e.g. kTestEncodingId)
	so.writeIdConstants("Encoding", encodings)
}

func (so *sourceOutputter) writeV1Constants(c *config.CobaltRegistry) error {
	metrics := make(map[uint32]string)
	reports := make(map[uint32]string)
	if len(c.Customers) > 1 || len(c.Customers[0].Projects) > 1 {
		return fmt.Errorf("Cobalt v1.0 output can only be used with a single project config.")
	}
	so.writeNames(c)
	for _, metric := range c.Customers[0].Projects[0].Metrics {
		if metric.MetricName != "" {
			metrics[metric.Id] = metric.MetricName
			for _, report := range metric.Reports {
				if report.ReportName != "" {
					reports[report.Id] = fmt.Sprintf("%s", report.ReportName)
				}
			}
		}
	}
	so.writeIdConstants("Metric", metrics)
	if so.forTesting {
		so.writeIdConstants("Report", reports)
	}

	for _, metric := range c.Customers[0].Projects[0].Metrics {
		if len(metric.MetricDimensions) > 0 {
			for i, md := range metric.MetricDimensions {
				events := make(map[uint32]string)
				eventNames := make(map[string]uint32)
				for value, name := range md.EventCodes {
					events[value] = name
					eventNames[name] = value
				}
				varname := "Metric Dimension " + strconv.Itoa(i)
				if md.Dimension != "" {
					varname = "Metric Dimension " + md.Dimension
				}
				so.writeEnum(metric.MetricName, varname, events, md.EventCodeAliases)
			}
		}
	}

	return nil
}

func (so *sourceOutputter) writeNames(c *config.CobaltRegistry) {
	so.language.writeStringConstant(so, c.Customers[0].CustomerName, "Customer Name")
	so.language.writeStringConstant(so, c.Customers[0].Projects[0].ProjectName, "Project Name")
}

func (so *sourceOutputter) writeFile(c, filtered *config.CobaltRegistry) error {
	so.writeGenerationWarning()

	customer := ""
	project := ""

	for _, cust := range c.Customers {
		customer = strings.TrimLeft(customer+"_"+cust.CustomerName, "_")
		for _, proj := range cust.Projects {
			project = strings.TrimLeft(project+"_"+proj.ProjectName, "_")
		}
	}

	so.language.writeExtraHeader(so, customer, project, so.namespaces)

	for _, name := range so.namespaces {
		so.language.writeNamespaceBegin(so, name)
	}

	if len(c.Customers) == 1 && len(c.Customers[0].Projects) == 1 {
		if err := so.writeV1Constants(c); err != nil {
			return err
		}
	} else if len(c.MetricConfigs) > 0 || len(c.ReportConfigs) > 0 || len(c.EncodingConfigs) > 0 {
		so.writeLegacyConstants(c)
	}

	b64Bytes, err := Base64Output(c, filtered)
	if err != nil {
		return err
	}

	so.writeComment("The base64 encoding of the bytes of a serialized CobaltRegistry proto message.")
	so.language.writeStringConstant(so, string(b64Bytes), so.varName)

	for _, _ = range so.namespaces {
		so.language.writeNamespaceEnd(so)
	}

	so.language.writeExtraFooter(so, customer, project, so.namespaces)

	return nil
}

func (so *sourceOutputter) getOutputFormatter() OutputFormatter {
	return func(c, filtered *config.CobaltRegistry) (outputBytes []byte, err error) {
		err = so.writeFile(c, filtered)
		return so.Bytes(), err
	}
}

func getOutputFormatter(format, namespace, varName string, forTesting bool) (OutputFormatter, error) {
	namespaceList := []string{}
	if namespace != "" {
		namespaceList = strings.Split(namespace, ".")
	}
	switch format {
	case "bin":
		return BinaryOutput, nil
	case "b64":
		return Base64Output, nil
	case "cpp":
		return CppOutputFactory(varName, namespaceList, forTesting), nil
	case "dart":
		return DartOutputFactory(varName, forTesting), nil
	case "rust":
		return RustOutputFactory(varName, namespaceList, forTesting), nil
	default:
		return nil, fmt.Errorf("'%v' is an invalid out_format parameter. 'bin', 'b64', 'cpp', 'dart', and 'rust' are the only valid values for out_format.", format)
	}
}

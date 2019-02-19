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

	writeExtraHeader(so *sourceOutputter)
	writeEnumBegin(so *sourceOutputter, name ...string)
	writeEnumEntry(so *sourceOutputter, value uint32, name ...string)
	writeEnumEnd(so *sourceOutputter, name ...string)
	writeEnumAlias(so *sourceOutputter, enumName, name []string)
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
}

func newSourceOutputter(language outputLanguage, varName string) *sourceOutputter {
	return newSourceOutputterWithNamespaces(language, varName, []string{})
}

func newSourceOutputterWithNamespaces(language outputLanguage, varName string, namespaces []string) *sourceOutputter {
	return &sourceOutputter{
		buffer:     new(bytes.Buffer),
		language:   language,
		varName:    varName,
		namespaces: namespaces,
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
func (so *sourceOutputter) writeEnum(prefix string, suffix string, entries map[uint32]string) {
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
	so.language.writeEnumEnd(so, prefix, suffix)
	for _, id := range keys {
		so.language.writeEnumAlias(so, []string{prefix, suffix}, []string{entries[id]})
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
	if len(c.Customers) > 1 || len(c.Customers[0].Projects) > 1 {
		return fmt.Errorf("Cobalt v1.0 output can only be used with a single project config.")
	}
	for _, metric := range c.Customers[0].Projects[0].Metrics {
		if metric.MetricName != "" {
			metrics[metric.Id] = metric.MetricName
		}
	}
	so.writeIdConstants("Metric", metrics)

	for _, metric := range c.Customers[0].Projects[0].Metrics {
		events := make(map[uint32]string)
		for value, name := range metric.EventCodes {
			events[value] = name
		}
		so.writeEnum(metric.MetricName, "EventCode", events)

		if len(metric.MetricDimensions) > 0 {
			for i, md := range metric.MetricDimensions {
				events := make(map[uint32]string)
				for value, name := range md.EventCodes {
					events[value] = name
				}
				varname := "Metric Dimension " + strconv.Itoa(i)
				if md.Dimension != "" {
					varname = "Metric Dimension " + md.Dimension
				}
				so.writeEnum(metric.MetricName, varname, events)
			}
		}
	}

	return nil
}

func (so *sourceOutputter) writeFile(c, filtered *config.CobaltRegistry) error {
	so.writeGenerationWarning()

	so.language.writeExtraHeader(so)

	for _, name := range so.namespaces {
		so.language.writeNamespaceBegin(so, name)
	}

	if len(c.Customers) > 0 {
		so.writeV1Constants(c)
	} else {
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

	return nil
}

func (so *sourceOutputter) getOutputFormatter() OutputFormatter {
	return func(c, filtered *config.CobaltRegistry) (outputBytes []byte, err error) {
		err = so.writeFile(c, filtered)
		return so.Bytes(), err
	}
}

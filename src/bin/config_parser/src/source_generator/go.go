// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements outputLanguage for Go
package source_generator

type Go struct {
	packageName string
}

func (_ Go) getCommentPrefix() string { return "//" }

func (g Go) writeExtraHeader(so *sourceOutputter, projectName, customerName string, namespaces []string) {
	so.writeLineFmt("package %s", g.packageName)
}

func (_ Go) writeExtraFooter(so *sourceOutputter, projectName, customerName string, namespaces []string) {
}

func (_ Go) writeEnumBegin(so *sourceOutputter, name ...string) {
	so.writeLineFmt("type %s uint32", toPascalCase(name...))
	so.writeLine("const (")
	so.writeLineFmt("_ %s = iota", toPascalCase(name...))
}

func (_ Go) writeEnumEntry(so *sourceOutputter, value uint32, name ...string) {
	so.writeLineFmt("  %s = %d", toPascalCase(name...), value)
}

func (_ Go) writeEnumAliasesBegin(so *sourceOutputter, name ...string) {}

func (_ Go) writeEnumAlias(so *sourceOutputter, name, from, to []string) {}

func (_ Go) writeEnumEnd(so *sourceOutputter, name ...string) {
	so.writeLineFmt(")")
}

func (_ Go) writeEnumExport(so *sourceOutputter, enumName, name []string) {}

func (_ Go) writeNamespacesBegin(so *sourceOutputter, namespaces []string) {}

func (_ Go) writeNamespacesEnd(so *sourceOutputter, namespaces []string) {}

func (_ Go) writeConstInt(so *sourceOutputter, value uint32, name ...string) {
	so.writeLineFmt("const %s uint32 = %d;", toPascalCase(name...), value)
}

func (_ Go) writeStringConstant(so *sourceOutputter, value string, name ...string) {
	so.writeLineFmt("const %s string = \"%s\";", toPascalCase(name...), value)
}

// varName will be the name of the variable containing the base64-encoded serialized proto.
// packageName is the name of the go package for the generated code.
// If forTesting is true, a constant will be generated for each report ID, based on the report's name.
func GoOutputFactory(varName string, packageName string, forTesting bool) OutputFormatter {
	return newSourceOutputter(Go{packageName: packageName}, varName, forTesting).getOutputFormatter()
}

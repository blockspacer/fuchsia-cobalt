// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements outputLanguage for C++
package source_generator

import (
	"strings"
)

type CPP struct{}

// Gets header guards of the form:
//
// COBALT_REGISTRY_{PROJECT_NAME}_{CUSTOMER_NAME}_{NAMESPACE}_GEN_
func getHeaderGuard(projectName, customerName string, namespaces []string) string {
	base := toUpperSnakeCase("cobalt", "registry", projectName, customerName)
	if len(namespaces) > 0 {
		base = toUpperSnakeCase(base, strings.Join(namespaces, " "))
	}
	return toUpperSnakeCase(base, "gen") + "_"
}

func (_ CPP) getCommentPrefix() string { return "//" }

func (_ CPP) writeExtraHeader(so *sourceOutputter, projectName, customerName string, namespaces []string) {
	if projectName == "" || customerName == "" {
		so.writeLine("#pragma once")
	} else {
		guard := getHeaderGuard(projectName, customerName, namespaces)
		so.writeLine("#ifndef " + guard)
		so.writeLine("#define " + guard)
	}
	so.writeLine("")
	so.writeLine("#include <cstdint>")
	so.writeLine("")
}

func (_ CPP) writeExtraFooter(so *sourceOutputter, projectName, customerName string, namespaces []string) {
	if projectName != "" && customerName != "" {
		so.writeLine("")
		so.writeLine("#endif  // " + getHeaderGuard(projectName, customerName, namespaces))
	}
}

func enumNamespace(name ...string) string {
	name = append(name, "scope")
	return toSnakeCase(name...)
}

func (_ CPP) writeEnumBegin(so *sourceOutputter, name ...string) {
	so.writeLineFmt("namespace %s {", enumNamespace(name...))
	so.writeLine("enum Enum {")
}

func (_ CPP) writeEnumEntry(so *sourceOutputter, value uint32, name ...string) {
	so.writeLineFmt("  %s = %d,", toPascalCase(name...), value)
}

// C++ allows duplicate keys in an enum, so nothing needs to be done here.
func (_ CPP) writeEnumAliasesBegin(so *sourceOutputter, name ...string) {
}

func (_ CPP) writeEnumAlias(so *sourceOutputter, name, from, to []string) {
	so.writeLineFmt("  %s = %s,", toPascalCase(to...), toPascalCase(from...))
}

func (_ CPP) writeEnumEnd(so *sourceOutputter, name ...string) {
	so.writeLine("};")
	so.writeLineFmt("}  // %s", enumNamespace(name...))

	so.writeLineFmt("using %s = %s::Enum;", toPascalCase(name...), enumNamespace(name...))
}

func (_ CPP) writeEnumExport(so *sourceOutputter, enumName, name []string) {
	enum := toPascalCase(enumName...)
	variant := toPascalCase(name...)
	so.writeLineFmt("const %s %s_%s = %s::%s;", enum, enum, variant, enum, variant)
}

func getNamespaces(namespaces []string) string {
	ns := make([]string, len(namespaces))
	for i, v := range namespaces {
		ns[i] = toSnakeCase(v)
	}
	return strings.Join(ns, "::")
}

func (_ CPP) writeNamespacesBegin(so *sourceOutputter, namespaces []string) {
	if len(namespaces) > 0 {
		so.writeLineFmt("namespace %s {", getNamespaces(namespaces))
	}
}

func (_ CPP) writeNamespacesEnd(so *sourceOutputter, namespaces []string) {
	if len(namespaces) > 0 {
		so.writeLineFmt("}  // %s", getNamespaces(namespaces))
	}
}

func (_ CPP) writeConstInt(so *sourceOutputter, value uint32, name ...string) {
	name = append([]string{"k"}, name...)
	so.writeLineFmt("const uint32_t %s = %d;", toCamelCase(name...), value)
}

func (_ CPP) writeStringConstant(so *sourceOutputter, value string, name ...string) {
	name = append([]string{"k"}, name...)
	so.writeLineFmt("const char %s[] = \"%s\";", toCamelCase(name...), value)
}

// Returns an output formatter that will output the contents of a C++ header
// file that contains a variable declaration for a string literal that contains
// the base64-encoding of the serialized proto.
//
// varName will be the name of the variable containing the base64-encoded serialized proto.
// namespace is a list of nested namespaces inside of which the variable will be defined.
// If forTesting is true, a constant will be generated for each report ID, based on the report's name.
func CppOutputFactory(varName string, namespace []string, forTesting bool) OutputFormatter {
	return newSourceOutputterWithNamespaces(CPP{}, varName, namespace, forTesting).getOutputFormatter()
}

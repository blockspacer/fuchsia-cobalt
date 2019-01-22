// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements outputLanguage for C++
package source_generator

type CPP struct{}

func (_ CPP) getCommentPrefix() string { return "//" }

func (_ CPP) writeExtraHeader(so *sourceOutputter) {
	so.writeLine("#pragma once")
	so.writeLine("")
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

func (_ CPP) writeEnumEnd(so *sourceOutputter, name ...string) {
	so.writeLine("};")
	so.writeLineFmt("}  // %s", enumNamespace(name...))

	so.writeLineFmt("typedef %s::Enum %s;", enumNamespace(name...), toPascalCase(name...))
}

func (_ CPP) writeEnumAlias(so *sourceOutputter, enumName, name []string) {
	enum := toPascalCase(enumName...)
	variant := toPascalCase(name...)
	so.writeLineFmt("const %s %s_%s = %s::%s;", enum, enum, variant, enum, variant)
}

func (_ CPP) writeNamespaceBegin(so *sourceOutputter, name ...string) {
	so.writeLineFmt("namespace %s {", toSnakeCase(name...))
}

func (_ CPP) writeNamespaceEnd(so *sourceOutputter) {
	so.writeLine("}")
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
func CppOutputFactory(varName string, namespace []string) OutputFormatter {
	return newSourceOutputterWithNamespaces(CPP{}, varName, namespace).getOutputFormatter()
}

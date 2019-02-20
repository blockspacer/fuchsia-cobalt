// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implement outputLanguage for Rust
package source_generator

type Rust struct{}

func (_ Rust) getCommentPrefix() string { return "//" }

func (_ Rust) writeExtraHeader(so *sourceOutputter) {}

func (_ Rust) writeEnumBegin(so *sourceOutputter, name ...string) {
	so.writeLineFmt("pub enum %s {", toPascalCase(name...))
}

func (_ Rust) writeEnumEntry(so *sourceOutputter, value uint32, name ...string) {
	so.writeLineFmt("  %s = %d,", toPascalCase(name...), value)
}

func (_ Rust) writeEnumEnd(so *sourceOutputter, name ...string) {
	so.writeLineFmt("}")
}

// We don't alias Enums in rust, since this can easily be accomplished with a
// use EnumName::*;
func (_ Rust) writeEnumAlias(so *sourceOutputter, enumName, name []string) {}

func (_ Rust) writeNamespaceBegin(so *sourceOutputter, name ...string) {
	so.writeLineFmt("pub mod %s {", toSnakeCase(name...))
}

func (_ Rust) writeNamespaceEnd(so *sourceOutputter) {
	so.writeLine("}")
}

func (_ Rust) writeConstInt(so *sourceOutputter, value uint32, name ...string) {
	so.writeLineFmt("pub const %s = %d;", toUpperSnakeCase(name...), value)
}

func (_ Rust) writeStringConstant(so *sourceOutputter, value string, name ...string) {
	so.writeLineFmt("pub const %s: &str = \"%s\";", toUpperSnakeCase(name...), value)
}

// varName will be the name of the variable containing the base64-encoded serialized proto.
// namespace is a list of nested namespaces inside of which the variable will be defined.
// If forTesting is true, a constant will be generated for each report ID, based on the report's name.
func RustOutputFactory(varName string, namespace []string, forTesting bool) OutputFormatter {
	return newSourceOutputterWithNamespaces(Rust{}, varName, namespace, forTesting).getOutputFormatter()
}

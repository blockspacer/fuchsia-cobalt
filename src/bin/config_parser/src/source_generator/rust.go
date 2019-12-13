// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements outputLanguage for Rust
package source_generator

import "strings"

type Rust struct{}

func (_ Rust) getCommentPrefix() string { return "//" }
func (_ Rust) supportsTypeAlias() bool  { return true }

func (_ Rust) writeExtraHeader(so *sourceOutputter, projectName, customerName string, namespaces []string) {
	so.writeLine("use cobalt_client::traits::AsEventCode;")
	so.writeLine("")
}
func (_ Rust) writeExtraFooter(so *sourceOutputter, projectName, customerName string, namespaces []string) {
}

func (_ Rust) writeEnumBegin(so *sourceOutputter, name ...string) {
	so.writeLine("#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]")
	so.writeLineFmt("pub enum %s {", toPascalCase(name...))
}

func (_ Rust) writeEnumEntry(so *sourceOutputter, value uint32, name ...string) {
	so.writeLineFmt("  %s = %d,", toPascalCase(name...), value)
}

func (_ Rust) writeEnumAliasesBegin(so *sourceOutputter, name ...string) {
	so.writeLine("}")
	so.writeLineFmt("impl %s {", toPascalCase(name...))
}

func (_ Rust) writeEnumAlias(so *sourceOutputter, name, from, to []string) {
	so.writeLine("  #[allow(non_upper_case_globals)]")
	so.writeLineFmt("  pub const %s: %s = %s::%s;", toPascalCase(to...), toPascalCase(name...), toPascalCase(name...), toPascalCase(from...))
}

func (_ Rust) writeEnumEnd(so *sourceOutputter, name ...string) {
	so.writeLine("}")
	so.writeLine("")
	so.writeLineFmt("impl AsEventCode for %s {", toPascalCase(name...))
	so.writeLine("  fn as_event_code(&self) -> u32 {")
	so.writeLine("    *self as u32")
	so.writeLine("  }")
	so.writeLine("}")

}

// We don't alias Enums in rust, since this can easily be accomplished with a
// use EnumName::*;
func (_ Rust) writeEnumExport(so *sourceOutputter, enumName, name []string) {}

func (_ Rust) writeTypeAlias(so *sourceOutputter, from, to []string) {
	so.writeLineFmt("pub use %s as %s;", toPascalCase(from...), toPascalCase(to...))
}

func (_ Rust) writeNamespacesBegin(so *sourceOutputter, namespaces []string) {
	if len(namespaces) > 0 {
		ns := make([]string, len(namespaces))
		for i, v := range namespaces {
			ns[i] = toSnakeCase(v)
		}
		so.writeLineFmt("pub mod %s {", strings.Join(ns, "::"))
	}
}

func (_ Rust) writeNamespacesEnd(so *sourceOutputter, namespaces []string) {
	if len(namespaces) > 0 {
		so.writeLine("}")
	}
}

func (_ Rust) writeConstUint32(so *sourceOutputter, value uint32, name ...string) {
	so.writeLineFmt("pub const %s: u32 = %d;", toUpperSnakeCase(name...), value)
}

func (_ Rust) writeConstInt64(so *sourceOutputter, value int64, name ...string) {
	so.writeLineFmt("pub const %s: i64 = %d;", toUpperSnakeCase(name...), value)
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

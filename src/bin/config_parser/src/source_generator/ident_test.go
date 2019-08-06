// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source_generator

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

var identTests = []struct {
	input          []string
	snakeCase      string
	upperSnakeCase string
	pascalCase     string
	camelCase      string
}{
	{
		input:          []string{"This is a test"},
		snakeCase:      "this_is_a_test",
		upperSnakeCase: "THIS_IS_A_TEST",
		pascalCase:     "ThisIsATest",
		camelCase:      "thisIsATest",
	},
	{
		input:          []string{"I", "don't", "know", "what_i", "expected"},
		snakeCase:      "i_don_t_know_what_i_expected",
		upperSnakeCase: "I_DON_T_KNOW_WHAT_I_EXPECTED",
		pascalCase:     "IDon_tKnowWhatIExpected",
		camelCase:      "iDon_tKnowWhatIExpected",
	},
	{
		input:          []string{"0", "TEST"},
		snakeCase:      "_0_test",
		upperSnakeCase: "_0_TEST",
		pascalCase:     "_0Test",
		camelCase:      "_0Test",
	},
	{
		input:          []string{"more", "word(s)"},
		snakeCase:      "more_word_s_",
		upperSnakeCase: "MORE_WORD_S_",
		pascalCase:     "MoreWord_s_",
		camelCase:      "moreWord_s_",
	},
	{
		input:          []string{"I Hope This Works (DoesIt?)"},
		snakeCase:      "i_hope_this_works___does_it__",
		upperSnakeCase: "I_HOPE_THIS_WORKS___DOES_IT__",
		pascalCase:     "IHopeThisWorks_DoesIt__",
		camelCase:      "iHopeThisWorks_DoesIt__",
	},
}

func TestIdentFunctions(t *testing.T) {
	for _, tt := range identTests {
		if diff := cmp.Diff(toSnakeCase(tt.input...), tt.snakeCase); diff != "" {
			t.Errorf("snakeCase doesn't match expected: %s", diff)
		}

		if diff := cmp.Diff(toUpperSnakeCase(tt.input...), tt.upperSnakeCase); diff != "" {
			t.Errorf("upperSnakeCase doesn't match expected: %s", diff)
		}

		if diff := cmp.Diff(toPascalCase(tt.input...), tt.pascalCase); diff != "" {
			t.Errorf("pascalCase doesn't match expected: %s", diff)
		}

		if diff := cmp.Diff(toCamelCase(tt.input...), tt.camelCase); diff != "" {
			t.Errorf("camelCase doesn't match expected: %s", diff)
		}
	}
}

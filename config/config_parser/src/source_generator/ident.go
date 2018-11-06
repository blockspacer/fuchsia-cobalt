// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements logic for converting arbitrary strings into valid
// idents of various styles.

package source_generator

import (
	"regexp"
	"strings"
	"unicode"
)

var (
	wordSeparator     = regexp.MustCompile(`[ _]`)
	validFirstChar    = regexp.MustCompile(`^[a-zA-Z_]`)
	invalidIdentChars = regexp.MustCompile(`[^a-zA-Z0-9_]`)
)

func splitIdentWord(name string) string {
	entries := []string{}
	prevChar := ' '
	curWord := []rune{}
	name = wordSeparator.ReplaceAllString(name, " ")
	for _, char := range name {
		if unicode.IsLetter(char) && unicode.IsUpper(char) && !unicode.IsUpper(prevChar) {
			if len(curWord) > 0 && len(strings.Trim(string(curWord), " ")) > 0 {
				entries = append(entries, strings.Trim(strings.ToLower(string(curWord)), " "))
			}
			curWord = []rune{char}
		} else {
			curWord = append(curWord, char)
		}
		prevChar = char
	}
	if len(curWord) > 0 {
		entries = append(entries, strings.Trim(strings.ToLower(string(curWord)), " "))
	}
	return strings.Join(entries, " ")
}

// splitIdentWords takes an arbitrary string and splits along word boundaries.
//
// Examples:
//
//   splitIdentWords("ThisIsAString") = "this is a string"
//   splitIdentWords("this_is_a_string") = "this is a string"
func splitIdentWords(nameParts ...string) string {
	var retParts []string
	for _, part := range nameParts {
		retParts = append(retParts, splitIdentWord(part))
	}
	return strings.Join(retParts, " ")
}

// simpleTitle is a simplified version of strings.Title that more appropriately
// matches what we is needed for pascalCase and snakeCase.
func simpleTitle(name string) string {
	words := []string{}
	for _, word := range strings.Split(wordSeparator.ReplaceAllString(name, " "), " ") {
		if len(word) == 0 {
			continue
		}
		runes := []rune(word)
		runes[0] = unicode.ToUpper(runes[0])
		words = append(words, string(runes))
	}
	return strings.Join(words, " ")
}

// sanitizeIdent takes an ident and replaces all invalid characters with _.
//
// Examples:
//   sanitizeIdent("0Test") = "_0Test"
//   sanitizeIdent("THIS IS A TEST") = "THIS_IS_A_TEST"
//   sanitizeIdent("IHopeThisWorks(DoesIt?)") = "IHopeThisWorks_DoesIT__"
func sanitizeIdent(name string) string {
	if !validFirstChar.MatchString(name) {
		name = "_" + name
	}

	return invalidIdentChars.ReplaceAllString(name, "_")
}

// toSnakeCase converts an arbitrary string into snake_case (should be a valid
// ident in all supported languages).
//
// Examples:
//
//   toSnakeCase("this is a string") = "this_is_a_string"
//   toSnakeCase("testing_a_string") = "testing_a_string"
//   toSnakeCase("more word(s)") = "more_word_s_"
func toSnakeCase(nameParts ...string) string {
	return sanitizeIdent(strings.ToLower(splitIdentWords(nameParts...)))
}

// toUpperSnakeCase converts an arbitrary string into UPPER_SNAKE_CASE (should
// be a valid ident in all supported languages).
//
// Examples:
//
//   toUpperSnakeCase("this is a string") = "THIS_IS_A_STRING"
//   toUpperSnakeCase("testing_a_string") = "TESTING_A_STRING"
//   toUpperSnakeCase("more word(s)") = "MORE_WORD_S_"
func toUpperSnakeCase(nameParts ...string) string {
	return strings.ToUpper(toSnakeCase(nameParts...))
}

// toPascalCase converts an arbitrary string into PascalCase (should be a valid
// ident in all supported languages).
//
// Examples:
//
//   toPascalCase("this is a string") = "ThisIsAString"
//   toPascalCase("testing_a_string") = "TestingAString"
//   toPascalCase("more word(s)") = "MoreWord_s_"
func toPascalCase(nameParts ...string) string {
	capitalizedWords := simpleTitle(splitIdentWords(nameParts...))
	return sanitizeIdent(strings.Join(strings.Split(capitalizedWords, " "), ""))
}

// toCamelCase converts an arbitrary string into camelCase (should be a valid
// ident in all supported languages).
//
// Examples:
//
//   toCamelCase("this is a string") = "thisIsAString"
//   toCamelCase("testing_a_string") = "testingAString"
//   toCamelCase("more word(s)") = "moreWord_s_"
func toCamelCase(nameParts ...string) string {
	pascal := toPascalCase(nameParts...)
	runes := []rune(pascal)
	runes[0] = unicode.ToLower(runes[0])
	return string(runes)
}

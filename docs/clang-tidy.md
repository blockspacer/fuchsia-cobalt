# Lint

We use clang-tidy to lint C++ code and aim to keep the repository warning-clean.
The linter is configured in the root level `.clang-tidy` file. Developers
should not create additional configuration files at a lower level, as
this will cause disagreements in the tree.

## How to lint

`./cobaltb.py lint` is a Cobalt script that wraps language-specific linters in a common
command line interface. It gathers a list of files, based on the options you
specify, separates them by matching linter, and executes each required linter.
`clang-tidy` is used for C and C++ files.

Without any other arguments, `./cobaltb.py lint` lints all source files, and passes them through the
linter:

```
./cobaltb.py lint
```

To restrict linting to only a specific directory, add the directory as an argument:

```
./cobaltb.py lint <dir>
```

## Suppressing warnings

Any warning can be suppressed by adding a `// NOLINT(<check_name>)` or a
`// NOLINTNEXTLINE(<check_name>)` comment to the offending line. It is also
possible to disable the check entirely within the repository by editing the
root level `.clang-tidy` file.

## Checks

There are a number of check categories enabled, and specific checks within them
have been disabled for the reasons below. The list of enabled check categories
is as follows:

 - `bugprone-*`
 - `clang-diagnostic-*`
 - `google-*`
 - `misc-*`
 - `modernize-`
 - `performance-*`
 - `readability-*`

This list tracks the reasons for which we disabled in particular [checks]:

 - `clang-diagnostic-unused-command-line-argument` - ninja-generated compilation
    database contains the linker argument which ends up unused and triggers this
    warning for every file
 - `misc-noexcept*` - Cobalt doesn't use C++ exceptions
 - `misc-non-private-member-variables-in-classes` - We don't allow classes/structs
    with a mix of private and public members, but all public is fine.
 - `modernize-avoid-c-arrays` - Cobalt makes use of raw C arrays in some performance critical
    calculations.
 - `modernize-concat-nested-namespaces` - Cobalt currently needs to maintain compatibility with
    c++11, which does not support nested namespaces.
 - `modernize-deprecated-headers` - Cobalt uses old-style C headers
 - `modernize-raw-string-literal` - the check was suggesting to convert `\xFF`
    literals, which we'd rather keep in the escaped form.
 - `modernize-return-braced-init-list` - concerns about readability of returning
    braced initialization list for constructor arguments, prefer to use a
    constructor explicitly
 - `modernize-use-equals-delete` - flagging all gtest TEST_F
 - `modernize-use-trailing-return-type` - Cobalt C++ code typically uses the
    `int foo()` style of defining functions, and not the `auto foo() -> int`
    style as recommended by this check.
 - `readability-implicit-bool-conversion` - Cobalt C++ code commonly uses implicit
    bool cast of pointers and numbers
 - `readability-isolate-declaration` - Zircon code commonly uses paired declarations.
 - `readability-uppercase-literal-suffix` - Cobalt C++ code chooses not to impose
    a style on this.

[checks]: https://clang.llvm.org/extra/clang-tidy/checks/list.html

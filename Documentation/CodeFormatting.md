# Code formatting

Include `<Mib/Develop/CodeFormatting>` and use the types in `NMib::NDevelop`.
The engine is pure: it reads immutable source bytes, produces an edit plan and
diagnostics, and performs no filesystem access. `MTool Format` applies the plan
and `MTool Validate` reports it without writing, so both commands share one
interpretation of the rules.

## Opting a file in

Formatting is selected by the consumer-owned EditorConfig property
`malterlib_format`:

```editorconfig
[*.{c,cc,cpp,cxx,h,hh,hpp,hxx}]
malterlib_format = malterlib

[Vendor/**]
malterlib_format = off
```

`malterlib` selects the Malterlib profile, `off` disables formatting, and the
generic `unset` removes an inherited value. A missing property also disables
formatting. Any other value is rejected, so a typo cannot read as "disabled".

Auditing an existing exclusion section matters: clearing `max_line_length` does
not disable this separate property.

## Settings

`CCodeFormattingSettings` validates the resolved properties in one place. The
profile defaults are tabs, four-column tab stops, and 190 columns. Explicit
standard properties take precedence:

| Property | Effect |
| --- | --- |
| `indent_style` | `tab` or `space`. |
| `indent_size`, `tab_width` | Tab stop width; `tab_width` wins, then `indent_size`. |
| `max_line_length` | Column limit, or `off`. Overrides the profile default of 190. |
| `trim_trailing_whitespace` | Defaults to true under the profile. |
| `insert_final_newline` | Defaults to true under the profile. |
| `end_of_line` | `lf`, `crlf`, or `cr`. Unset preserves the file's representation. |
| `charset` | Constraint only. `utf-8` and `utf-8-bom` are supported; anything else is reported as unsupported and left untouched. |

`m_nMaxColumns` stays zero when neither the profile nor `max_line_length`
supplies a limit, which is how a consumer decides that no validator applies.

`fg_DetectCodeLanguage` maps a path to `ECodeLanguage`. C and C++ sources and
headers are supported, including extensionless public include wrappers. Other
languages have no syntax backend and are never rewritten.

## Requests and results

```cpp
NDevelop::CCodeFormattingRequest Request;
Request.m_Source = Bytes;
Request.m_Path = "Source/Example.cpp";
Request.m_Language = NDevelop::fg_DetectCodeLanguage(Request.m_Path);
Request.m_Settings = NDevelop::CCodeFormattingSettings(Properties);

auto Result = NDevelop::fg_AnalyzeCodeFormatting(Request);
if (Result.m_Status == NDevelop::ECodeFormattingStatus::mc_Complete)
	auto Formatted = NDevelop::fg_ApplyCodeFormattingEdits(Request.m_Source, Result.m_Edits);
```

`m_Edits` is ordered by original byte offset and non-overlapping.
`fg_ApplyCodeFormattingEdits` validates that before rebuilding the buffer, so a
malformed plan raises instead of corrupting a file.

Every edit also appears in `m_Diagnostics` with `m_bHasAutomaticFix` set.
Constraints the engine cannot fix, such as an indivisible overlong line, appear
as diagnostics without a fix, so they stay visible after a write. Diagnostics
carry original byte spans plus one-based line and column coordinates.

`m_Status` is `mc_Unsupported` when the language, encoding, or source structure
is not handled, and `mc_Failed` when analysis could not produce a usable plan.
`m_Explanation` says which. Unsupported and failed results carry no edits.

## Rules

The implemented rule matrix is whitespace-only. Every plan is verified against
`fg_HasEquivalentCodeTokens`, and whole-file plans are re-analyzed to prove the
result is stable; either check failing is a formatter failure, not an edit.

| Rule | Behavior |
| --- | --- |
| `indentation` | Rewrites leading whitespace in the configured style, preserving the line's existing indentation width. |
| `trailing-whitespace` | Removes spaces and tabs before a line terminator. |
| `final-newline` | Appends a terminator to an unterminated last line. Whole-file requests only. |
| `line-ending` | Converts terminators to `end_of_line`. Whole-file requests only. |
| `clause-space` | Exactly one space between `if`, `for`, `while`, `switch`, or `catch` and its `(` on the same line. |
| `comma-space` | No space before a comma, one space after it on the same line. |
| `operator-space` | One space around unambiguous binary operators. |
| `block-blank-line` | Removes blank lines directly after an opening brace. |
| `case-blank-line` | Removes blank lines directly after a `case` or `default` label. |
| `line-length` | Diagnostic only; the engine never splits a line to satisfy the limit. |

`operator-space` covers `==`, `!=`, `<=`, `>=`, `<=>`, `||`, and the compound
assignments. Plain `=` is deliberately excluded: the same token spells a lambda
capture default and the trailing token of Malterlib's `_o=` and `_j=` DSL. `&`,
`&&`, `*`, `.`, and `->` are excluded because they are also declarators or
member access. An operator immediately following the `operator` keyword is a
declarator name and is left alone.

`indentation` normalizes indentation characters, not indentation depth. The
depth model for split statements, continuations, and clause parentheses is not
implemented yet, so existing structure is preserved rather than guessed.

## Protected regions

Layout inside multiline tokens is never touched: block comments, string and
character literals including raw strings, preprocessor directives with their
continuations, and the line a backslash splice ends on. A leading UTF-8
signature is its own token and is preserved.

A paired directive disables the engine for a region:

```cpp
// malterlib-format off
	int Aligned   = 1;
// malterlib-format on
```

Both lines are excluded along with everything between them. An unmatched or
nested directive is a failure, not a silently ignored comment.

A source that ends inside a comment or literal, contains a byte that cannot
start a token, or is not valid UTF-8 is reported as unsupported and left
untouched.

## Ranges

An empty `m_Ranges` formats the whole file. Otherwise each range is mapped to
original byte coordinates, validated, and merged. A range that is reversed,
out of bounds, or that starts or ends inside an encoded code point or a CRLF
pair is rejected.

`mc_Expand`, the default, grows each selection to the whole lines it touches and
reports the result in `m_EffectiveRanges`. A zero-length range is a cursor: it
selects the line it sits on, and at end of file the preceding line. An empty
file has an empty selection.

`mc_Strict` never modifies bytes outside the requested range. An edit that would
cross the boundary is dropped and reported as a `range-boundary` diagnostic
without an automatic fix, instead of being truncated.

Whole-file normalization, meaning `final-newline` and `line-ending`, only runs
for whole-file requests. Bytes outside the effective ranges are identical after
application.

Consumers that only want to report a subset of lines, such as changed-line
validation, analyze the complete snapshot and filter the resulting diagnostics
by their original line. The engine always sees the whole file, so context is
never guessed from an isolated substring.

## Text layout

`<Mib/Develop/TextLayout>` holds the shared text model. `CTextLineMap` splits a
buffer on LF, CRLF, and CR, recording each terminator so a rewritten file can
preserve the original representation. `fg_MeasureTextColumns` measures display
columns using EditorConfig tab stops, counts each whole code point as one
column, counts the remaining bytes of a malformed sequence, and reports counter
overflow instead of wrapping. `MTool Validate` uses the same function for its
line-length checks, so both commands measure a line identically.

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

The implemented rule matrix is whitespace-only but for two conversions, the
trailing return type and `braces`. Every plan is verified against
`fg_HasEquivalentCodeTokens` on the converted source, and whole-file plans are
re-analyzed to prove the result is stable; either check failing is a formatter
failure, not an edit.

| Rule | Behavior |
| --- | --- |
| `indentation` | Rewrites leading whitespace in the configured style, preserving the width of a line the layout does not place. |
| `trailing-whitespace` | Removes spaces and tabs before a line terminator. |
| `final-newline` | Appends a terminator to an unterminated last line. Whole-file requests only. |
| `line-ending` | Converts terminators to `end_of_line`. Whole-file requests only. |
| `clause-space` | Exactly one space between `if`, `for`, `while`, `switch`, or `catch` and its `(` on the same line. |
| `comma-space` | No space before a comma, one space after it on the same line. |
| `operator-space` | One space around unambiguous binary operators. |
| `angle-space` | No space between the closing markers of two nested template argument lists: `>>`, never `> >`. |
| `token-space` | Every other pair of tokens on one line takes the spelling the standard settles, where it settles one: member access and scope markers hug, a keyword stands apart from its parenthesis, a label's colon, a bit-field's width, and a unary sign hug, a trailing return type's arrow stands apart, an operator between two operands stands apart from both. |
| `block-blank-line` | Removes blank lines directly after an opening brace. |
| `case-blank-line` | Removes blank lines directly after a `case` or `default` label. |
| `line-break` | Brings a split construct back to one line when it fits and nothing forbids it, and gives a block's braces and statements lines of their own. |
| `structure` | Diagnostic only; the file's brackets do not nest as written, so no line of it can be placed and all of them are kept. |
| `line-length` | Diagnostic only, and measured on the formatted result: the lines the other rules break up are no violation, and one they leave too long is named where it stands in the source. |
| `braces` | Around a single statement guarded by `if`, `else`, `for`, or `while`: none when it is laid out as one line, braces when it spans lines or follows a split clause. |

`operator-space` covers `==`, `!=`, `<=`, `>=`, `<=>`, `||`, and the compound
assignments. Plain `=` is excluded here; `token-space` spells it, below. `&`,
`&&`, `*`, `.`, and `->` are excluded because they are also declarators or
member access; `token-space` spells those of them that are operators, below. An
operator immediately following the `operator` keyword is a declarator name and
is left alone.

`braces` is decided on the original source, like the trailing return type
conversion, and the layout is then made on the converted source. A statement
that is laid out as one line, brought onto one where it can be and otherwise
written on one, stands without braces; one laid out across lines, or guarded
by a clause that is split across lines, stands within them. Braces are dropped
only where the change cannot alter what the source says: the block holds
exactly one statement, which ends in `;` and is not itself a block, nothing
but whitespace stands between the braces and that statement, and no directive
is inside. A comment trailing the statement on its line follows it out of the
block; any other comment inside keeps the braces. They are added only around a
statement ending in `;` with nothing but whitespace between the guard and it,
behind an attribute on the clause's line, and behind a comment trailing the
statement's last line. A nested `if` is two statements to the structure
builder, so a block that shields a dangling `else` keeps its braces, and so do
the bodies of `do`, `switch`, `try`, and `catch`. The decision reaches every
guarded statement, a lambda's body included, whose block belongs to the group
the lambda is written in rather than to the statement around it.

`token-space` applies `fg_GetCanonicalSpacing`, the same answers the line-break
rule joins with, to every pair of adjacent tokens on one line, and leaves a pair
alone where that function does not settle the spelling. An operator function's
name is left alone too, since the sources spell it both ways, and so is a `/`
written tight between two names, which is a path in a macro argument.

An operator with an operand on both sides of it is written apart from both,
whatever they are spelled with: `5 * 5`, `nFlags & mc_Mask`, `a + (b | c)`.
Without an operand in front, the same token is the unary form, `-1` and
`*pValue`; without one behind, it belongs to something else, a cast's
`(CFoo *)` or a pack's `&&...`. `*`, `&`, and `&&` stay ambiguous even between
two operands, since a name in front of one can be a type as easily as a value:
`C(CStr &_A)` and `C(a & b)` spell the same tokens. Those three are settled
only where a declaration cannot stand: behind a literal or the closing marker
of a call or a subscript, behind the `=` that ends the declarator part of the
statement or parameter they stand in, or inside an `if`, `while`, or `switch`
condition, which declares nothing without an `=` of its own. A parenthesis that
could close a cast is not such a marker, so `(int)*pValue` keeps its spelling,
while `sizeof`, `alignof`, `typeid`, and `noexcept` yield a value the way a
call does and `decltype` names a type. Everywhere else the pair keeps what the
source has.

A bit-field's width hugs the `:` that introduces it, `uint8 mp_Priority:2 = 0`,
named or not. What stands in front of that `:` is the name a type declares,
which no other colon at a declaration's level has: a label ends its statement
there, a base clause follows a definition's keyword, an initializer list follows
a parameter list however many qualifiers stand between, and a conditional's
colon answers a `?`. An unnamed bit-field, `uint32 : 3`, is spelled like a label
and is none; the width behind the colon is what tells the two apart.

Plain `=` assigns and initializes, and stands apart from both sides. Two
spellings in the sources are not that: a capture default, which the markers
around it settle as `[=]` and `[=, &m_Value]`, and the tail of Malterlib's
`_o=` and `_j=` command-line DSL, which hugs the key in front of it. A DSL
marker is told from every other name by its shape, an underscore with nothing
but lower case behind it, which no declared name in Malterlib has. Settling the
token is also what lets a statement broken at its `=` be measured, and so
joined when it fits and broken further when it does not; what does not fit
gives at its scopes, as in `auto Value = fg_Function` with the argument list
opened under it, never in front of the name.

A pack's ellipsis goes with what the pack is. One that declares a pack hugs the
name it introduces and stands apart from the type in front of it, as in
`typename ...tp_CParams` and `NTraits::TCDecay<tp_CParams> ...p_Params`; one
that expands a pack has no name to hug and is written tight against what it
expands, as in `tp_CParams...>` and `fg_Forward<tp_CParams>(p_Params)...`. The
name behind the ellipsis is what tells the two apart. A declarator in front of
one is the exception: `&&...p_Params` and `&& ...p_Params` are both written,
so that pair keeps what it has, and `sizeof...` and a fold's ellipsis are
spelled by the rules for the tokens around them.

`indentation` normalizes indentation characters, not indentation depth. Depth
is `line-break`'s, described under [Line structure](#line-structure): the level
of a block's statements, and of a split statement's continuations and clause
parentheses. This rule writes the lines that one does not place, and spells the
ones it does the same way, so the two never disagree about a line.

## Line structure

`<Mib/Develop/CodeFormattingStructure>` builds a layout tree over the token
stream: statements, blocks, and bracketed groups, with the split points where a
canonical split form starts a new line. Anything it cannot classify becomes an
unsupported node whose layout is preserved.

Brackets that do not nest as written make every line position a guess, so the
file keeps all of its lines and `structure` says why, naming the opener left
without its closer. A scope ends at a closer of its own kind; another kind's
standing there is what says the two do not nest, whether the file runs out of
tokens first, as an unclosed `{` does, or a closer further down balances the
opener in the place of the one it is missing, which is what an unclosed `(` in
the middle of a file does. The brace of the block a statement stands in is the
exception: it ends that statement, which is how a list written without
terminators is spelled, such as the enumerators of an `enum` body.

A `<` opens a template argument list only when it is written tight against the
name before it, or across a line break; Malterlib spells a comparison with
spaces, so the loose spelling stays an operator. A `>>` that ends a template
argument list is read the way C++ reads it, as two `>` tokens, so that each list
has a closing marker of its own, and two closers written apart are joined back
into one `>>` unless the layout gives the second a line of its own. A brace
holding a statement terminator at its
own level is a block, which is how a lambda body inside an argument list is told
apart from a braced initializer.

`fg_GetCanonicalSpacing` gives the inline separator between two adjacent tokens.
It decides only the spellings the standard settles, and answers `mc_Preserve`
elsewhere, which is what keeps a rewrite from guessing at an ambiguous
construct such as `->`, which is both member access and a trailing return type.

A `*`, `&`, or `&&` is a declarator, separated from its type and hugging the
name it declares, where only a type can stand in front of it: behind `const`,
`volatile`, or another declarator; behind a name or a template argument list
when a separator, a closing marker, an ellipsis, or `=` follows it; and inside
a parameter list, outside a default argument. Behind a parameter list, with
only the function's cv-qualifiers between, the same token is its ref-qualifier
instead. That declares nothing, so what follows it is the rest of the
declaration rather than a name, and stands apart from it: `f_Get() const &
noexcept`. A parameter list is a template
header's, a lambda's, a catch clause's, or a function's. A function's
parenthesis is its parameter list when a specifier or a type stands in front of
the name, as C++ reads it, and behind a bare name only when a body, an
initializer list, a qualifier, or a defaulted or deleted definition follows. A
template header counts as standing in front of the name: it is what a
constructor template, which has no return type, spells there, as in
`template <typename tf_CP0>` above `C(tf_CP0 &&_P0)`, and it is what tells a
deduction guide's arrow, written apart like a trailing return type's, from the
member access that `C(x)->y` is without one.
`cFoo<T> && cBar<T>` and `TCFoo<T> &&_Other` spell the same tokens, so a `&&`
behind a template argument list in front of a name keeps its spelling. The
line-break rule never splits at a declarator.

`line-break` lays every statement out in two phases. The statement is first
taken as if it were written on one line, with every gap at its inline spelling:
a gap the source already writes on one line keeps its width, and one holding a
line break is measured at the width joining it writes. A statement that fits at
its own indentation is written that way, whatever lines the source had. One
that does not is split, outermost break first, and each resulting line is split
further only while it is still too long:

1. The loosest binary operators at the line's own bracket level each start a
   line. The first operator stays on the line before a lambda it takes.
2. A scope standing behind another scope's closing marker, such as a lambda's
   parameter list behind its capture list, and a trailing return type behind a
   parameter list, each move down whole. A lambda's capture list, template
   parameter list and parameter list are one introducer: they stand together
   on a line or each takes one of its own.
3. The first scope on the line is opened: its opening and closing markers take
   lines of their own and every element stands on one, laid out the same way.
   What follows the closing marker resumes under it. A function's qualifiers
   and its pure specifier stay behind the closing parenthesis where they fit.
4. A name that is still too long with its parameter list opened opens its own
   template argument list, and after that breaks at its member accesses, all
   at once.

A declaration whose name does not fit in front of its parameter list first has
its return type moved behind that list, as `auto ... -> Type`, when the
converted signature fits on one line or the name would not fit otherwise and
the type is wider than the `auto` that replaces it. An explicit instantiation
is converted the same way. A bare name behind a complete type, such as an
attribute macro, keeps the declaration from being converted. The
conversion is the one rule that changes tokens. It is decided on the original
source, and the layout is then made on the converted source, so the lines the
plan writes are the lines a later pass sees.

The decisions are kept per gap between tokens and written out once, so no
decision depends on an edit already made, or on where the source happened to
break its lines. A construct that contains a block comment, a multiline token,
or a braced initializer written across lines keeps its lines; only its inner
constructs are brought back to one line where they fit. A line comment only
ends its line: a construct holding one never fits on a line and is split, and
the lines around the comment are laid out and indented as usual, the comment
staying at the end of its own.
A lambda body inside a call is different: a block never fits on a line, so the
call is written split, the scope holding the body is the one opened while what
stands in front of it stays on the line where it fits, the body opens under its
introducer at the element's indentation with its lines following it, what
comes after the call resumes under the closing parenthesis, and the terminator
takes a line of its own. A bare name behind a capture list, such as an
attribute macro, trails the list on its line. An arrow behind a call's
arguments is a member access, and only one behind a parameter list a trailing
return type. A braced initializer written across lines is excluded because it is data,
most of it written one element per line on purpose, including Malterlib's `_o=`
and `_j=` command-line DSL. A template header, a `requires` clause, a label, and
the statement a clause guards each keep their own line.

A line that is still too long after all of that is broken again, so the layout
does not depend on where the source happened to break: an element the source
wrote split is measured like any other, and the scopes inside it open in turn
until every line of the result fits. A line that nothing above can shorten,
such as one long literal, is reported by `line-length` and left alone. That
report is made on the result, so it names the lines that are still too long
once every other rule has run, against the source line each came from.

A block's braces and each of its statements take a line of their own. A body
opens where its head puts it, a declaration's at the statement's indentation
and a lambda's one level in, and takes its lines along so that their depth
still follows the brace, a comment on a line of its own among them; a body with
a multiline token inside, whose lines could not follow, stays where it is. That
holds whether the body shares a line with its head or already has one of its
own. Whose body it is the capture list in front of the brace says, not the
first parenthesis of the statement, which may open a call the lambda is handed
to; a subscript operator's name ends in brackets of its own and names a
declaration, no lambda.

Every statement of a block stands at the block's own level, whatever depth the
source gave the line it starts, and the closing brace stands at the level of
the head that opened it. What a clause, `else` or `do` guards stands one level
in from that clause, each clause of a chain counting for one, while the block
one guards stands at the clause's own level. A label stands one level out from
the statements written under it, which is where `case`, `default` and an access
specifier go. A statement whose own lines are fixed keeps the line it starts as
well, since moving that one alone would leave the rest of them behind: a braced
initializer written across lines, a block comment inside, a multiline token. So
does a statement inside a conditional, whose depth the sources decide for
themselves. A case written on its label's line stays there whole, as
`case 1: return 1;` is written on purpose, and so do the `if` of an `else if`
and an attribute on a clause's line. Behind a closing brace only a keyword
starts a statement of its own, since a name there declares a variable of the
type just defined. A lambda's terminator stands on a line of its own at the
statement's indentation, wherever the source left it, on that line already or
behind the brace; a declaration's stays behind its closing brace.

A brace holds statements, rather than the elements of a braced initializer,
when a statement terminator stands at its own level or an element of it starts
with a keyword only a statement begins with. A body whose every statement is
compound, such as a lambda that does nothing but loop, has neither a terminator
of its own nor an initializer's shape, and the keyword is what tells it apart.

## Conditional directives

The branches of `#if` … `#elif` … `#else` … `#endif` are alternatives, and the
engine reads no branch separately: it sees the branches one after another. That
is the same shape as any single branch as long as no construct is cut by a
branch boundary, which is what the engine requires before it lays out a
construct a conditional runs through. Where a construct spanning a boundary
does not span the whole group, every directive of the group is opaque and the
construct around it keeps the lines the source gave it:

```cpp
#if DDebug
	if (a)
#else
	if (b)
#endif
		g();
```

A branch that opens a bracket it does not close, or closes one it did not open,
holds a piece of a construct rather than a whole alternative. The file then has
no line structure to be laid out against, which `structure` reports, naming the
conditional rather than the construct it left open.

Everywhere else a directive is transparent: it only ends the line it stands on,
as a line comment does. The construct around it is written split, each stretch
between two directives is a line of its own at the level the construct gives
it, and a stretch is only broken further where it is still too long, so no
construct is opened up to make room that no line of it needs. An operator
standing at a directive is one the construct is written broken at, and then
every operator that binds as loosely takes a line of its own; where no
directive stands at an operator none is broken, since what a branch holds binds
tighter than the boundary around it.

```cpp
	co_return co_await m_Promises
#if DDebug
		.f_Insert()
#else
		.f_Insert()
#endif
		.f_Future()
	;
```

Directive lines themselves are never rewritten. The sources spell them both at
column one and indented by conditional depth, so the standard does not settle
them and they keep what they have, a block moving around them included.

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

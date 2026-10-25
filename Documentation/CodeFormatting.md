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
[*.{c,cc,cpp,cxx,h,hh,hpp,hxx,m,mm}]
malterlib_format = malterlib

[Vendor/zlib/*.{c,h}]
malterlib_format = malterlib-c

[*.{MHeader,MSettings,MTarget}]
malterlib_format = malterlib-buildsystem

[Vendor/**]
malterlib_format = off
```

`malterlib` selects the Malterlib profile for C, C++, Objective-C and
Objective-C++, `malterlib-c` the same profile reading C and Objective-C as C
(see [C](#c)), `malterlib-buildsystem` the profile for the build system's
syntax, `off` disables formatting, and the
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

The profile selects the language, and `CCodeFormattingSettings::f_GetLanguage`
gives it: C++ with Objective-C's additions for `malterlib`, C with them for
`malterlib-c`, the build system's syntax for `malterlib-buildsystem`. A file's
name plays no part; the `.editorconfig` that opts it in is authoritative, so its
sections name which files are written in which language.

## C

`malterlib-c` formats C by the same rules, without what C++ adds to it. A word
only C++ reserves is a name, so `struct CNode *new;`, `int class;` and
`delete(pNode)` read as the declarations and the call they are. A `<` is always
a comparison, since C has no templates, and a bracket subscripts or designates,
since it has no lambdas: `{[0] = 1}`. No return type moves behind a trailing
arrow, which C cannot spell. C++ reads all of these the other way, so a C
project's sources opt in with this profile, by directory or by extension,
rather than with `malterlib`. A raw string reads as it does in C++, as the GNU
C dialects GCC and Clang compile by default read it: `R"x(a"b)x"`.

## Naming

C++ leaves some spellings open that a project's naming settles: whether `(Name)`
is a cast or a grouping, whether a parenthesis inside a template argument list
is a call, and which macros stand where `inline` does. The engine reads that
naming from the project's style, `CCodeFormattingStyle`, which gives every
identifier its roles once, when the source is lexed. A request without one reads
C++ alone and refuses or keeps a layout where only the naming could decide.

`MTool Format` reads it from a YAML file named `.malterlib-format`, the nearest
one in the file's directory or above. Where there is none, it reads
`Malterlib/Core/.malterlib-format` from the nearest directory above that holds
one, and where there is none of those either, it formats without a naming.

```yaml
Version: 1
Extends: ../Core/.malterlib-format
FunctionNames:
  - 'f_.*'
TypeNames:
  - 'C[A-Z].*'
  - 'umint'
MacroNames:
  - 'D[A-Z].*'
SpecifierMacros:
  - 'inline_always'
DSLMarkers:
  - '_'
  - '_o'
  - '_j'
  - '_jo'
```

| Key | Meaning |
| --- | --- |
| `Version` | Required, and `1`. |
| `Extends` | Another document, relative to this one's directory, applied first. A list this one leaves out keeps what it extends. A cycle is an error. |
| `FunctionNames` | Names of functions. A name among them followed by a parenthesis is a call wherever it stands. |
| `TypeNames` | Names of types beyond C++'s own. A parenthesis holding only one is a cast. |
| `MacroNames` | Names of macros, which can expand to anything, so the layout around them is kept rather than read. These lists are the only macros the engine knows: a source's own `#define`s are not read, so a file formats the same wherever its macros are defined. |
| `SpecifierMacros` | Macros that stand in front of a declaration the way `inline` does, and are part of a run of specifiers. |
| `DSLMarkers` | User-defined literal suffixes of a DSL, which hug what they mark. |
| `NoReturnMacros` | Macros that never return, such as those that throw, so a case ending in one does not fall through to the next. |
| `UnformattedCalls` | Functions and macros whose parenthesis keeps its text as written, such as a macro turning its argument into a string: `BSON_STR({"a" : [1, 2]})`. Nothing inside it is edited. |
| `AlignTrailingComments` | `true` puts the comments behind a run of enumerators or members at one column. See [Layout options](#layout-options). |
| `AlignEnumValues` | `true` puts the `=` of a run of enumerators at one column. See [Layout options](#layout-options). |
| `AlignTrailingCommentsMaximumColumn` | The latest column, counting from 1, an aligned comment may start at; `0` is no limit. See [Layout options](#layout-options). |
| `TrailingScopeAtStatementLevel` | `true` gives a lambda's body or opened scope that ends its statement's only expression no continuation level. See [Layout options](#layout-options). |

A list written as a sequence replaces the one extended. Written as a mapping
holding `Append`, its sequence is added to the one extended instead:

```yaml
Version: 1
Extends: ../Core/.malterlib-format
TypeNames:
  Append:
    - 'CValue'
```

An entry is a name, or a pattern anchored at both ends in which `.` is any
character, `[A-Z0-9_]` a class, and `*` repeats the atom in front of it. A name
is found by binary search and a pattern is tried only for names whose first
character it can start with, so a naming costs no measurable time. A key the
format does not have, a malformed pattern, or a list holding anything but
strings is an error, since a misspelt key would otherwise leave the list it
meant to set as the extended document has it.

Malterlib's naming is in `Malterlib/Core/.malterlib-format`, and every other
module's document holds `Extends: ../Core/.malterlib-format`.

## Layout options

Besides the naming, `.malterlib-format` holds options that change the layout
itself. Each is off where no document sets it, and a document that leaves one
out keeps what it extends, so setting it in `Malterlib/Core/.malterlib-format`
turns it on for every module.

These options make a small edit reformat lines it does not touch. Adding an
enumerator or member with a name longer than the rest of its run realigns every
`=` and comment in the run, and a tail added behind a trailing scope, `+ 6` or
`> "Error"`, moves the whole scope and its body one level in. The commits that
make such edits change more lines than they mean to, and those lines conflict
with every other branch that touches them, so merging code needs more conflict
resolution than it does without the options. The sections below say what edits
each option reacts to.

### Aligned comments and values

`AlignTrailingComments: true` puts the comments behind a run of enumerators, or
of members, at one column: one space past the run's longest line, padded with
spaces (`comment-align`). A run is the lines with nothing but its entries
between them; an entry continued over several lines is one of them, and a
comment continued on the lines below its entry stands at the run's column.
`AlignEnumValues: true` does the same for the `=` of a run of enumerators,
one space past the run's longest name (`enum-value-align`), and its comments are
aligned behind the values. A run holding a single `=` or a single comment keeps
it one space behind its code.

```cpp
enum class EEvent : uint32
{
	mc_None          = 0
	, mc_Read        = 1 << 0 // Readable
	, mc_WriteClosed = 1 << 4 // Closed
	, mc_Error       = 1 << 5
};
```

An entry added, removed or renamed so that the run's longest line changes moves
the column, and with it every other entry's `=` or comment.

Alignment never gives way to the line length: a line aligning makes too long is
written anyway, and `line-length` reports it for the code or comment to be
shortened. `AlignTrailingCommentsMaximumColumn` keeps one long entry from
pushing a whole run's comments far to the right: an entry whose comment would
start past that column, counting from 1, keeps its comment one space behind its
code, and the rest of the run is aligned without it. `0` is no limit.

### Trailing scopes at the statement's level

With `TrailingScopeAtStatementLevel: true`, a lambda's body, or an opened
parenthesis or brace, that ends its statement's only expression has no
continuation level. It stands at the statement's level, as one the statement
opens with does, and the terminator ends its closing marker's line:

```cpp
auto Cleanup = g_OnScopeExit / [&]
{
	fg_Remove();
};

auto Value = fg_Function
(
	5
);
```

Where the expression goes on behind the marker, the scope is one part of
several and keeps its level, as it does without the option:

```cpp
auto Value = fg_Function
	(
		5
	)
	+ 6
;
```

So does a scope behind a head already broken in front of it, such as a lambda
behind an operator on a line of its own, and a declaration's parameter list,
which its return type is moved behind instead. An edit that adds such a tail or
breaks such a head, or takes it away again, moves the whole scope and its body
by a level. Turning the option off again gives back the layout without it.

## Requests and results

```cpp
NDevelop::CCodeFormattingRequest Request;
Request.m_Source = Bytes;
Request.m_Path = "Source/Example.cpp";
Request.m_Settings = NDevelop::CCodeFormattingSettings(Properties);
Request.m_pStyle = pStyle; // Optional; see Naming and Layout options.

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

The implemented rule matrix is whitespace-only but for eight conversions:
`trailing-return`, `braces`, `east-qualifier`, `specifier-order`,
`empty-statement`, `enum-comma`, `list-comma`, and `trailing-comment`. Every
plan is verified against `fg_HasEquivalentCodeTokens` on the converted source,
and the plan is re-analyzed until the result is stable, up to four passes whose
edits compose into the one plan; a token-stream change, or a file still changing
after the last pass, is a formatter failure, not an edit. A range is analyzed
again where the plan has moved it, and a further pass that would edit outside
the effective ranges leaves the plan as it stands.

| Rule | Behavior |
| --- | --- |
| `indentation` | Rewrites leading whitespace in the configured style, preserving the width of a line the layout does not place, the lines of a block comment behind its first included. |
| `directive-indentation` | A preprocessor directive starts its line with its `#`, indented behind it by a level for each conditional it stands in, apart from the code around it: `#elif`, `#else` and `#endif` stand where their `#if` does, and `#if 0` over `#\tif 0` over `#\t\tdefine A`. Indented with spaces the name stands where the tabs would put it, `#   if 0` with a tab width of 4. A conditional without `#elif` or `#else` that has no code in front of its `#if` or behind its `#endif`, such as `#ifdef __cplusplus` around a header, adds no depth. A directive's continuation lines move together, the least indented one level in from the directive and the others as far in from that as the source has them, and splices the source aligns at one column are aligned again behind the longest line. A file whose conditionals do not pair up keeps its directives. |
| `trailing-whitespace` | Removes spaces and tabs before a line terminator. |
| `final-newline` | Appends a terminator to an unterminated last line. Whole-file requests only. |
| `line-ending` | Converts terminators to `end_of_line`. Whole-file requests only. |
| `clause-space` | Exactly one space between `if`, `for`, `while`, `switch`, or `catch` and its `(` on the same line. |
| `comma-space` | No space before a comma, one space after it on the same line. |
| `comment-align` | With the project's `AlignTrailingComments`, the comments behind a run of enumerators or members stand one space past the run's longest commented line, padded with spaces so the column holds at any tab width. A run is the entries of one enum, struct, class or union body, or an Objective-C section's instance variables, on consecutive lines, each on one line; a blank line, a comment line and a member that is no field (a function, a type, an alias) end it, a field whose type a class key names, `struct CAddr m_Addr;`, being a field, while a comment continued on the lines below keeps its entry in the run and its continuation lines stand where it does, at the entry's indentation and spaces from there. A run with a single comment keeps it one space behind the code. A line the alignment makes longer than `max_line_length` is aligned all the same and reported by `line-length`. |
| `enum-value-align` | With the project's `AlignEnumValues`, the `=` of a run of enumerators stands one space past the run's longest name, padded with spaces, and the comments behind them are aligned behind that. A run with a single `=` keeps it one space behind the name. |
| `comment-space` | A line comment trailing code stands one space behind it, and so does a block comment on one line that trails code and ends its line. The comment lines directly below that are indented past the code line's own indentation continue it, and are aligned with it wherever it lands; a comment line at the code's indentation is a comment of its own. Comment lines of their own read as one text: the blanks every line of a run starts with behind its `//` are one space, `//\t\tA` over `//\t\t\tB` becoming `// A` over `// \tB`. A documenting run, `///` or `//!`, keeps its text, and so does one whose lines go on behind the shared blanks with a tab on one line and a space on another. A block comment whose text stands further in than one space behind its `/*` starts below the marker at the marker's column, its lines keeping where they stand relative to each other. |
| `string-continuation` | A string literal continued on the next line, adjacent literals being one string, stands one level in when the line it continues starts its statement, and at that line's level when the line is a list element or an operand the statement is split at. |
| `line-break` (DSL arrays) | A DSL array such as `_o[...]` written across lines is opened, as a braced initializer is, and one written on one line is opened only where it does not fit. An opened array's `[` stands on a line of its own at the element's level, or one level in when the line starts its statement, and its elements and `]` stand with it. In an opened array, and an object whose keys are the DSL's, `"Key"_o= ...`, the closing marker takes a line of its own and no blank line stands between elements. A list holding a directive keeps its lines, the array's lines moving with its bracket. |
| `operator-space` | One space around unambiguous binary operators. |
| `angle-space` | No space between the closing markers of two nested template argument lists: `>>`, never `> >`. |
| `token-space` | Every other pair of tokens on one line takes the spelling the standard settles, where it settles one: member access and scope markers hug, a keyword stands apart from its parenthesis, a label's colon, a bit-field's width, and a unary sign hug, a trailing return type's arrow stands apart, an operator between two operands stands apart from both. A run of pointers is one declarator, `CFoo **`, while a reference to a pointer stands apart from it: `CFoo * &`. A declarator stands apart from a calling convention macro between it and the name, `void * DMibCrossmoduleAPI fs_Alloc`. A postfix `++` or `--` ends its operand, so `*pParse++ - '0'` subtracts. |
| `block-blank-line` | Removes blank lines directly after an opening brace and directly in front of a closing one. |
| `case-blank-line` | Removes blank lines directly after a `case` or `default` label. |
| `blank-line` | Collapses a run of blank lines into one, and takes out one that cuts a construct continued on the next line: behind an opening bracket or an operator, such as the `=` in front of an initializer's brace, and in front of a closing bracket. Blank lines between a list's elements, between statements and around a block's braces stay. |
| `function-blank-line` | One blank line after a function's body, unless the scope the function stands in ends there. |
| `type-blank-line` | One blank line after a struct, class, union or enum definition, unless the scope it stands in ends there. A variable of a C struct type with a braced initializer, `struct timespec Time = {...};`, defines no type. |
| `access-blank-line` | One blank line in front of `public:`, `private:`, or `protected:`, and none after it. The first specifier in a class stands directly under the opening brace, and two in a row stand together: `private:` directly above `public:` follows it the way a member does. |
| `line-break` | Brings a split construct back to one line when it fits and nothing forbids it, splits one too long for its line outermost break first (see [Line structure](#line-structure)), and gives a block's braces and statements lines of their own. |
| `layout-directive` | A list a `columns`, `table` or `rows` directive leads stands in rows, and the lines an `align: =` directive leads have their `=` at one column (see Protected regions). Reports the directive where its list keeps its lines. |
| `structure` | Diagnostic only; the file's brackets do not nest as written, so no line of it can be placed and all of them are kept. |
| `line-length` | Diagnostic only, and measured on the formatted result: the lines the other rules break up are no violation, and one they leave too long is named where it stands in the source.  In the build system's files, a string ending a line, a template string included, counts for no width, since its value is what makes the line long and no break shortens it; what stands in front of it counts, and so does a string with more behind it. |
| `east-qualifier` | A `const` or `volatile` written in front of its type moves behind it: `const int &_Value` becomes `int const &_Value`. |
| `specifier-order` | `static` stands in front of `constexpr`: `constexpr static umint` becomes `static constexpr umint`. |
| `empty-statement` | A statement terminator that ends nothing is taken out: `f_Call();;` becomes `f_Call();`. Only an empty statement standing behind another terminator goes, with nothing but spaces, line breaks and comments between them. The comments stay where they are, and a block comment in front of it on its line keeps it, since the comment would come to end the line. |
| `enum-comma` | An enumerator's comma stands in front of it, and the last one has none. A comma that a comment or a conditional's directive separates from the enumerator behind it moves in front of that enumerator, into every branch it is compiled in, and so does one ending its line behind a block comment that describes the enumerator in front of it, `A = 1 /* One */,` over `B`; the one behind the last enumerator is taken out; the layout moves the rest. An enum holding another directive, or an enumerator that is the first in some configurations only, keeps its commas. |
| `list-comma` | A comment behind a comma in a bracketed list describes the element in front of it, so the comma moves in front of the element behind it, as the layout writes it: `a, // A` over `b` becomes `a // A` over `, b`. A comma ending a braced list written across lines is taken out. In a braced list holding a conditional's directive, the commas move as `enum-comma` moves an enumerator's, the list keeping its commas where an element is the first in some configurations only. Only a line comment on the comma's line, with the next element on a later line and nothing but blanks and comments between, moves it, and so do comment lines between a comma ending its line and the next element: `{1, 2},` over `// B` over `{3, 4}` becomes `{1, 2}` over `// B` over `, {3, 4}`. |
| `trailing-comment` | A block comment ending its line behind code is written as a line comment: `f_Call(); /* Text */` becomes `f_Call(); // Text`, and one over several lines becomes one line comment per line of its text, its documenting marker kept (`/**<` as `///<`) and the continuation lines aligned under the first. A comment the line would be longer than `max_line_length` with, behind code that fits by itself, moves to a line of its own above that line, at its indentation, as a line comment, unless the statement it stands in goes on behind it, as one behind an operand of an expression written across lines does, which stays behind the part it describes; a braced list's rows are no such statement; the line is measured as the spacing rules write it, the comment one space behind the code, so a column the alignment options put it at never moves it. A documenting comment keeps its marker, `/*!< Text */` written as `//!< Text`, and never moves, since it describes what stands in front of it. A comment standing for the name a declarator hugs, `int */*_Size*/`, stays a block comment where the declaration is split, as one behind an operand of a list does. A `malterlib-format` marker, a clang-tidy `NOLINT` comment, a comment behind a directive, and a block comment whose text ends in a line splice stay as they are. |
| `trailing-return` | A function declaration or definition that does not fit on its line has its return type moved behind its parameter list, `auto fg_Function(...) -> CFoo`, where that lets its name fit, and is then split as any other. |
| `braces` | Around a single statement guarded by `if`, `else`, `for`, or `while`: none when it is laid out as one line, braces when it spans lines or follows a split clause. |

`operator-space` covers `==`, `!=`, `<=`, `>=`, `<=>`, `||`, and the compound
assignments. Plain `=` is excluded here; `token-space` spells it, below. `&`,
`&&`, `*`, `.`, and `->` are excluded because they are also declarators or
member access; `token-space` spells those of them that are operators, below. A
symbol immediately following the `operator` keyword is part of a name rather
than an operation, and is spelled by `token-space` as well.

`east-qualifier` moves a qualifier that leads its type behind it, a run of them
as one: `const volatile int` becomes `int const volatile`. Whether a qualifier
leads is told by what stands in front of it. A separator, an opening marker, an
arrow, or a specifier has no type for it to follow, so the type is what comes
next: parameters, template arguments, `static const`, a trailing return type,
a conversion operator's type. Behind a name, a template argument list, or a
parenthesis the qualifier can as well belong to what it follows, as in
`CFoo const m_Value` and `f_Get() const override`, and is only moved where a
type and then a declarator or a name follow it, which neither reading has:
`mark_nodebug const CFoo &f_Get()`.

The type is followed to its last token: a name, qualified, with a leading `::`
and template arguments where it has them, a fundamental type's several words,
`auto`, `decltype` and its operand, behind `typename` or the keyword of an
elaborated name. Specifiers written behind the qualifier stay in front of the
type, so `const static int` becomes `static int const`. What follows the type
is no part of it: the class of a member pointer, `int const CFoo::*`, and of a
member defined outside its class, `NStr::CStr const CFoo::ms_Name`.

It refuses rather than guesses. A comment or a line break inside the span, a
`<` the structure did not resolve as a template argument list, a macro's bare
`const` argument, and a disabled or unselected region all leave the qualifier
where it is. So does a move the next pass would repeat: with a macro between
the type and its declarator, `const CFoo DFar *`, the moved qualifier would
lead `DFar` in turn. The conversion is made in a stage of its own, before the
other two, since a return type that moves takes its qualifier along; the
source it leaves is then analyzed like any other, and the edits of both stages
are composed into one plan on the original. Beyond the checks every plan gets,
the stage proves that the source reads the same with its qualifiers left out
before and after, which is all that moving one may change.

`specifier-order` writes a declaration's specifiers in one order where the
sources have two, `static` in front of `constexpr`, which is the order two out
of three declarations in the tree already have. Only what a run of specifiers is
made of, C++'s specifiers and the naming's `SpecifierMacros`, may stand between
the two, `constexpr inline_always static` becoming
`static constexpr inline_always`, with nothing but spaces and line breaks around
it; a comment in the run, and a disabled or unselected region, leave it as it
is. It is made in the same stage as `east-qualifier`, whose proof it shares: the
source reads the same with the moved words left out, and has as many of each.
The other specifiers have no order the sources agree on, `inline_always static`
and `static constexpr` and `constexpr inline_always` each being the common
spelling of its pair, so they stay where they are written.

`function-blank-line` sets a function's body off from what follows it, a comment
behind its closing brace staying on that line. Nothing is added in front of the
closing brace of the scope the function stands in, in front of a conditional's
directive, which has lines of its own, or a pragma, which belongs to the code it
brackets, or in front of a macro invoked directly
under the body, which belongs to the function as the one that implements the
streaming of a type does. Only a function's body counts: a lambda's ends in the
statement's terminator, and the requirements of a requires expression have the
body below them. `type-blank-line` follows the same rules, so a `#define` behind
a type is set off from it as it is from a function.

Each enumerator of an `enum` body takes a line of its own with the comma in
front of it, `EA` over `, EB`; a comma written at the end of a line moves to the
front of the enumerator behind it, and a blank line between the two stays in
front of the comma. A comma a comment or a conditional's directive stands behind
is moved by `enum-comma`, which leaves nothing but blanks between a comma and
its enumerator. An enum that conversion keeps its commas in breaks behind a
comma rather than in front of it. An enumerator too long for its line is laid
out as a list's element is, at the body's level: its value broken at its
operators starts with the `=`, `, EFileAttrib_All` over
`= EFileAttrib_UserExecute` over `| EFileAttrib_UserRead`.

`access-blank-line` leaves a specifier alone where a comment or a directive
stands between it and what is in front of it, since the blank line then belongs
in front of that comment, if anywhere. A specifier still written on the line of
a member gets its blank line from the layout, together with the line of its
own. The blank-line rules never overlap: where none may stand, behind a brace
or a label, that rule takes the whole run, and `blank-line` only ever takes
what is left of one behind its first.

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
statement's last line. A guarded clause, `if (a)` under a `for`, owns only its
condition, so the braces go around it together with what it guards in turn and
the `else` branches behind that, ending at their `;` or `}`. A nested `if` is
two statements to the structure builder, so a block that shields a dangling
`else` keeps its braces, and so do
the bodies of `do`, `switch`, `try`, and `catch`. The decision reaches every
guarded statement, a lambda's body included, whose block belongs to the group
the lambda is written in rather than to the statement around it.

`token-space` applies `fg_GetCanonicalSpacing`, the same answers the line-break
rule joins with, to every pair of adjacent tokens on one line, and leaves a pair
alone where that function does not settle the spelling, such as a `/` written
tight between two names, which is a path in a macro argument.

An operator function is named by the `operator` keyword and what follows it: a
symbol, the call operator's own parentheses, the subscript operator's brackets,
a literal's suffix, or the type a conversion yields, qualified and with template
arguments where it has them. That name stands apart from the keyword and from
the parameter list behind it, so the forms are `operator = (`, `operator () (`,
`operator [] (`, `operator ""_f (`, and `operator NStr::CStr ()`. A name is not
a call, so the parenthesis of an ordinary one stays where it is. The one
parenthesis behind a name that keeps its spelling is a pointer to function's
declarator, `void (*pCall)(int)`, which is told by the parameter list behind it
and by holding no separator; a call whose first argument takes an address,
`m_Actor(&CActor::f_Get, _Value)`, is a call like any other, and being settled
is what lets the statement around it be laid out. What such a name stands in
front of is a parameter list wherever the function is declared, which is also
what tells the arrow behind it, a trailing return type's, from the member access
that `->` otherwise is: neither `operator co_await`, named by a keyword an
expression uses, nor `operator ""_f`, named by a suffix behind a literal, spells
a type in front of the list the way an ordinary declaration does.
`noexcept(false)` counts as a qualifier like `noexcept`, and a body behind the
arrow's type makes the arrow a trailing return type whatever the parenthesis is
read as, `DMibTestSuite("Name") -> TCFuture<void>` above its braces. A lambda
that takes nothing may leave its parameter list out, and its arrow then stands
behind the capture list, its qualifiers, or the attribute macro that follows the
list, `[pState] mutable -> TCFuture<void>`; such a lambda owns its body like any
other. A macro that opens a function, named `D` and a capital, is followed by
its return type and body the same way,
`DMibTestSuite("Name") -> TCFuture<void>`, where a member access behind a
macro's result never reaches a body through a type alone, `DEPTR(p)->m_Value`.
The brackets of `delete []` capture nothing, so `delete [] pArray->m_pData`
keeps its member access.

A conditional's `?` and `:` stand apart from both of their operands, a
parenthesised one included, `bFlag ? (a + b) : (c + d)`; the colon is the
conditional's when a `?` stands in front of it at the same level.

An operator with an operand on both sides of it is written apart from both,
whatever they are spelled with: `5 * 5`, `nFlags & mc_Mask`, `a + (b | c)`.
Behind a cast a sign is unary, `(aint)-16324`: a parenthesis that ends in a
declarator or a qualifier is a cast, and so is one holding a single name
Malterlib spells as a type's, a type prefix or one of the fundamental aliases,
where `sizeof(void *)` behind a name is an operand like any call. Without an
operand in front, the same token is the unary form, `-1` and `*pValue`; without
one behind, it belongs to something else, a cast's `(CFoo *)` or a pack's
`&& ...`. `*`, `&`, and `&&` stay ambiguous even between two operands, since a
name in front of one can be a type as easily as a value: `C(CStr &_A)` and
`C(a & b)` spell the same tokens. Those three are settled only where a
declaration cannot stand: behind a literal or the closing marker of a call or a
subscript, behind the `=` that ends the declarator part of the statement or
parameter they stand in, or inside an `if`, `while`, or `switch` condition,
which declares nothing without an `=` of its own. A parenthesis that could close
a cast is not such a marker, so `(int)*pValue` keeps its spelling, while
`sizeof`, `alignof`, `typeid`, and `noexcept` yield a value the way a call does
and `decltype` names a type. Everywhere else the pair keeps what the source has.
A lambda's body closes an operand the way a closing parenthesis does, so a sign
behind it is binary: `[] { ... } + g_Other`. A statement's own block closes
none, and a sign behind one is unary. Behind a declarator stands the name it
declares, never a literal or an operator spelled as a keyword, so
`nMove * sizeof(t_CKey)` and `i * 8` multiply wherever they stand. A function's
own qualification is part of its name, so `NMemory::fg_MemMove(...)` is a call,
where `void NMib::fg_Foo(CFoo *_pA)` declares one. A parenthesis holding nothing
but fundamental type words, where no call or `sizeof` could have produced it, is
a cast and no operand, so a sign behind it is unary, `(smint)-1`, where
`sizeof(int) - 1` and `(Value) - 1` subtract.

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
`_o=` and `_j=` DSL. A marker hugs what it marks: the `=` that makes a key of
the literal in front of it, `"Names"_o= 5`, and the brackets of an array,
`_o["--file"]`. A marker with no key in front of it spells an object, and its
`=` hugs the brace as well, `_o={"Key"_o= 5}`. The sources write these one way,
so a space in any of them is taken out. A DSL
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
name behind the ellipsis is what tells the two apart. Behind a declarator the
ellipsis stands apart the same way wherever it stands: `tfp_CParams &&
...p_Params`, `tfp_CParams && ...` where the pack has no name, and
`tp_CParams && ...>` where it expands. `sizeof...` and a fold's ellipsis are
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

A string an element of a list continues on the line below keeps that line, as a
line comment ends the one it stands on, so the list around it opens and a value
holding it gives at each of its loose operators. A continued string that stands
directly in a statement keeps the statement's lines; where the value has loose
operators, each in front of its first string or behind its last starts a line,
`Header` over `+ "a"` over `"b"_f` over `<< Value`, while those between its
strings stay where the strings put them. Its terminator takes a line of its own,
as a split statement's does. The operand sections of an
asm statement are split points of its parentheses, so a split one puts each `:`
and `,` in front of the section or operand it starts.

A `<` opens a template argument list only when it is written tight against the
name before it, or across a line break; Malterlib spells a comparison with
spaces, so the loose spelling stays an operator. Behind a name the naming lists
as a type's or a function's, which no comparison has for an operand, it opens a
list however it is spaced, so `TCVector< CStr >` and `TCVector < CStr >` both
become `TCVector<CStr>`. A `>>` that ends a template argument list is read the
way C++ reads it, as two `>` tokens, so that each list has a closing marker of
its own, and two closers written apart are joined back into one `>>` unless the
layout gives the second a line of its own. A brace holding a statement
terminator at its own level is a block, which is how a lambda body inside an
argument list is told apart from a braced initializer. Behind the call
operator's name, `operator ()`, a `<` can open nothing else, so it opens a list
however it is spaced: `fCheck.template operator ()<void>("Void")`.

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
header's, a lambda's, a catch clause's, a function's, or a function type's:
the parenthesis behind a pointer to function's declarator, `void (*pCall)(int
_A)` and `void (DMibCrossmoduleAPI *m_fFree)(void *_pMemory)`, with a calling
convention macro allowed in front of the declarator; and
directly inside a template argument list a parenthesis behind a type, which
ends in a template argument list of its own or in a name, and is written apart
from the parenthesis, as in `TCFunction<void (CFoo &&_Value)>`. A template
argument is as often a value the same tokens yield, by a call or by a
construction, and Malterlib's naming is what tells the call: a name carrying
one of the function prefixes is a function's, so
`TCFoo<::NMib::fg_GetHash<t_pMember>(t_Hash)>` passes what the call yields and
hugs its parentheses however it was written. A type's name says nothing there,
since `TCFoo<CBindActorOptions(_Type, _Call)>` constructs where
`TCFunction<CFoo (int _A)>` declares, so those keep the spelling the source
gave them. `decltype` and its
operand name a type wherever one can stand, `decltype(auto) fg_Get(tf_C &&_A)`.
A function's
parenthesis is its parameter list when a specifier or a type stands in front of
the name, as C++ reads it, and behind a bare name only when a body, an
initializer list, a qualifier, or a defaulted or deleted definition follows. A
template header counts as standing in front of the name: it is what a
constructor template, which has no return type, spells there, as in
`template <typename tf_CP0>` above `C(tf_CP0 &&_P0)`, and it is what tells a
deduction guide's arrow, written apart like a trailing return type's, from the
member access that `C(x)->y` is. A guide without a header is told by naming its
template on both sides of the arrow, `TCFoo(CVoidTag) -> TCFoo<void>`.
`cFoo<T> && cBar<T>` and `TCFoo<T> &&_Other` spell the same tokens, so a `&&`
behind a template argument list in front of a name keeps its spelling. The
line-break rule never splits at a declarator. A
parenthesis in front of a `&` is only a parameter list where it closes one: one
whose last word is a declarator or a qualifier spells a type, so it is a cast,
and what it converts hugs it, `(ch8 const *)&Value`. The
parenthesis of `if constexpr (...)` holds a condition like any other `if`, never
parameters. A `new` hugs its placement arguments, which stand apart from the
type behind them, `new(_pMemory) CFoo(1)`, and belong to the head: the layout
opens the constructor's arguments, not them.
A `const` or `volatile` behind a `*` qualifies the pointer and stands apart
from it, as it does from a type: `CFoo * const pFoo`, `ch8 const * const *`. A
`&` or `&&` behind a `*` refers to the pointer and stands apart from it the same
way, `ch8 const * &o_pParse`, while a pointer to a pointer is written tight,
`ch8 **ppArgv`.

`line-break` lays every statement out in two phases. The statement is first
taken as if it were written on one line, with every gap at its inline spelling:
each gap is measured at the width the spacing rules leave it, so one the source
writes wider or narrower than the standard counts as the standard spells it, and
only a gap the standard does not settle keeps its own width. A gap holding a
line break the standard does not settle cannot be measured at all, and a
statement holding one is left as it stands. A gap the layout breaks belongs to
the layout: the clause, comma and operator rules leave it alone, since a space
and a line break written into one gap would land on the same offset. A
statement that fits at
its own indentation is written that way, whatever lines the source had. One
that does not is split, outermost break first, and each resulting line is split
further only while it is still too long:

1. The loosest binary operators at the line's own bracket level each start a
   line. An operand still too long for its line gives at the tighter operators
   it holds before any of its scopes is opened, and those line up with the
   looser ones: `a(…)`, `+ b(…)` and `> Actor / [&]` each take a line. An
   operand holding a lambda is laid out by its scopes instead. The first
   operator stays on the line before a lambda it takes, and so does the lambda's
   capture list where it fits there: `g_ActorFunctor / [this]` with the
   parameter list and the return type on the lines below, the parameter list
   opened where it does not fit. A capture list too long for that line takes the
   operator with it onto a line of its own, `/ [...]` leading it where the
   lambda's body opens: at the element's level in a list, at the continuation
   level in a statement, whose `=` stays on the head. Only a capture list too
   long for that line too goes below the operator, opened. The same holds for an
   operator taking a lambda inside an operand: where the capture list does not
   fit behind it, the operand gives there before any of its scopes opens,
   `> pState->m_Promise % fg_Format(...)` over `/ [...]`. A conditional's `?`
   and `:` bind loosest of all, so a conditional gives at them before either of
   its operands is opened: the condition, `? a` and `: b` each take a line. A
   conditional groups to the right, so one in a branch, `: c ? d : e`, is a
   single operand of the outer one: whole where it fits, and where it does not,
   its `? d` and `: e` stand a level deeper than the `:` in front of it. A
   parameter's default broken at its operators is given the way a statement's
   value is, its `=` starting the first operand's line at the parameter's level.
   A declaration's name that does not fit in front of its parameter list gives
   in front of the `::` behind its class's template arguments, which stay whole:
   `TCFoo<…>` over `::f_Name()`. An operator's declaration gives there only
   where an exception specification or a trailing return type behind its
   parameter list can then give first, `TCFloat<…>` over
   `::operator + (…) const` over `-> TCFloat`; without one its parameter list
   opens behind the whole name. A range holding a gap the standard does not
   settle has no single-line form to measure, and is left as it stands rather
   than broken at its operators; an operator behind a template argument list is
   settled, so a conditional behind one is laid out wherever it was broken.
2. A scope standing behind another scope's closing marker, such as a lambda's
   parameter list behind its capture list, and a trailing return type behind a
   parameter list, each move down whole. A lambda's capture list, template
   parameter list and parameter list are one introducer: they stand together
   on a line or each takes one of its own. A parameter list moves below its
   capture list to stand with what follows it, so where that line would give
   at its exception specification or return type anyway, the list stays behind
   the capture list: `[&](auto &&_Key)` over `noexcept(...)` over `-> void`.
   A call on what another call yields
   spells one call expression with it, `f_CallActor(&C::f_Fn)(_Params)`, and
   moves down only where the line has nothing later to give: where a scope
   holding a lambda body stands behind it, that body opens below whatever else
   is done, and the whole call expression stays on the line in front of it.
3. The first scope on the line is opened: its opening and closing markers take
   lines of their own and every element stands on one, laid out the same way.
   What follows the closing marker resumes under it, and so do a function's
   qualifiers, `override`, `final` and its pure specifier behind an opened
   parameter list: `)` alone, then `const noexcept` on the line below it, and a
   trailing return type below that.
   Where a member access follows that closing marker, opening the scope would
   put the access on a line of its own anyway, and that line was all the
   statement needed: the scope stays whole and the chain behind it takes the
   lines below, as in `m_Subscription(&CFoo::f_GetActor)` with
   `.f_Timeout(30.0)` and `.f_CallSync(m_pRunLoop)` under it. A scope holding a
   lambda body is not one of those, since its body takes lines whether the
   scope is opened or not.
   The name a declaration's type declares stands behind the type's template
   argument list the same way, so the type stays whole and the name, with any
   declarator, takes the line below it: `TCMap<CStr, CValue>` with `m_Values`
   under it.
4. A name that is still too long with its parameter list opened breaks at its
   qualification, then opens its own template argument list, and after that
   breaks at its member accesses, all at once.

A declaration whose name does not fit in front of its parameter list first has
its return type moved behind that list, as `auto ... -> Type`, when the
converted signature fits on one line or the name would not fit otherwise and the
type is wider than the `auto` that replaces it. The specifiers in front of the
return type stay in front of `auto`, and are measured with it:
`inline_small auto f_Get(...)`. An explicit instantiation is converted the same
way. A typedef and a pointer to function's declaration never are, having no
return type to move, and the parameter list behind a pointer to function's
declarator opens as a function's does: `NTSTATUS (NTAPI *m_fQuery)` over `(`
over `IN HANDLE _Thread`. A statement in a function's body never is converted
either: a type in front of a name and a parenthesis defines a variable there,
`TCUniquePointer<CFoo> pFoo(fg_Construct())`, and no function is declared inside
another. A variable constructed with arguments elsewhere has the same shape, and
is told by what its parenthesis holds: an element that opens with a function's
name, a literal, a keyword that is a value, a unary operator or a brace is an
argument, which no parameter list holds, so
`TCSet<int> Set(fg_Construct(&Allocator))` is never converted. A bare name
behind a complete type, `void *` included, such as an attribute macro, keeps the
declaration from being converted, and so does a macro in front of the name,
`DMibFloatConstexpr`, which can expand to specifiers as readily as to a type.
Malterlib's attribute macros that stand in front of a return type, such as
`mark_artificial` and `only_parameters_aliased`, are specifiers like
`inline_always`. An operator function is named by the keyword and its symbol,
`operator ->*`, and has its return type moved like any other name. A requires
clause on a line of its own behind a declaration keeps that line, and the
declaration in front of it is laid out as one ending there. The parenthesis of
`operator new` is its parameter list, never placement arguments, and the braces
of `extern "C"` hold declarations like a namespace's. A parameter's default
argument is given to it as a statement's value is: the `=` and the value take
the line below, and the type stays whole with its name. A lambda standing in a
construct whose own lines are kept, such as a braced initializer written across
lines, still has a head of its own, split where it does not fit on its line, and
its body opens as it does anywhere else.

A lambda body splits the expression it ends. Each operator binding loosest in
front of the body starts a line of its own at the body's level, and so does a
binary operator behind the body's closing brace: `pState->m_Launch(...)` over
`+ g_Dispatch(Actor) / [=]`, the body, and `+ fg_Other()` under its brace. The
operator that takes the lambda stays on the line, `g_Dispatch(Actor) / [=]`.
Every body in an expression opens, a later one standing under what goes on
behind the first: `}` over `() && []` over `{`. A requires expression's body
opens the same way wherever it stands, in a requires clause under the clause's
line, what the clause goes on with starting the line below its closing brace.
The conversion is the one rule that changes tokens. It is decided on the
original source, and the layout is then made on the converted source, so the
lines the plan writes are the lines a later pass sees.

The decisions are kept per gap between tokens and written out once, so no
decision depends on an edit already made, or on where the source happened to
break its lines. A construct that contains a block comment over several lines, a
multiline token, or a braced initializer written across lines that holds a
conditional's directive or that a layout directive or a disabled region owns
keeps its lines; only its inner constructs are brought back to one line where
they fit. A block comment on one line that the code behind it follows on that
line names that code, `/*bFlag=*/true`, and keeps no lines: it moves with the
code, joined onto the line in front where the construct fits and leading the
code's line where the construct is split. One in front of a closer, a separator
or a terminator describes what stands in front of it, and one inside a lambda's
body leaves the construct around it alone, since the body is laid out on lines
of its own. One on a line of its own ends its line as a line comment does.

A braced list, or a DSL's array, whose opening marker ends its line is written
open: it never fits on a line, as a block does not, and it is laid out as a list
too long for its line is, wherever the source put its closing marker and its
elements. Each element takes a line of its own with its comma in front, the
first one level in from the opening marker and the closing marker under it, and
is laid out as an element: a list written on one line inside it is joined where
it fits, and one written open is opened in turn. A blank line between two
elements of a braced list sets them apart and stays, on whichever side of the
comma the source wrote it; among a DSL's data it goes. A list written open with
nothing in it stays open, `{` over `}`. A list that ends what it stands in,
written open or too long for the line behind what stands in front of it, opens
below that, which stays on its line where it fits: `CFoo Value =`,
`_Builder << CFoo` or `, "Key"_o=` over `{`, the braces one level in from a
statement and at an element's level, and the terminator of a statement on a line
of its own, `CFoo Value =` over `{` over `}` over `;`. Behind a separator the
comma stands alone on its line above the list, `,` over `{`. Data meant to stand
in rows, such as a table of bytes, says so with a layout directive (see
Protected regions).

A braced list that keeps its lines is laid out as an opened list as far as it
can be. Where its braces stand on lines of their own, the statement it ends is a
split one: the braces stand one level in from the statement and its terminator
on a line of its own. An opening brace written at the end of a line takes a line
of its own there, and the list's lines move with it. A parenthesis ending the
statement, which keeps its lines for a list inside it, stands the same way, and
every line inside such a list or parenthesis moves with it. Each element
starting a line stands one level in from the opening brace, the first among
them, and the closing brace under the opening one; an element holding a
multiline token stays where it is. Each element written on one line takes a line
of its own with its comma in front, a comma written behind the element in front
of it included, and is laid out as an element. A comma a conditional's directive
separates from the element behind it is moved by `list-comma` as `enum-comma`
moves an enumerator's; a list that conversion keeps its commas in keeps all of
them behind its elements. So does an element only a continued string takes
across lines, its continuation at the element's level.

A line comment only ends its line: a construct holding one never fits on a line
and is split, and the lines around the comment are laid out and indented as
usual, the comment staying at the end of its own. Comment lines in front of a
list's or a call's closing marker stand among its elements, one level in from
the marker. A lambda body inside a call is different: a block never fits on a
line, so the call is written split, the scope holding the body is the one opened
while what stands in front of it stays on the line where it fits, the body opens
under its introducer at the element's indentation with its lines following it,
what comes after the call resumes under the closing parenthesis, and the
terminator takes a line of its own. A bare name behind a capture list, such as
an attribute macro, trails the list on its line. An arrow behind a call's
arguments is a member access, and only one behind a parameter list a trailing
return type. A template header, a `requires` clause, a label, and the statement
a clause guards each keep their own line. A template header too long for its
line opens its parameter list, with `template` alone on its line and the markers
at the header's own level, since nothing extends the header behind its `>`. A
class's own template argument list, behind the name its definition or
specialization declares, opens at the head's level the same way, `struct TCFoo`
over `<` and `>`, while a base clause behind it continues the head one level in,
`: public CBase`. A requires clause behind a header keeps the lines it was
written on as well, and so does a member template's header behind the clause;
the declaration behind them starts its own line and is laid out as usual. Only a
requires clause elsewhere in a declaration keeps the whole statement's lines.

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
source gave the line it starts, and the closing brace stands at the level of the
head that opened it. What a clause, `else` or `do` guards stands one level in
from that clause, each clause of a chain counting for one, while the block one
guards stands at the clause's own level. A macro invoked where a statement
starts, with a block behind its parenthesis and an `else` behind that block, is
the `if` it expands to, since only an `if` takes an `else`:
`BOOST_IF_CONSTEXPR(c)` over its block guards it as `if (c)` does. In a
function's body, so does a call of a name ending its line with a statement
written further in on the next, as a loop macro's is: `list_for_each(p, Head)`
over `f(p);`. A call alone on its line with the next statement starting at its
own column, or a label behind it, is a statement of its own without a `;`:
`IOTRACE((...))` over `rc = f();`. A label stands one level out from the
statements written under it, which is where `case`, `default` and an access
specifier go. A statement whose own lines are fixed keeps the line it starts as
well, since moving that one alone would leave the rest of them behind: a block
comment inside, a multiline token. One ending in a braced initializer written
across lines moves, and the initializer's lines with it. A statement inside a
conditional stands at its scope's depth like any other, since directives are
indented apart from the code. A case whose body is one statement, a `break`
behind it included, stands on its label's line where the whole fits there,
`case 1: a = 1; break;`; a block, a clause, more statements, a comment or a
blank line between them keep the body under the label, a block opened like any
other, and so does a label that other labels stand directly over, since the body
is theirs as well. The `if` of an `else if` and an attribute on a clause's line
stay where they are written. The `if` is laid out against the line the `else`
starts, so a condition that does not fit behind both opens under the `else`, as
it would under an `if` of its own. A statement that opens with such an
attribute, `if (x) [[unlikely]]` with the block below, is laid out at the
clause's indentation, where its block opens. Behind a closing brace only a
keyword starts a statement of its own, since a name there declares a variable of
the type just defined. A lambda's terminator stands on a line of its own at the
statement's indentation, wherever the source left it, on that line already or
behind the brace; a declaration's stays behind its closing brace. A value broken
at its operators starts a line of its own with the `=`, so that every operand
stands under the one in front of it:

```cpp
	umint nExpectedActors
		= m_ConcurrentActors[EPriority_Normal].f_GetLen()
		+ m_ConcurrentActors[EPriority_Low].f_GetLen()
		+ 1
	;
```

A value that fits on a line of its own behind its `=` takes that line whole
instead, and is broken at its operators only where it does not: a value that
takes a lambda keeps the `=` on the head, and one a comment or a directive cuts
gives at each operand, as its other stretches do.

That holds where the first operand is a plain line. One that has to be opened,
`auto Value = fg_Function` with its parenthesis below, or that takes a lambda's
body, `auto Cleanup = g_OnScopeExit / [&]`, keeps the `=` on the head, and so
does a DSL key, which hugs its `=`. A compound assignment gives its left hand
side a value the same way and is laid out as an `=` is: `_Output` over
`+= CStr::CFormat(...)` over `<< ...`, and `*_pCommandLine` over `%= "..."_f`.

What stands in front of a statement's `=` is the last thing to give. The value
behind it is opened first, a call's arguments and then its name's template
arguments, so a declaration keeps its type whole on the head line. A value with
nothing in it to open takes a line of its own behind the `=` where that makes
the statement fit. Where the type and the name do not fit on one line to begin
with, the name goes down with its value and the type stays whole on the line
above them. Only then is the type in front of the name opened. Everywhere
else the `=` is never a place to break: a statement the source broke there is
brought back onto its head.

A call in front of the `=` is a declaration's parameter list only where `0`,
`default` or `delete` follows the `=`; any other value is assigned to what the
call yields. Assigned to a member call, `Object.f_CreateMember(Name)`, the value
goes behind its `=` on a line of its own, the call in front of it closed.

What an expression goes on with behind a lambda's body, an operator, a member
access, or the arguments the lambda is called with at once, resumes under the
body's closing brace, is broken there at its operators where it does not fit,
and gives the statement's terminator a line of its own. A statement that opens
with its lambda has no expression for the body to stand under and no
continuation level, like one that opens with a parenthesis: its body stands
where the statement does, and what it is called with under the brace, written as
the source has it, `}();` or `(CParams());` on a line of its own. With no
continuation level the terminator would stand at the level of the line it ends,
so it ends that line instead: `() > fg_TempCopy(Promise);`. An attribute macro
may stand between a capture list and the parameters, apart from both,
`[&] mark_nodebug (int _A)`, and the lambda is one all the same: its body opens
one level in, and its parenthesis holds parameters. A brace behind a lambda's
introducer is that lambda's body whatever it holds, an empty one included, which
the source has nothing else to tell by; an empty brace behind a subscript
initializes an array. A guarded statement that holds a body is laid out across
lines whether or not the source wrote it on one, so `braces` puts it within
them.

The template arguments of a class that a name is qualified with stand in front
of that name, and are the last to give for the same reason a return type is:
`template auto TCActor<CManager>::f_InternalCallActor` keeps its qualification
whole and opens the arguments of `f_InternalCallActor` below it.

A name that does not fit even with its parameter list opened gives at its
qualification before it gives those arguments, which keeps the class whole and
puts what it qualifies on the line below it, as in
`TCSection<TCCommandLineSpecification<CFoo>>` with `::CSection::f_Register`
under it. The break taken is the last `::` that leaves what stands in front of
it on the line, and only one is taken: a head that needs a second has more in
it than a name, and opens the arguments after all, which is what an explicit
instantiation whose arguments fill a line of their own does.

A call or a subscript on what a call or a subscript yields is a postfix
operation like a member access and binds the same way, so it is a link of the
chain: `f_CallActor(&C::f_Fn)`, `[1]`, `(_Params)` and `.f_Timeout(10.0)` each
take a line once the chain gives. A lambda's parameter list, a cast's operand
and an empty list are not links. The chain starts at the first access on what
a call, a subscript or a braced value yields, an empty one included: `CFoo{}`
over `.f_SetNotes(...)` over `.f_SetTags(...)`, and `fg_CurrentActor()` over
`.f_Bind<...>(...)` over `.f_CallSync(...)`. A subscript hugs what yields it,
`f_Get()[0]`, where an attribute's brackets stand apart, `if (bFlag)
[[unlikely]]`.

A member chain that gives at all gives at every member, whether it resumes
under a closing marker or starts at the access that kept a scope whole, and
whether or not what is left of it would fit on one line. Filled a call at a
time it would end in a line of as many calls as happened to fit.

A named cast's parenthesis belongs to the head the way a C cast's does, since
what it yields is what the rest of the expression is written on. It stays closed
while everything up to the next scope fits on the line, so that the line gives
at the call behind it: `reinterpret_cast<CCallActor>(_Actor).template f_Bind`
with the scopes of `f_Bind` opened below.

A member chain that resumes under a call's closing marker and does not fit is
broken at every member, each under that marker. Taken one scope at a time it
would be broken only as far as it had to be, and end in a line of as many calls
as happened to fit.

A statement split at the parenthesis it opens with is the exception to a split
statement's terminator taking a line of its own. Its lines have no continuation
level and stand where the statement does, so a terminator on a line of its own
would stand among them as one more of them; it ends the last line instead, as in
`.f_CallSync();` under the closing parenthesis, and as the `);` of a fold
expression does. An operator behind the opened parenthesis stands under its
closing marker for the same reason, `> g_DiscardResult;`. A statement that
merely starts with a parenthesis, such as `(*fLocked)() > [&]` with its lambda
below it, has a continuation level and keeps the terminator on its own line.

A compound requirement inside a `requires` expression opens its statement with
a brace, and holds an expression rather than statements: it is no block, its
braces stand apart from the expression, the arrow behind it names a concept,
`{ _fOnEntry(_Key) } -> cIsSame<bool>;`, and one written across lines comes back
to one, since it is no initializer's data either.

What follows a block continues its statement only where the block is a lambda's
body inside an expression: an operator, a member access or a call behind the
closing brace. Two brackets open an attribute, which starts the next
declaration, `[[nodiscard]] bool f_IsEmpty() const` behind a member's body.

A brace holds statements, rather than the elements of a braced initializer,
when a statement terminator stands at its own level or an element of it starts
with a keyword only a statement begins with. A body whose every statement is
compound, such as a lambda that does nothing but loop, has neither a terminator
of its own nor an initializer's shape, and the keyword is what tells it apart.
A definition's keyword stands behind the template header that declares it, and
the head is read from there: without stepping over `template <typename t_C>`,
a class template whose body holds nothing but a function definition would be
read as an initializer, and nothing inside it would be laid out or spaced as a
declaration.

Comment lines of their own stand where the code under them does. In front of a
block's closing brace they stand at the level of the block's last statement. In
front of a label they end the body above it where the source has them deeper
than the label, and introduce the label otherwise; behind a class's opening
brace they stand among the members, since an access label stands at the brace's
level. Inside a construct other than a namespace a directive is no level of the
code's, so comments in front of one stand where the code behind it does; outside
functions, comments in front of a directive other than a conditional's or a
pragma stand where the statement above them does. A comment line continuing a
trailing comment, written further in than the code that comment trails, stays
with it. A directive taken out of the code with a line comment,
`//#define DDebug`, stands at column one as directives do, keeps its text, and
parts the comment lines around it into runs of their own.

A file that starts with `{` holds the elements of a list another file includes
inside its braces, and is laid out as those elements are: one element to a row
where they fit, each later one behind a leading comma, a comma ending the file
kept.

A comment standing between a line break and the token it precedes is text,
not indentation: the layout writes the indentation in front of the comment and
leaves the comment where it was, `/*Result =*/ mp_Results.f_Insert(_Measure)`.
A directive's trailing whitespace is layout, like a line comment's, and is
trimmed without counting as a change to the token stream.

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

What the source keeps is the depths of the lines relative to each other, and
only where nothing places a line. A statement a guard in front of it governs
stands one level in from the guard, and a body behind a head a directive cuts
off stands at the head's line, as `g();` does above; an `else` stands where the
`if` it answers does. Only an alternative branch, `#else` or `#elif`, between
the two keeps the source's depth, since the two are never compiled together.

A branch that opens a bracket it does not close, or closes one it did not open,
holds a piece of a construct rather than a whole alternative. The file then has
no line structure to be laid out against, which `structure` reports, naming the
conditional rather than the construct it left open.

Everywhere else a directive is transparent: it only ends the line it stands on,
as a line comment does. A line comment inside a construct is treated the same
way: what stands behind it is a line of its own and is measured as one, so a
base clause under `class CFoo // Comment` stays closed where it fits, and a
member chain broken at a comment stays broken at every member. The construct
around it is written split, each stretch between two directives is a line of its
own at the level the construct gives it, and a stretch is only broken further
where it is still too long, so no construct is opened up to make room that no
line of it needs. An operator standing at a directive is one the construct is
written broken at, and then every operator that binds as loosely takes a line of
its own; where no directive stands at an operator none is broken, since what a
branch holds binds tighter than the boundary around it.

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

A conditional whose branches write alternative heads for one body, `#if A`
over `if (a) {` over `#else` over `if (b) {` over `#endif`, cuts no construct
apart: every branch opens the same brackets. Each branch is then laid out in a
variant of the file that holds it alone, the plan takes each branch's lines
from its own variant and the rest from the first, and the merged result is
verified as any plan is. Branches that open different brackets keep the
directives opaque.

Directive lines are written by `directive-indentation` alone: at column one,
indented behind the `#` by conditional depth, a block moving around them
included.

## Objective-C

The `malterlib` profile reads C++ with Objective-C's additions, so one profile
covers C, C++, Objective-C and Objective-C++, and a header included from both
needs nothing of its own. No addition can be read into valid C++: a C++ bracket
holds no two operands side by side, a subscript stands behind its operand, and a
capture list separates its captures. The C++ rules apply to all of it. The lexer
reads `@` and a name as one word, `@"..."` as a string, and the `@` of an object
literal, `@[`, `@{` and `@(`, as a token that hugs its bracket.

A bracket whose top level writes two operands side by side, the receiver and the
selector, is a message send. Each selector part starts at a name standing next
to an operand, so a conditional's `b : c` is no part. The receiver and each part
stand apart, a keyword hugs its `:` and the argument stands one space behind it,
and `@selector(...)` is written as one word:

```objc
[pMenu addItem: pItem atIndex: 0];
NSSound *pSound = [[NSSound alloc] initWithData: pData];
SEL Selector = @selector(setObject:forKey:);
```

A send too long for its line opens its bracket: the receiver and each selector
part take a line of their own one level in, and a send that opens its statement
ends it with the terminator behind its closing bracket, as a statement opening
with a parenthesis does. A block passed along is laid out as a lambda is, `^`
behind the comma and its brace below it:

```objc
[
	pMachine
	startWithCompletionHandler: ^(NSError *_pError)
	{
		fg_Run();
	}
];
```

`@interface`, `@implementation` and `@protocol` open a section with a line of
their own, which writes its base, category and protocols apart:
`@interface CFoo (CBar) : NSObject <CDelegate>`. Instance variables in braces
below it are a block, `@public`, `@private`, `@protected` and `@package` stand
at its brace's level as an access specifier does, and `@end` is a statement of
its own. A method writes its sign apart, a type in parentheses against the name
behind it, and each keyword against its `:` with the parameter one space behind
it. Its signature is one line where it fits and gives at each keyword part
behind the first, one level in, where it does not; its body is a function's:

```objc
- (void)f_Set: (int)_Value with: (int)_Other
{
}
```

`@try`, `@finally` and `@autoreleasepool` introduce a block as `try` does, and
`@catch` and `@synchronized` stand apart from their parenthesis as a clause
does.

## Build system files

A file opted in with `malterlib_format = malterlib-buildsystem` is
`ECodeLanguage::mc_BuildSystem`. Malterlib opts in its hand-written
`.MBuildSystem`, `.MConfig`, `.MGeneratorSettings`, `.MHeader`, `.MInclude`,
`.MLBuildOptions`, `.MOptionalHeader`, `.MSettings`, `.MTarget` and
`.MWorkspace` files; `.MRepo` is JSON, and `.MRepoState` and `.MGeneratorState`
are written by the build system, so none of those is.

Their rules change whitespace only:

| Rule | Effect |
| --- | --- |
| `line-break` | A value is laid out by the rules C++ code is, with its openers ending their lines (see below). |
| `indentation` | A line the value layout does not place stands at the level of the innermost bracket open at its start, and a line that starts by closing it one level out. |
| `blank-line` | One blank line at most, none right inside a bracket, and none at the start or end of the file. |
| `trailing-whitespace`, `line-ending`, `final-newline` | As for C++. |
| `line-length` | Reported only. A string ending a line counts for no width in the report. |

A statement is what stands from a line no value bracket is open at to the line
its brackets close on; a `{` starting its line outside a value opens a scope of
the registry, which keeps its lines. A value written across lines is brought
back onto one where it fits, as a call is in C++, unless it holds an array or
object written across lines, which keeps its lines with each element on a line
of its own. A value too long for its line opens the widest bracket it holds
whole, or the one holding what keeps its lines: the opener ends its line, the
members stand one level in, split at their commas with each comma in front, or,
without commas, in front of the operators that bind loosest, and the closer
stands at the line's level. A member laid out the same way opens in turn, and
two objects that both open meet on the line one closes on, `}, {`. A string
ending a line counts for its width here; only the report leaves it out.

Inside a bracket the parser reads a line break as whitespace, and a statement
no bracket is open in ends at one, so only a value inside a bracket is split
at its operators or member calls. There, as in C++, a value's own operators
split it before a bracket it holds opens, in front of the ones binding
loosest by the build system's precedence: `?` and `:`, then `||` and `??`,
`&&`, `|`, `^`, `&`, `==`, `!=`, `<==>` and `<!=>`, the other comparisons,
`<<` and `>>`, `+` and `-`, and `*`, `/` and `%`. A binary operator is a run
of operator characters with a blank on both sides. What follows the first
operator continues one level in, except in a grouping parenthesis, one not
written against a name, whose operands stand at its level, as a C++
condition's do. A member chain gives at every `->` from the first access on
what a bracket yields, one level in, unless opening its widest bracket leaves
the lines in front of and behind that bracket fitting:

```
	Compile.Disabled (
		this.Identity->GetFile() == "crtbegin.c"
		|| this.Identity->GetFile()
			->GetPath()
			->GetFile()
			== "dso_handle.c"
	)
	Value Target.Type->SwitchWithError(
		"Unsupported: "
		, "StaticLibrary", (Documentation <==> "Doxygen")
			? `@(Prefix)libtool`
			: `@(Prefix)llvm-lib`
	)
```

A comma ending an opened list goes, the one change to the tokens these rules
make, which verification allows for since such a comma separates nothing:

```
	SdkRoot_macOS: string = (
		(MalterlibMaxSupportedMacOSVersion == "Latest")
		? `@(Developer)/MacOSX.sdk`
		: `@(Developer)/MacOSX@(MalterlibMaxSupportedMacOSVersion).sdk`
	)
	License =+ [
		{
			Path: "A"
		}, {
			Path: "B"
		}
		, { Path: "C" }
	]
```

A call to one of the language's switch functions always opens, whether or not
it fits. The value switched on, unless the function is called on it with `->`,
and the error or default of `SwitchWithError` and `SwitchWithDefault`, take a
line each, and each key shares its line with its value:

```
	f_SourceLanguageFlag _Type->SwitchWithError(
		"Unsupported compile type: "
		, "C++", "/TP"
		, "C", "/TC"
	)
```

A statement holding a comment other than one ending it, or continued by a
backslash, keeps its lines.

A line a backslash continues aligns with the value it continues, the first
string on the line the chain starts on, its tabs filled out with spaces up to
that column where the string does not start on a tab stop:

```
	Contents	`#include <Mib/Core/Core>\n`\
				`namespace NMib\n`
```

A comment that starts its line at column one comments out what it stands in
front of, as `//\tKey Value` does, and keeps that column. Strings, template
strings with the expressions inside them, and block comments are single tokens,
so a bracket inside one counts for nothing. A bracket that closes nothing, one
that never closes, and a literal the file ends inside make the file unsupported.

## Protected regions

Layout inside multiline tokens is never touched: block comments, string and
character literals including raw strings, and the line a backslash splice ends
on. A directive continued over lines is the exception: `directive-indentation`
moves its continuation lines and the splices behind them. A leading UTF-8
signature is its own token and is preserved.

clang-tidy's `NOLINT` suppresses the diagnostics of the line it stands on, and
`NOLINTNEXTLINE` those of the line below it, so the code on such a line is
neither split nor joined with another line's, and the comment never moves off
it.

A paired directive disables the engine for a region:

```cpp
// malterlib-format off
	int Aligned   = 1;
// malterlib-format on
```

Both lines are excluded along with everything between them, and none of them is
reported for its length: data written to a width of its own, such as a raw
string's test input, is what the region is for. An unmatched or nested
directive is a failure, not a silently ignored comment.

Inside a block comment the directive stands on a line of its own without the
`//`, as in front of code commented out, or as the whole comment:

```
/*	malterlib-format off
	f_GetSetting switch(GeneratorSetting.f_GetSetting<Value>) {
	malterlib-format on
*/
/* malterlib-format off */
```

A layout directive on the first line inside a braced list lays the list out in
aligned rows, and the `layout-directive` rule writes it:

```cpp
ch8 g_Table[] =
	{
		// malterlib-format columns: 4
		  'A', 'B', 'C', 'D'
		, 'E', 'F' // Last
		, 'G'
	}
;
```

- `columns: N` puts N elements in each row, `columns: auto` as many as fit
  within `max_line_length`.
- `table` puts one braced record in each row, its fields the columns.
- `rows` keeps the elements on the lines they were written on, without
  aligning them, and a string continued on the next line stays continued at the
  rows' level: data written as records of different lengths, such as a header
  and the fields behind it.

Rows after the first start with their comma, and with columns the first is
indented as far as the comma takes. A column is as wide as its widest cell,
numbers aligned to the right and anything else to the left. A comment behind an
element ends its row, and comment lines above an element start its row and stay
above it. A list holding
a comment elsewhere, or an element its row cannot spell on one line, keeps its
lines and the directive is reported; so is a row longer than
`max_line_length`.

`// malterlib-format align: =` aligns the `=` of each line in the run of lines
directly below it, one space behind the longest left-hand side; a blank line,
a comment line, or a line holding no `=` ends the run.

Any other `malterlib-format` directive, and a layout directive anywhere but the
first line inside a braced list, is a failure.

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

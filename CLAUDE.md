# CLAUDE.md - Develop Module

Develop is an optional module for reusable editor, IDE, and developer-tool
support. Follow the framework guidance in `../Core/CLAUDE.md`.

## Structure

- `Include/Mib/Develop/`: public include wrappers.
- `Source/`: public declarations and implementations.
- `Documentation/`: component documentation.
- `Test/`: native Malterlib tests.
- `Malterlib_Develop.MHeader`: library and test registration.

## EditorConfig

The public API is `<Mib/Develop/EditorConfig>`, in `NMib::NDevelop`.
See `Documentation/EditorConfig.md` for the loader, cache, and boundary semantics.

Keep configuration parsing and resolution independent of MTool and Git.
Keep consumer-specific validation rules and diagnostic output in the consumer.
The resolver is an actor and `f_Resolve` returns `TCFuture`. Dispatch filesystem
operations to the resolver's `CSharedRoundRobinBlockingActors`, which a host
running many resolvers passes in so their loads share a bounded set of threads.
Custom loaders are `TCActorFunctorWeak` callbacks returning futures and taking
their path by value. They support virtual projects and snapshots. Preserve
custom property values and apply generic `unset` semantics.

Actor methods can be reentrant across awaits. Coalesce pending configuration
loads and preserve the cache generation captured by each resolve. Cache clearing
must prevent older completions from populating the new cache. Propagate load
failures to all waiters and allow subsequent retries. Use `fg_AsyncDestroy` when
owning resolver actors.

The compiled glob implementation is private to EditorConfig and currently
supports a documented subset of the specification. Do not describe it as a
fully conforming EditorConfig core without running the upstream conformance
cases.

## Code formatting

The public API is `<Mib/Develop/CodeFormatting>`, `<Mib/Develop/TextLayout>`,
and `<Mib/Develop/CodeFormattingStructure>`, in `NMib::NDevelop`. See
`Documentation/CodeFormatting.md` for the opt-in property, settings, rule
matrix, line structure, protected regions, and range contract.

The build system's `.M*` files are a second language, opted in with
`malterlib_format = malterlib-buildsystem`. The `.editorconfig` is
authoritative: the profile alone selects the language, and nothing in the engine
or MTool may infer one from a file's name. It has rules of its own
(`fp_PrepareBuildSystemLines` and the `fp_RuleBuildSystem*` rules), sharing the
lexer, ranges, protected regions, disabled regions and verification with C++.
Its rules read brackets and lines alone and never build a `CCodeStructure`; keep
them whitespace-only, and settle a new rule against the whole tree's `.M*` files
before enabling it, since nearly every line there already follows one convention.

`fg_AnalyzeCodeFormatting` is pure: it takes immutable source bytes and returns
an ordered, non-overlapping edit plan plus diagnostics. Introduce no filesystem
operations, Git access, or console output in the engine; those belong in the
consumer. `MTool Format` applies the plan and `MTool Validate` reports it, so a
rule must never be reimplemented as a separate regular-expression check.

Every rule but six conversions changes whitespace only: the trailing return
type, the braces dropped around a single guarded statement, a qualifier moved
behind the type it leads, `static` moved in front of `constexpr`, an empty
statement taken out behind another terminator, and the comma behind an enum's
last enumerator taken out. A conversion is decided first, on the original
source, and the layout is made on the converted source, so a plan is verified
with `fg_HasEquivalentCodeTokens` against the converted source, and a whole-file
plan is re-analyzed to prove it converged; a failing check reports a formatter
failure instead of emitting edits. Conversions that can rewrite the same text
are made in stages rather than taught about each other: words that only change
places, qualifiers and specifiers, move first, and terminators that end nothing
go with them; the source that leaves is analyzed by an inner analyzer that makes
the other two, and `fg_ComposeEdits` merges any edit that reaches into a conversion's text
with that conversion. A conversion has to be a fixpoint of its own rule, which
the re-analysis enforces per file: one whose output it would convert again,
as a moved qualifier in front of a macro would be, must refuse instead. Another
rule that would change tokens, such as adding required braces, needs its own
precondition proof and structural equivalence tests before it is enabled.

The line-break rule works in two phases: a statement is first taken as one
line, and then split outermost break first, only where a line is still too
long. Layout decisions are recorded per gap between tokens and written out
once; never emit an edit from inside the layout, since a decision must not
depend on an edit already made or on where the source broke its lines.

A conditional's branches reach the engine as one token stream, one branch after
another. That is the same shape as any single branch only while no construct is
cut by a branch boundary; where one is, the directives are opaque and the
construct around them keeps its lines. A transparent directive ends the line it
stands on, as a line comment does, and the stretches between directives are the
construct's lines.

Prefer an explicit unsupported result over a guessed edit. Ambiguous spellings
stay out of the matrix: `&`, `&&`, and `*` are declarators as well as
operators, read as declarators only where `fg_IsDeclaratorToken` can prove it
and as operators only where `fg_IsInfixOperator` can prove that no declaration
stands where they do. The structure builder answers the same way: an
unclassified construct keeps its layout, and `fg_GetCanonicalSpacing` returns
`mc_Preserve` for a pair the standard does not settle, which is what makes a
relayout refuse rather than guess.

A project's naming is evidence the standard leaves. It comes from
`.malterlib-format` (see the documentation's Naming section) as roles the lexer
gives each identifier, so a name the naming lists as a function's names a
function wherever it stands. Never hardcode a name or prefix in the engine: ask
`f_HasRole`, and add a list to the document when a new kind of name is needed.
Resolving the document is I/O and belongs to MTool, which caches it per
directory. Read the naming only where C++ itself settles nothing and
the convention answers on its own, as `fg_NamesCall` does for a parenthesis
inside a template argument list, which spells a call, a construction and a
function type alike. A type prefix is weaker evidence than it looks: the same
name constructs a value as readily as it names a function type's return. Where
a parenthesis holds one name and nothing else, `fg_NamesType` reads a type
prefix or a fundamental alias as a cast, since `(aint)` converts and `(Count)`
groups.

A decision and the spelling it is measured against have to agree. Width is
measured at the width the spacing rules write, never at the source's, and a
spacing rule never writes into a gap the layout breaks. Every convergence
failure the whole tree produced was one of those two disagreements, or a
decision taken against the source's indentation before the layout moved the
block it stood in: `fp_IsInFunctionBody` and the attribute statement's level
came out of the same trial. Reproduce such a failure by dumping both passes
from the stability check; the message alone names only the line that moved.

Leaving a pair unsettled costs more than an unchanged gap. A line holding one
has no single-line form to measure, so the layout cannot join it and, until the
gap is settled, cannot break it up either: every statement split at its `=`
went unlaid out for that reason, overlong lines included. Settle what the
sources really do spell one way, and keep `mc_Preserve` for what they spell
two.

The engine's cost is almost all predicates run per gap, so keep them free of
allocation and of scans over the node list. `f_IsText(Token, "...")` compares
in place and inline (the `CStr` overload built a heap string per question and
was two thirds of the run), `CCodeStructure` indexes the node opening, closing
and enclosing at each token and the statement ending there, and the analyzer
remembers each gap's canonical and inline spacing and each token's width. A
scan over the nodes per token is quadratic in one statement's size: a single
14000-element table initializer took 0.75 s, longer than the rest of the tree
on ten cores, until its requirement-brace check used the index. Rule names and
explanations stay literals until an edit is made. A pass that makes no edit is
not verified against itself, which is what makes a check of an already
formatted tree cheap. `./mib format --check` on the whole tree of 4032 files
takes about 0.55 s on ten cores, 0.18 s of it loading the build system; a serial
`-j 1` run (about 2.1 s) is the number to compare an engine change against.

Corpus trials are part of the work, not a final check. Every structural bug in
the line-break rule so far was found by reading a diff of already-correct
sources, not by a unit test: a lambda body parsed as an initializer, a clause
body pulled onto its condition, and a group joined across a statement boundary.
Turn each one into a golden case.

## Tests

Read `../Test/CLAUDE.md` before modifying tests. Use native tests for parser,
resolver, lexer, and formatting-engine behavior; MTool's integration tests cover
the command line, staged/base validation, and safe writes. Formatting tests must
cover golden output, idempotence, token equivalence, protected regions, range
endpoints, and explicit unsupported cases.

```bash
MalterlibBuildShowProgress=false ./mib build-target Tests Com_Test_Malterlib_Develop
/c/Deploy/Tests/Test_Malterlib_Develop.exe --no-color
```

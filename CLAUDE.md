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
operations through `fg_BlockingActor`. Custom loaders are `TCActorFunctorWeak`
callbacks returning futures and taking their path by value. They support virtual
projects and snapshots. Preserve custom property values and apply generic `unset`
semantics.

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

`fg_AnalyzeCodeFormatting` is pure: it takes immutable source bytes and returns
an ordered, non-overlapping edit plan plus diagnostics. Introduce no filesystem
operations, Git access, or console output in the engine; those belong in the
consumer. `MTool Format` applies the plan and `MTool Validate` reports it, so a
rule must never be reimplemented as a separate regular-expression check.

Every rule in the current matrix changes whitespace only. A plan is verified
against `fg_HasEquivalentCodeTokens`, and a whole-file plan is re-analyzed to
prove it converged; a failing check reports a formatter failure instead of
emitting edits. A rule that would change tokens, such as adding required braces
or converting to a trailing return type, needs its own precondition proof and
structural equivalence tests before it is enabled.

Prefer an explicit unsupported result over a guessed edit. Ambiguous spellings
stay out of the matrix: plain `=` is also a lambda capture default and the tail
of the `_o=` DSL, and `&`, `&&`, and `*` are also declarators. The structure
builder answers the same way: an unclassified construct keeps its layout, and
`fg_GetCanonicalSpacing` returns `mc_Preserve` for a pair the standard does not
settle, which is what makes a relayout refuse rather than guess.

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

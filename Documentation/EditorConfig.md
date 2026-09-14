# EditorConfig

Include `<Mib/Develop/EditorConfig>` and use the types in `NMib::NDevelop`.

`CEditorConfig` parses configuration text once and compiles its section patterns.
`f_Apply(relativePath, properties)` applies matching sections to a property map.
Later sections override earlier ones; `unset` removes a property. A document
with `root = true` clears inherited properties. Standard case-insensitive values
are normalized, while custom property values retain their spelling.

```cpp
NDevelop::CEditorConfig Configuration("root = true\n[*.cpp]\nindent_style = tab\n");
NDevelop::CEditorConfigProperties Properties;
Configuration.f_Apply("src/main.cpp", Properties);
```

`CEditorConfigResolver` is an actor that discovers configurations from a file's directory upwards,
stopping at `root = true`, an optional boundary directory, or the filesystem root.
It applies the discovered documents from parent to child. A source file need
not exist to resolve its properties.

```cpp
TCActor<NDevelop::CEditorConfigResolver> Resolver = fg_Construct();
auto DestroyResolver = co_await fg_AsyncDestroy(Resolver);
auto Properties = co_await Resolver(&NDevelop::CEditorConfigResolver::f_Resolve, "/project/src/main.cpp");
```

The default loader dispatches file existence checks and reads to `fg_BlockingActor`,
keeping filesystem I/O off the thread pool. The other constructor accepts an
`FEditorConfigLoader`: a `TCActorFunctorWeak` returning `TCFuture<TCOptional<CStr>>`.
Create it with `g_ActorFunctorWeak / [](CStr _Path) -> TCFuture<TCOptional<CStr>> { ... }`
on the actor that owns the loader's state. It receives an absolute configuration
path by value. An empty optional means missing; an empty string means a present,
empty document. An expired loader actor reports an error. This supports virtual
projects and preloaded snapshots without introducing a Git dependency.

The resolver caches both present and missing documents, and the chain of
documents that applies to a directory, so the files of one directory walk the
directory tree once. Loads run on one blocking actor per resolver, however many
resolves are in flight. Overlapping resolves
share pending loads for the same configuration. Failures reach every waiting
caller and are not cached, allowing a later call to retry.

After the source changes, await `Resolver(&CEditorConfigResolver::f_ClearCache)`
or create a resolver per immutable snapshot. Clearing starts a new cache
generation: existing resolves finish using their old generation, while new
resolves use the new one. A late result cannot repopulate the cleared cache.
This controls cached state, not the underlying source: a custom loader that
needs an atomic snapshot must supply immutable contents.

Patterns support Unicode literals, `*`, `**`, `?`, character sets and ranges,
negation, escapes, and nested brace alternatives. Matching is case-sensitive
and uses slash-separated paths. Numeric brace ranges are currently rejected;
this API does not claim full EditorConfig conformance. Patterns are limited
to 1024 Unicode characters.

This module resolves configuration. Consumers decide which properties to
enforce, how to report violations, and whether to restrict discovery to a
repository boundary. MTool uses a boundary and supplies Git snapshot contents
for changed-line validation.

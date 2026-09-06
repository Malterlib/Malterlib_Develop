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

## Tests

Read `../Test/CLAUDE.md` before modifying tests. Use native tests for parser
and resolver behavior; MTool's integration tests cover staged/base validation.

```bash
MalterlibBuildShowProgress=false ./mib build-target Tests Com_Test_Malterlib_Develop
/c/Deploy/Tests/Test_Malterlib_Develop.exe --no-color
```

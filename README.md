# Malterlib Develop

Optional, reusable support for editors, IDEs, and developer tools.

The components are [EditorConfig parsing and resolution](Documentation/EditorConfig.md),
available through `<Mib/Develop/EditorConfig>`, and [code formatting](Documentation/CodeFormatting.md),
available through `<Mib/Develop/CodeFormatting>` and `<Mib/Develop/TextLayout>`.
All of them live in namespace `NMib::NDevelop`.

The library target is `Lib_Malterlib_Develop`. Add it as a dependency of a
consumer target; MTool does this for its source validator and formatter.

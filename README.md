# Malterlib Develop

Optional, reusable support for editors, IDEs, and developer tools.

The first component is [EditorConfig parsing and resolution](Documentation/EditorConfig.md),
available through `<Mib/Develop/EditorConfig>` in namespace `NMib::NDevelop`.

The library target is `Lib_Malterlib_Develop`. Add it as a dependency of a
consumer target; MTool does this for its source validator.

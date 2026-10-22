# Oxygen.Editor.Schemas.Generators

The engine declares the authored scene node components once, in
`Oxygen/Scene/Schema/SceneComponents.yaml`: each component's kind, its
properties and the component and field ids the engine applies them by.
[SceneComponentsGenerator](src/SceneComponentsGenerator.cs), a Roslyn generator,
reads that file as an additional file and generates the editor's side; the
engine build generates its own header from the same file.

A project sets `SceneComponentsOutput` to what it owns and imports
[SceneComponents.targets](SceneComponents.targets):

| Output        | Project     | Generated                                                         |
| ------------- | ----------- | ----------------------------------------------------------------- |
| `WireIds`     | Runtime     | `RuntimeComponentId` and one `<FieldSet>Field` enum per field set |
| `PropertyIds` | Scenes      | `SceneSchema`: each component's `Kind` and typed `PropertyId`s    |
| `WireTable`   | LivePreview | `SceneWire`'s property-to-component, fields and encoding table    |

A problem in the schema is a compiler error at the offending value:

| Id          | Problem                                                               |
| ----------- | --------------------------------------------------------------------- |
| `OXGNSC001` | `SceneComponentsOutput` is empty or names an unknown output           |
| `OXGNSC002` | The project has no `SceneComponents.yaml` additional file, or several |
| `OXGNSC003` | The file is not well-formed YAML                                      |
| `OXGNSC004` | A missing or unknown key, a value of the wrong kind, or a duplicate   |
| `OXGNSC005` | A component's field set or a property's field does not exist          |
| `OXGNSC006` | A value type or enumeration has no editor type                        |
| `OXGNSC007` | A property's fields do not match its value, or it has fields unwired  |
| `OXGNSC008` | A property names an encoding the editor does not implement            |

YamlDotNet, which reads the file, is embedded in the generator and never reaches
the editor. The Verify snapshots in [tests/Snapshots](tests/Snapshots) hold the
generated code of the engine's schema and each error.

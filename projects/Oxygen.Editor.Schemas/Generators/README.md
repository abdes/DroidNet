# Oxygen.Editor.Schemas.Generators

The engine declares the authored scene node components once, in
`Oxygen/Data/Schema/SceneComponents.yaml`: each component's kind and
cardinality, its properties with their defaults, ranges and units, the override
ids of the properties a scene stores as overrides, and the component and field
ids the engine applies them by. The engine SDK installs the file in
`share/oxygen/schemas`, and the editor reads that installed copy
(`$(OxygenSdkSchemasDir)`, from the repository's `Common.props`), never the
engine source tree.
[SceneComponentsGenerator](src/SceneComponentsGenerator.cs), a Roslyn generator,
reads it as an additional file and generates the editor's side; the engine
build generates its own header from the same file. A build without the
installed file fails, asking for the SDK to be installed.

A project sets `SceneComponentsOutput` to what it owns and imports
[SceneComponents.targets](SceneComponents.targets):

| Output        | Project     | Generated                                                                                                                                                                                                                                                                                                           |
| ------------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `WireIds`     | Runtime     | `RuntimeComponentId`, one `<FieldSet>Field` enum per field set, and `RuntimeOverrideId`                                                                                                                                                                                                                             |
| `PropertyIds` | World       | `SceneSchema`, in `Oxygen.Editor.World`: each component's `Kind`, `IsRequired` and typed `PropertyId`s, nested part classes (`SceneSchema.Geometry.Surface`), `Overridable`, and per component `Defaults`, `RootDefaults`, `Ranges`, `Units` and `DisplayUnits`; `RangeOf`, `UnitOf`, `DisplayUnitOf`; `SceneUnits` |
| `WireTable`   | LivePreview | `SceneWire`'s property-to-component, fields and encoding table                                                                                                                                                                                                                                                      |

A problem in the schema is a compiler error at the offending value:

| Id          | Problem                                                                       |
| ----------- | ----------------------------------------------------------------------------- |
| `OXGNSC001` | `SceneComponentsOutput` is empty or names an unknown output                   |
| `OXGNSC002` | The project has no `SceneComponents.yaml` additional file, or several         |
| `OXGNSC003` | The file is not well-formed YAML                                              |
| `OXGNSC004` | A missing or unknown key, a value of the wrong kind, or a duplicate           |
| `OXGNSC005` | A component's field set or a property's field does not exist                  |
| `OXGNSC006` | A value type or enumeration has no editor type                                |
| `OXGNSC007` | A property's fields do not match its value, or it has fields unwired          |
| `OXGNSC008` | A property names an encoding the editor does not implement                    |
| `OXGNSC009` | An override id, default, range, commit policy or unit does not fit a property |
| `OXGNSC010` | An enumeration's members differ from those of its editor type                 |

YamlDotNet, which reads the file, goes to the compiler as an analyzer next to
the generator (its `GetTargetPath` returns both) and never reaches the editor's
assemblies. The Verify snapshots in [tests/Snapshots](tests/Snapshots) hold the
generated code of the engine's schema and each error.

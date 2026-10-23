# SceneSchemaCodeGen

Generates the engine code of the authored scene node components from
[Schema/SceneComponents.yaml](../../Schema/SceneComponents.yaml), after
validating it against
[Schema/SceneComponents.schema.json](../../Schema/SceneComponents.schema.json)
and checking that its names, ids, field uses, enumerations, units, defaults and
ranges are consistent.

The Data build runs the generator before it compiles the module, when the
schema or the tool changes, and writes `Schema/Generated.SceneComponents.h`
into the source tree, where it is versioned:

- `ComponentId`: one id per component applied field by field;
- one `<FieldSet>Field` enumeration per field set, with `kCount` one past its
  largest id;
- `ValueType`, `PartKind`, `OverrideId`, `kOverrideProperties` and
  `FindOverrideProperty`: the address, value type and referenced asset type of
  every property a scene stores as overrides;
- `k<Enumeration>Members`: the wire values of each enumeration;
- one namespace per component kind (and part kind) with each property's
  default and `PropertyRange` in field units, after the property's
  `encoding`: per field for a wired component, per property otherwise. An
  enum field also gets its `RootDefault`, when declared, and its `Values`.

Units are checked but not generated: no engine code shows them. The header is
kept out of clang-format, since its layout is the generator's.

```powershell
python -m scene_schema_codegen.cli --schema ../../Schema/SceneComponents.yaml --json-schema ../../Schema/SceneComponents.schema.json --cpp-out ../../Schema/Generated.SceneComponents.h
```

Other consumers of the schema generate their own code from the YAML file; this
tool produces engine output only.

Tests: `python -m pytest tests/ -v`, registered with CTest as
`Oxygen.Data.SceneSchemaCodeGen.Tests`.

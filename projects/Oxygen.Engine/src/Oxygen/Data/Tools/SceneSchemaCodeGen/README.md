# SceneSchemaCodeGen

Generates the engine code of the authored scene node components from
[Schema/SceneComponents.yaml](../../Schema/SceneComponents.yaml), after
validating it against
[Schema/SceneComponents.schema.json](../../Schema/SceneComponents.schema.json)
and checking that its names, ids, field uses, enumerations, units, defaults and
ranges are consistent.

The Data build runs the generator before it compiles the module, when the
schema or the tool changes, and writes two headers into the source tree, where
they are versioned. `Schema/Generated.SceneComponents.h` holds:

- `ComponentId`: one id per component applied field by field;
- one `<FieldSet>Field` enumeration per field set, with `kCount` one past its
  largest id;
- `ValueType`, `PartKind`, `Encoding`, `ValueMember`, `OverrideId`,
  `kOverrideProperties` and `FindOverrideProperty`: the address, value type,
  encoding, change value member and referenced asset type of every property a
  scene can change (every wired property and every asset-sourced one);
- `k<Enumeration>Members`: the wire values of each enumeration;
- one namespace per component kind (and part kind) with each property's
  default and `PropertyRange` in field units, after the property's
  `encoding`: per field for a wired component, per property otherwise. An
  enum field also gets its `RootDefault`, when declared, and its `Values`.

`Schema/Generated.SceneChanges.h` holds `EncodeChange`, which turns a value in
schema units into a change value (degrees to radians, Euler angles to a
quaternion), and one `ApplyChange` per cooked component record, which writes a
change through the record's hand-written field writer
([SceneRecordFields.h](../../SceneRecordFields.h)). The generator maps each
wired component kind to its record and refuses a wired component without one.

Units are checked but not generated: no engine code shows them. The headers are
kept out of clang-format, since their layout is the generator's.

```powershell
python -m scene_schema_codegen.cli --schema ../../Schema/SceneComponents.yaml --json-schema ../../Schema/SceneComponents.schema.json --cpp-out ../../Schema/Generated.SceneComponents.h --changes-out ../../Schema/Generated.SceneChanges.h
```

Other consumers of the schema generate their own code from the YAML file; this
tool produces engine output only.

Tests: `python -m pytest tests/ -v`, registered with CTest as
`Oxygen.Data.SceneSchemaCodeGen.Tests`.

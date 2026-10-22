# SceneSchemaCodeGen

Generates the component and field ids of the authored scene node components
from [Schema/SceneComponents.yaml](../../Schema/SceneComponents.yaml), after
validating it against
[Schema/SceneComponents.schema.json](../../Schema/SceneComponents.schema.json)
and checking that its names, ids and field uses are consistent.

The Scene build runs the generator before it compiles the module, when the
schema or the tool changes, and writes `Schema/Generated.SceneComponents.h`
into the source tree, where it is versioned:

- `ComponentId`: one id per component applied field by field;
- one `<FieldSet>Field` enumeration per field set, with `kCount` one past its
  largest id.

```powershell
python -m scene_schema_codegen.cli --schema ../../Schema/SceneComponents.yaml --json-schema ../../Schema/SceneComponents.schema.json --cpp-out ../../Schema/Generated.SceneComponents.h
```

Other consumers of the schema generate their own code from the YAML file; this
tool produces engine output only.

Tests: `python -m pytest tests/ -v`, registered with CTest as
`Oxygen.Scene.SceneSchemaCodeGen.Tests`.

# Oxygen.Editor.ContentPipeline.Testing

The test kit of `Oxygen.Editor.ContentPipeline`: what other modules' tests use
in place of the pipeline's ports, instead of mocking them in each test. Only
test projects reference it.

- `FakeAssetRedirects` is an in-memory `IAssetRedirects`. `Move` and `Delete`
  record the session's relocations; `Resolve` follows every later move. No asset
  exists at a moved-from or deleted path.
- `FakeMaterialSlotProvider` is an in-memory `IGeometryMaterialSlotProvider`.
  `Publish`, `Remove` and `Fail` set what reading a geometry's inventory
  returns; `HoldReads` holds every read until the returned
  `MaterialSlotReadGate` is released, so a test can change a scene while a read
  is pending.

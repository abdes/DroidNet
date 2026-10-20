# Oxygen.Editor.ContentPipeline.Testing

The test kit of `Oxygen.Editor.ContentPipeline`: what other modules' tests use
in place of the pipeline's ports, instead of mocking them in each test. Only
test projects reference it.

- `FakeAssetRedirects` is an in-memory `IAssetRedirects`. `Move` and `Delete`
  record the session's relocations; `Resolve` follows every later move. No asset
  exists at a moved-from or deleted path.
- `FakeContentCookCoordinator` is an `IContentCookCoordinator` that runs one
  operation at a time over the active project, without the cook leases the real
  coordinator takes on disk, so it serves a project on an in-memory file system.
  Requests run in arrival order; pending cooks are not coalesced.
- `FakeMaterialSlotProvider` is an in-memory `IGeometryMaterialSlotProvider`.
  `Publish`, `Remove` and `Fail` set what reading a geometry's inventory
  returns; `HoldReads` holds every read until the returned
  `MaterialSlotReadGate` is released, so a test can change a scene while a read
  is pending.

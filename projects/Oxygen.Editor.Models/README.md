# Oxygen.Editor.Models

The model family module: what is true of the editor's models whatever shows
them. It holds no view, view model or dialog.

- `ModelImportSettings` reads and writes a model's import record,
  `<ProjectRoot>/imports/<source_identity>.import.json`, against the engine's
  `oxygen.model-import.schema.json` from the installed SDK. The ImportTool
  reads and writes the same records. A record holds the model's source, its
  virtual path and its import settings, and what earlier imports established:
  the source identity, the surface provenance, the node continuity, the
  published nodes and the source fingerprint.
- Relocating a model rewrites the record's source (`WithSource`) and nothing
  else. An import changes only what it establishes (`WithImportUpdate`,
  checked by `DiffersOnlyByImport`).
- `ModelImportRecords` indexes a project's records by source path and by
  virtual path. A model is imported when a record names it. The index reads the
  folder when it is created, so callers create one per operation.

The content pipeline, which cooks and imports models, depends on this module;
this module depends only on the shared asset formats.

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
- `ModelImportSettingCatalog` lists the settings a person chooses, each a
  `ModelImportSetting`: a job key with the importer's default and the manifest
  schema's values and ranges, read and written without touching other keys.
  `ModelMaterialRemap` reads and writes the material remap.
  `ModelGeometryDefaults` reads and writes the defaults the import writes into
  each geometry, by geometry anchor: its LOD policy and its surfaces'
  visibility and shadow casting. `ModelImportSettingScope` reads and writes a
  setting in the model's own settings or in one texture's overrides.
- `ModelSourceFacts` are what the source analysis reports of a model file: its
  format and version, units, axes, counts, scenes and the names it produces.
- `ImportRunSettings` are the ImportTool's execution settings of one import, with
  the defaults the installed import manifest schema states.
- `ModelImportSession` reads and writes an import session: the models of one
  import and the run's settings, as an ImportTool import manifest.
- `IModelImportPrompt` is the port of the Import model dialog: a UI surface asks
  it which models to import and gets a `ModelImportPlan`.
- `ModelDocumentKind` opens a model source file as a document titled with its
  extension. Its `ModelContent` names the model and follows its source when it
  moves. It edits the model's import record (settings and node continuity), each
  change one history step, and never writes the source. Save goes through the
  `IModelImportRecordWriter` port, which replaces the record and reimports the
  model as one operation. `TakeRecord` takes what an import rewrote and keeps
  unsaved edits.
- `IModelFacts` is the port that reads a model's `ModelFacts`: its source, last
  import and counts, which Properties shows on the model's root.

The content pipeline, which cooks and imports models, depends on this module.
This module depends on the shared asset formats, the project session (to find a
model's record) and the scene model's LOD policy.

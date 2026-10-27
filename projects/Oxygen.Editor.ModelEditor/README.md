# Oxygen.Editor.ModelEditor

The model family's UI: the **Model Editor** of model documents, and the
**Import model** dialog and its import settings panel. The Content Explorer
opens the dialog through `IModelImportPrompt` and Properties reads a model's
facts through `IModelFacts`, ports of `Oxygen.Editor.Models`; neither references
this project.

## The Model Editor

A model document opens from the Content Explorer (double-click an imported
model) and shows the model's preview scene, which `DocumentScenes` builds
(`Oxygen.Editor.Scenes`), in one viewport of the `Viewing` profile: no transform
tools, layouts or maximize, a click selects the node under the cursor, and Show
and the view mode offer _Preview environment_ and the exposure. The toolbar
saves, reimports when the source changed or the last import failed, places the
model in the open scene (_Add to scene_), browses to the selection's geometries
or the model file, and says where the import stands. Its tab stays in its
window (`CanTearOut` is false). `ModelFactsReader` reads the facts Properties
shows on the model's root: the source analysis's counts, the last import and
the LODs of the imported geometries.

Beside the viewport, behind a splitter, `ModelImportSettingsPanel` edits the
model's import record; its width and whether it is collapsed are per user
(`ModelEditorLayout`). Each edit is one step of the document's history, and a
banner says the view shows the last import until Save. Save goes through
`IModelImportRecordWriter`, which `ModelImports` implements with the content
pipeline's `ReplaceModelRecordAsync`: the record is replaced and the model
reimported as one operation, whatever saved the document. After every cook the
editor takes the record an import rewrote, keeping unsaved edits. The panel
adds three Model Editor groups:

- **Node continuity.** The anchors the last import lost and the new ones. A lost
  anchor offers _Continue…_: the nodes of the last import, new ones first, with
  Find and a caution. Every entry can be removed, and an unsaved entry carries
  the override bar. Clicking an anchor selects its node.
- **Selected mesh.** The selected node's geometry defaults, keyed by the
  geometry's anchor from the record's surface provenance: the LOD policy, in the
  shared `LodPolicyEditor`, and each surface's visibility and shadow casting.
- **Selected texture.** The textures the selected mesh's materials use, read
  from the cooked materials (`IMaterialTextures`). The chosen texture's maximum
  size, compression quality and normal map convention override the Textures
  group.

`ModelImportProblems` lists each open model's kept import problems, the
diagnostics its publication keeps (`IPublishedDiagnostics`), under the model's
file in the Problems pane. A problem that names a node selects it. The
toolbar's status counts them, and its link opens Problems filtered to the
model.

## The Import model dialog

- **Files.** A list of model files, every one selected when the dialog opens,
  with the standard list selection (click, Ctrl+click, Shift+click, Ctrl+A). A
  compact toolbar adds files (Ctrl+O, or dropped from File Explorer), removes them
  (Delete) and moves them (Alt+Up, Alt+Down); rows also reorder by dragging. The
  list's order is the order of the run's jobs and of a saved session. A file whose
  source is missing or whose name is taken blocks the import until it is fixed or
  removed.
- **Name and folder.** Name shows with one file selected; the content folder edits
  every selected file. A taken name blocks the import until the model is renamed or _Replace_ is chosen; the replaced
  model keeps its import record, which takes the chosen settings.
- **Settings.** `ModelImportSettingsPanel` edits the selected files as the
  Properties pane edits a multi-selection: shared values show, differing ones are
  Mixed, the override bar is full or half, and a row that applies to only some
  files edits those and says which. The rows are the editor's shared property
  editors (`Oxygen.Editor.Controls`) and asset picker (`Oxygen.Editor.Pickers`).
  The settings and their importer defaults are `ModelImportSettingCatalog`'s.
- **Facts and Before import.** Each added file is analyzed
  (`IModelSourceAnalyzer`, the ImportTool's `analyze-sources`). A full-width
  InfoBar states its format, units, axes and counts (or how many files are
  selected); its severity is the most serious finding's and its title counts the
  errors and warnings. Its action, _Before import_, opens what the import will
  report as a flyout of `DiagnosticCard`s, each naming the objects it is about.
- **Result.** The Units and axes group says what the import does to the model's
  size and orientation in Oxygen's space (right-handed, Z up, metres).
- **Import run.** The chevron of the Import button opens the ImportTool's
  execution settings (threads, jobs at once, each pipeline's workers and queue);
  unset values show the installed manifest schema's defaults.
- **Import sessions.** _Open session…_ and _Save session…_ read and write the
  list and the run as an ImportTool import manifest (`ModelImportSession`), which
  `ImportTool batch --manifest … --cooked-root …` runs as is.

The dialog returns a `ModelImportPlan`; the Content Explorer imports it with
`IContentPipelineService.ImportSourcesAsync`, the new models as one cook run.

## Dependencies

`Oxygen.Editor.Models`, `Oxygen.Editor.Scenes`, `Oxygen.Editor.LivePreview`,
`Oxygen.Editor.Viewports`, `Oxygen.Editor.ContentPipeline`,
`Oxygen.Editor.Controls`, `Oxygen.Editor.Pickers`, `Oxygen.Editor.Assets`,
`Oxygen.Editor.Data` (the per-user layout) and Aura's dialog service. It never references `Oxygen.Editor.SceneInspector` or
the engine runtime: its viewport comes from `ViewportServices`.

## Tests

`tests/` holds the view models' unit tests; `tests/UI` hosts the views in the
shared WinUI test host.

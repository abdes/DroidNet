# Oxygen.Editor.ModelEditor

The model family's UI: the **Import model** dialog and its import settings
panel. The Content Explorer opens the dialog through `IModelImportPrompt`, a port
of `Oxygen.Editor.Models`, and never references this project.

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

`Oxygen.Editor.Models`, `Oxygen.Editor.ContentPipeline`, `Oxygen.Editor.Controls`,
`Oxygen.Editor.Pickers`, `Oxygen.Editor.Assets` and Aura's dialog service. It
never references `Oxygen.Editor.SceneInspector`.

## Tests

`tests/` holds the view models' unit tests; `tests/UI` hosts the views in the
shared WinUI test host.

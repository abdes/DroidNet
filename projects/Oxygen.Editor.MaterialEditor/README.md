# Oxygen.Editor.MaterialEditor

The Material Editor's view and view models. The material model, its document
kind, edits and validation are in `Oxygen.Editor.Materials`.

The editor edits a material's scalar properties and native texture channel
bindings through `Oxygen.Editor.Materials`.
Texture assignments participate in document save, dirty state and undo/redo;
channel UV bindings and unexposed descriptor fields are preserved. Material
graphs, custom shaders, and rendered preview remain outside this project scope.

## Tests

`tests/` holds the editor's unit tests; `tests/UI/` holds the tests of its view,
which run in the shared WinUI test host (`Oxygen.Editor.MaterialEditor.UI.Tests`).

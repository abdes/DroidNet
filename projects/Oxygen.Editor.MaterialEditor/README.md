# Oxygen.Editor.MaterialEditor

Material authoring UI and document services for Oxygen editor V0.1.

The editor edits `oxygen.material.v1` scalar properties and native texture
channel bindings through the existing `Oxygen.Managed.Assets` material model.
Texture assignments participate in document save, dirty state and undo/redo;
channel UV bindings and unexposed descriptor fields are preserved. Material
graphs, custom shaders, and rendered preview remain outside this project scope.

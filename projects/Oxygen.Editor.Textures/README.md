# Oxygen.Editor.Textures

The texture family module: what is true of the editor's textures whatever shows
them. It holds no view, view model or dialog.

- `Texture` is the model over the descriptor format `TextureSource`
  (`Oxygen.Managed.Assets`): whether it cooks to a cube, whether it keeps HDR
  texels, and whether its images exist. The Material Editor and the
  environment's cubemap picker read it.
- `TextureImport` imports an image as a project texture: it resolves the target
  in an authoring folder, copies the image (or the six faces of a cube) and
  writes the descriptor beside it. It owns the cube layouts, their detection
  from an image's size, and the intents, color spaces and formats an import
  offers; the face names are the format's (`TextureSource.CubeFaceSuffixSets`),
  which the cooker uses to find a six-face cube's faces. A refused import throws
  `TextureImportException` with a `TextureImportCodes` code. The Content
  Explorer's import dialog calls it.

Both read and write files through `IFileSystem`, so their tests run on an
in-memory file system.

The cook only recognizes the descriptor suffix, so this module depends on
Projects and the shared asset formats only. The Texture Editor's document kind
comes with that editor.

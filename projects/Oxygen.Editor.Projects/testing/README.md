# Oxygen.Editor.Projects.Testing

The test kit of `Oxygen.Editor.Projects`: what other modules' tests use to make a
project, instead of building `ProjectContext` by hand. Only test projects
reference it.

- `TestProject.Create(fileSystem)` makes a project whose root and authoring mount
  folders exist on the given file system. Unit tests pass Testably's in-memory
  `MockFileSystem`; integration tests that need real files pass a
  `RealFileSystem` and a temporary root. The file system is the project's
  (`Context.FileSystem`), so code under test reads the project's files through
  it.
- `Create(fileSystem, root, mounts)` takes `ProjectMount`s for a mount
  whose folder differs from its name.
- `WriteFile` and `PathOf` place and find files by virtual path
  (`/Content/Textures/Wood.png`) through the project's own mapping.
- `SaveProjectFile(context)` writes the project file (`Project.oxy`) code that
  loads the project reads; `InfoOf(context)` is that saved form, for a fake
  project manager or a mount change.
- `ManifestJson(mounts, schemaVersion, outputFolderView)` is the text of a
  `Project.oxy`, written by `ProjectInfo.ToJson`, for a test that validates or
  loads a project file; a test of a malformed file keeps its own text.
- `BindMounts(fileSystem, projectRoot, (name, location)...)` writes the
  project's `.mounts.json`, binding mounts to folders or pak files on this
  machine. It is the one test writer of that file.
- `Open(info, fileSystem)` opens a saved project, as the editor opens it, for a
  test that loads its project through the project manager.
- `TestSession(context)` is a project session for one project, which services
  of the session receive; `ChangeContext(next)` gives it new mounts, as a mount
  change does, and `End()` cancels its lifetime, as closing the session does.
- `TestSessionHost` opens test sessions one at a time and changes the open
  one's mounts as the editor's host does, refusing another project.

A change to `ProjectContext` is made here once, not in each test.

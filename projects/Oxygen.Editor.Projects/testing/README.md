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
- `Create(fileSystem, root, mounts)` takes `ProjectMountPoint`s for a mount
  whose folder differs from its name.
- `WriteFile` and `PathOf` place and find files by virtual path
  (`/Content/Textures/Wood.png`) through the project's own mapping.
- `SaveProjectFile(context)` writes the project file (`Project.oxy`) code that
  loads the project reads; `InfoOf(context)` is that saved form, for a fake
  project manager or a mount change.
- `Open(info, fileSystem)` opens a saved project, as the editor opens it, for a
  test that loads its project through the project manager.

A change to `ProjectContext` is made here once, not in each test.

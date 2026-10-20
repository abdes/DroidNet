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
- `WriteFile` and `PathOf` place and find files by virtual path
  (`/Content/Textures/Wood.png`) through the project's own mapping.

A change to `ProjectContext` is made here once, not in each test.

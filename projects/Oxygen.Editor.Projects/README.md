# Oxygen.Editor.Projects

What an Oxygen project is to the editor: its manifest and settings, its files
and mounts, how it is created, validated and loaded, and the contracts of an
open project, the project session. It knows no document kinds, editors or
cooking; those modules read the project through it.

## Components

| Component                   | Holds                                                                           |
| --------------------------- | ------------------------------------------------------------------------------- |
| `IProjectSession`           | An open project: its `Context`, `Settings`, loaded `Project` and `Lifetime`     |
| `IProjectSessionHost`       | Opens, changes the mounts of and closes the editor's one session                |
| `IProjectManagerService`    | Loads, stages, accepts and saves projects (`Project.oxy` and settings)          |
| `ProjectContext`            | A project as its mounts are now, with the `FileSystem` that holds its files     |
| `ProjectSettings`           | Settings saved in the manifest and shared by everyone, such as cook on change   |
| `ProjectAssetFiles`         | The one mapping between the project's virtual paths and its files               |
| `ProjectOutputFolders`      | Output folders by rule (any dot-named folder); `.cooked`, `.build`, `.pipeline` |
| `CookedContentOrdering`     | The priority of the project's output and its libraries                          |
| `IProjectCookScopeProvider` | A project's cook scope: its root and cooked output root                         |
| `IAuthoringTargetResolver`  | Where a new asset of a family is created                                        |
| `AuthoredAssetTemplate`     | Folder, default name, suffixes and content of a family's new files              |
| `IProjectCreationService`   | Creates a project from a template                                               |
| `IProjectValidationService` | Classifies a project folder before it opens                                     |
| `IRecentProjectAdapter`     | Recent projects with their validation state                                     |

`ProjectsModule` registers the services in the editor scope. The session host is
the composition root's, because it composes the session's scope.

## Project session

`IProjectSession` is an open project. Every service, window and panel of the
project receives it, reads `Context` when it acts, and runs long work under
`Lifetime`, which is cancelled when the session starts to close.

- The project stays the same for the whole session: the same `ProjectId` and
  root. Opening another project replaces the session.
- A change of the content mounts (`IProjectSessionHost.ChangeContext`) gives a
  new `Context` and raises `ContextChanged`; only services whose state comes
  from the mounts follow it.
- `UpdateSettings` replaces the settings once they are saved and raises
  `SettingsChanged`.
- `IProjectManagerService` holds no open project.

## Files and mounts

Code reads a project's files through `ProjectContext.FileSystem`, so it takes no
file system of its own. The application gives an opened project the disk; tests
make projects on any file system.

- The project's root folder is the `project` mount, so every file of the project
  has a virtual path: `/project/...` at any depth.
- A declared mount (an authoring mount, a local folder) is a named folder. A
  file inside several mounts is named from the innermost.
- `project` and `Engine` are reserved; `ValidateDeclaredMountName` refuses them
  in any case.
- The disk is the truth for a file's existence. `ProjectAssetLocation` tells
  whether a location is a project file, a source in a source mount or a
  cooked library file. Only source mounts cook.

## Manifest

`Project.oxy` at the project's root, schema version 3:

```json
{
  "SchemaVersion": 3,
  "Id": "8f2b2b16-3a4a-4f39-9f2c-2d9f3a1b2c3d",
  "Name": "MyGame",
  "Category": "C44E7604-B265-40D8-9442-11A01ECE334C",
  "Thumbnail": "Media/Preview.png",
  "Mounts": [
    { "Name": "Content", "Kind": "Source", "Path": "Content" },
    { "Name": "Showcase", "Kind": "Source", "Path": "../../Media/Showcase" },
    { "Name": "Studio", "Kind": "Source" },
    { "Name": "Props", "Kind": "Cooked", "Path": "../Paks/Props.pak" }
  ]
}
```

| Field                | Meaning                                         |
| -------------------- | ----------------------------------------------- |
| `Id`                 | Non-empty GUID, the project's identity          |
| `Name`, `Category`   | Display name and category                       |
| `Thumbnail`          | Optional image, relative to the root            |
| `Mounts`             | Source folders and cooked libraries (see below) |
| `CookedContentOrder` | Optional library priority, lowest first         |
| `DefaultSceneId`     | The scene the workspace opens first             |
| `Settings`           | The project's settings (`ProjectSettings`)      |

`Location` and `LastUsedOn` are runtime and usage state, never saved in it.
Earlier versions are refused as unsupported, and a kind that is neither `Source`
nor `Cooked` as `mount.kind_unknown`.

## Mounts

Each mount has a name, a kind (`Source` or `Cooked`) and a path
relative to the project root, which may leave it, or no path: a placeholder.
`Project.oxy` never holds an absolute path (`path.absolute_committed`). At
least one `Source` mount is declared.

A git-ignored `.mounts.json` binds mounts on one machine. The project root's
file binds placeholders, overrides committed paths and adds local source
mounts; files above it, up to one marked `"root": true`, only bind
placeholders and add local mounts. The `Oxygen.Editor.Projects.Mounts`
namespace resolves this as the engine does:

```csharp
var table = ProjectMounts.Resolve(fileSystem, projectRoot, info.Mounts);
```

A `Cooked` mount's binding decides how it is read (`ResolvedMount.Container`): a
folder with a loose cooked index, a `.pak` file, or a library project's `.cooked`
folder, whose `head.json` selects the generation of the mount's root.
`ResolvedMount.Location` is what is read, the generation for a `.cooked` binding,
and `BoundTo` what the mount names. Every bound cooked mount is checked when the
table resolves, from its `LibraryFacts`: its one root is the mount's name
(`mount.cooked_root_mismatch`, `mount.cooked_root_multiple`), a loose root holds
no script with an external source (`mount.cooked_external_script`), what it
requires lies below it (`mount.cooked_dependency_missing`; the engine's content
lies below every mount), and this build can read it
(`mount.cooked_format_unsupported`). A mount with a finding is `Invalid`: it stays
in the table with its `Findings`, and contributes no assets. The exception is a
loose library that lacks what it requires: it stays `Bound` with the finding, and
a load of what it names fails, as the engine mounts it. `LibraryRead` reads the same
facts of a library that is not mounted yet.

`ProjectContext.Mounts` is the resolved table: each mount's location, role
(`Own` for a source folder inside the project, else `Shared`), where it came from
and its state (`Bound`, `Unbound`, `Missing`, `Invalid`).
`ProjectContext.SourceMounts` lists the bound source mounts, `LibraryMounts` the
bound and valid cooked mounts, and `CookedMounts` every cooked mount. Opening
never fails on an unbound, missing or invalid mount: they are warnings
(`mount.unbound`, `mount.folder_missing`, `mount.cooked_*`). Names, overlapping
folders and source folders in output folders are errors. The engine's `MountTableCases.json`, installed under
`conformance/`, is run by the tests of both readers.

## Opening a project

```csharp
var info = await projects.LoadProjectInfoAsync(@"C:\Projects\MyGame");
var staged = await projects.StageProjectLoadAsync(info!);
var project = projects.AcceptProjectLoad(staged!);
```

The composition root's `ProjectActivationCoordinator` stages and accepts the
load, then asks the session host to open the session.
Scenes and other documents open through their document kinds, in their own
modules.

## Dependencies

- `DroidNet.Composition` for the scope layers, `Storage` for files.
- `Oxygen.Managed.Assets` for asset paths and mount records.
- `Oxygen.Editor.Data` for recent usage.
- `Oxygen.Editor.World` for the template's initial scene identity.

## Testing

`Oxygen.Editor.Projects.Testing` is the test kit other modules use to make
projects and sessions: [testing/README.md](testing/README.md).

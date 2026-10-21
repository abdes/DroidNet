# Oxygen.Editor.Projects

What an Oxygen project is to the editor: its manifest and settings, its files
and mounts, how it is created, validated and loaded, and the contracts of an
open project, the project session. It knows no document kinds, editors or
cooking; those modules read the project through it.

## Components

| Component                   | Holds                                                                         |
| --------------------------- | ----------------------------------------------------------------------------- |
| `IProjectSession`           | An open project: its `Context`, `Settings`, loaded `Project` and `Lifetime`   |
| `IProjectSessionHost`       | Opens, changes the mounts of and closes the editor's one session              |
| `IProjectManagerService`    | Loads, stages, accepts and saves projects (`Project.oxy` and settings)        |
| `ProjectContext`            | A project as its mounts are now, with the `FileSystem` that holds its files   |
| `ProjectSettings`           | Settings saved in the manifest and shared by everyone, such as cook on change |
| `ProjectAssetFiles`         | The one mapping between the project's virtual paths and its files             |
| `ProjectOutputFolders`      | The pipeline's output folders inside a project (`.cooked`, `.imported`, ...)  |
| `CookedContentOrdering`     | The priority of the project's output and its libraries                        |
| `IProjectCookScopeProvider` | A project's cook scope: its root and cooked output root                       |
| `IAuthoringTargetResolver`  | Where a new asset of a family is created                                      |
| `AuthoredAssetTemplate`     | Folder, default name, suffixes and content of a family's new files            |
| `IProjectCreationService`   | Creates a project from a template                                             |
| `IProjectValidationService` | Classifies a project folder before it opens                                   |
| `IRecentProjectAdapter`     | Recent projects with their validation state                                   |

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
  whether a location is a project file, a source in an authoring mount or a
  library file. Only authoring mounts cook.

## Manifest

`Project.oxy` at the project's root:

```json
{
  "Id": "8f2b2b16-3a4a-4f39-9f2c-2d9f3a1b2c3d",
  "Name": "MyGame",
  "Category": "C44E7604-B265-40D8-9442-11A01ECE334C",
  "Thumbnail": "Media/Preview.png",
  "AuthoringMounts": [{ "Name": "Content", "RelativePath": "Content" }]
}
```

| Field                | Meaning                                                   |
| -------------------- | --------------------------------------------------------- |
| `Id`                 | Non-empty GUID, the project's identity                    |
| `Name`, `Category`   | Display name and category                                 |
| `Thumbnail`          | Optional image, relative to the root                      |
| `AuthoringMounts`    | Source folders (`Name` + project-relative `RelativePath`) |
| `LocalFolderMounts`  | Optional cooked library folders (`Name` + `AbsolutePath`) |
| `PakFileMounts`      | Optional `.pak` files mounted read-only as libraries      |
| `CookedContentOrder` | Optional library priority, lowest first                   |
| `DefaultSceneId`     | The scene the workspace opens first                       |
| `Settings`           | The project's settings (`ProjectSettings`)                |

Without `AuthoringMounts`, the loader uses one mount, `Content -> Content`.
`Location` and `LastUsedOn` are runtime and usage state, never saved in it.

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

# Project Layout And Templates LLD

Status: `accepted`

Last Updated: 2026-10-30

## 1. Purpose

This LLD defines the V0.1 on-disk layout for Oxygen Editor projects, the mount
rules that turn project files into asset identities, and the exact output
requirements for predefined project templates.

It is the source of truth for created project filesystem layout. Other LLDs
consume these rules; they must not define their own competing folder policy.

This LLD owns:

- standard project folder layout.
- authored asset locations for scenes, materials, geometry, textures, audio,
  video, scripts, prefabs, animations, and source media.
- `Project.oxy` layout fields related to mounts and template output.
- predefined template descriptor shape and required template payloads.
- Content Explorer root positioning rules.
- output folder rules.

This LLD does not own:

- project-service API signatures; see `project-services.md`.
- scene serialization DTO internals; see `scene-authoring-model.md`.
- asset catalog query primitives; see `asset-primitives.md`.
- browser row state reduction; see `content-explorer-asset-identity.md`.
- import/cook execution; see `content-pipeline.md`.
- material editor UI; see `material-editor.md`.

## 2. PRD Traceability

| Requirement | Coverage                                                                                                  |
| ----------- | --------------------------------------------------------------------------------------------------------- |
| `GOAL-005`  | Source content, descriptors, cooked output, and mount state have a predictable user-facing layout.        |
| `REQ-015`   | Procedural descriptors have a stable authored-content location.                                           |
| `REQ-016`   | Scoped source import has an explicit source-media position.                                               |
| `REQ-017`   | Project content roots and authoring mounts define asset identity.                                         |
| `REQ-018`   | Cooked output roots are separated from authoring data.                                                    |
| `REQ-019`   | Cooked output is discoverable for refresh/mount without becoming an authoring root.                       |
| `REQ-021`   | Asset references preserve authored identity through mount tokens.                                         |
| `REQ-024`   | Layout and mount failures can identify authoring-root, template, cooked-output, or browser-mapping cause. |
| `REQ-036`   | Content Explorer navigation and creation targets are predictable.                                         |

## 3. Architecture Links

- `ARCHITECTURE.md`: game-project filesystem contract, asset identity, content
  workflow, and save/cook/mount phase separation.
- `DESIGN.md`: save, descriptor generation, cook, catalog refresh, and mount
  refresh are distinct phases.
- `PROJECT-LAYOUT.md`: repository source-module placement rules. This LLD does
  not use `PROJECT-LAYOUT.md` for user-created project folders.
- [Oxygen rules](../../oxygen/RULES.md): cooked output is derived and not edited
  as authoring data; asset references preserve authoring intent.
- `project-services.md`: project manifest services, active project context,
  its resolved mount table, and cook-scope facts.
- `content-explorer-asset-identity.md`: browser projection of source, descriptor,
  cooked, missing, and broken states.
- `content-pipeline.md`: import/cook output paths and cooked index policy.

## 4. Project Layout Contract

The layout below is the V0.1 project contract. New predefined templates, new
project creation, new authored asset creation, and Content Explorer defaults must
follow it.

A folder is authored content when it is in a bound `Source` mount of the
project's [mount table](#resolved-mount-table), inside or outside the project
folder. No other folder is, whatever it holds.

Project configuration and derived output are explicitly not authored asset
roots.

## 5. Target Design

### Standard Project Layout

Every newly created V0.1 project has this shape:

```text
<ProjectRoot>/
  Project.oxy

  Content/                         # default authored content mount: asset:///Content/...
    Scenes/                        # authored editor scenes: *.oscene.json
    Materials/                     # material descriptors: *.omat.json
    Geometry/                      # geometry descriptors and imported geometry assets
    Textures/                      # texture descriptors and imported texture assets
    Audio/                         # audio assets and descriptors
    Video/                         # video/media assets and descriptors
    Scripts/                       # gameplay scripts, including Lua
    Prefabs/                       # reusable entity/scene fragments
    Animations/                    # animation assets and descriptors
    SourceMedia/                   # raw source files not yet imported as runtime assets
      Images/
      Audio/
      Video/
      DCC/

  Config/                          # project/game/editor configuration, not asset content
  Packages/                        # package/plugin payloads when present
  Imports/                         # model import records, committed: <virtual path>.import.json

  .cooked/                         # output: the editor's publication model
  .build/                          # output: the editor's working files
```

Folder requirements:

| Folder                        | Required in template payload                | Created or verified by project creation | Auto-created on first asset create                         | Browser group         | Default creation target |
| ----------------------------- | ------------------------------------------- | --------------------------------------- | ---------------------------------------------------------- | --------------------- | ----------------------- |
| `Project.oxy`                 | No; prohibited in predefined `SourceFolder` | Project creation writes final file      | No                                                         | Project metadata      | No                      |
| `Content/`                    | Yes                                         | Verified after payload copy             | No                                                         | Authoring mount       | Yes                     |
| `Content/Scenes/`             | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Scene                   |
| `Content/Materials/`          | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Material                |
| `Content/Geometry/`           | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Geometry                |
| `Content/Textures/`           | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Texture                 |
| `Content/Audio/`              | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Audio                   |
| `Content/Video/`              | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Video                   |
| `Content/Scripts/`            | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Script                  |
| `Content/Prefabs/`            | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Prefab                  |
| `Content/Animations/`         | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Animation               |
| `Content/SourceMedia/`        | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Raw source media        |
| `Content/SourceMedia/Images/` | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Raw image source        |
| `Content/SourceMedia/Audio/`  | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Raw audio source        |
| `Content/SourceMedia/Video/`  | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Raw video source        |
| `Content/SourceMedia/DCC/`    | Yes                                         | Verified after payload copy             | Yes, with layout warning if missing after project creation | Authoring mount       | Raw DCC source          |
| `Config/`                     | Yes                                         | Verified after payload copy             | No; settings workflows create files inside it              | Project configuration | No                      |
| `Packages/`                   | No                                          | No                                      | Package workflow creates                                   | Project packages      | No                      |
| `Imports/`                    | No                                          | No                                      | Model import creates                                       | Project files         | No                      |
| `.cooked/`                    | No                                          | No                                      | Cook creates                                               | Output folder         | No                      |
| `.build/`                     | No                                          | No                                      | Cook, Play and relocation create                           | Output folder         | No                      |

Predefined template validation fails with a `ProjectTemplate` diagnostic when a
folder marked `Required in template payload` is missing. After project creation,
first-use auto-creation of a missing kind folder is allowed only as a
user-visible `ProjectContentRoots` warning that restores the required folder
before writing the new asset.

### Authoring Scene Files

Authored scene documents are content assets.

V0.1 scene authoring path:

```text
<ProjectRoot>/Content/Scenes/<SceneName>.oscene.json
asset:///Content/Scenes/<SceneName>.oscene.json
```

Rules:

- `*.oscene.json` is the editor-authored scene source document.
- `*.oscene` is runtime output inside a generation selected by
  `.cooked/head.json`.
- top-level `Scenes/` is not part of the V0.1 project layout.
- scene create, scene open, scene save, starter-scene template payloads, and
  Content Explorer scene rows use `Content/Scenes/*.oscene.json`.
- scene document JSON schema ownership remains in `scene-authoring-model.md`.

### Authoring Identity

An authored content item or asset is identified by mount token plus path within
that mount:

```text
asset:///<MountName>/<PathWithinMount>
```

Examples:

```text
asset:///Content/Scenes/Main.oscene.json
asset:///Content/Materials/Red.omat.json
asset:///Content/Geometry/Crate.ogeo.json
asset:///Content/Textures/Wall.otex.json
asset:///Content/Scripts/Player.lua
asset:///Content/SourceMedia/DCC/Crate.blend
```

Rules:

- `Content` is the default authored mount and maps to `<ProjectRoot>/Content`.
- authored scenes live under `Content/Scenes`.
- raw source media under `Content/SourceMedia` is browsable and importable but
  has source identity, not runtime asset identity. It cannot satisfy runtime
  asset pickers until imported or converted into a supported descriptor/runtime
  asset.
- scripts under `Content/Scripts` are authored game content.
- `Config` is project configuration, not asset content, and is not addressed by
  `asset:///...` references.

### Project Manifest Layout Fields

`Project.oxy` contains:

`SchemaVersion = 2` declares one `Mounts` list. The editor refuses a version 1
manifest with its unsupported-version diagnostic; such projects are migrated by
editing their data, with no reader of the old form kept:

| Version 1 `Project.oxy`                             | Version 2 `Project.oxy` and `.mounts.json`                                                                          |
| --------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| `AuthoringMounts` entry                             | A `Mounts` entry of kind `Source`, with the same name and path                                                      |
| `LocalFolderMounts` entry without a cooked index    | A `Source` placeholder of the same name, bound in `.mounts.json` to the old absolute path                           |
| `LocalFolderMounts` entry with a cooked index       | A `Cooked` placeholder, bound in `.mounts.json`                                                                     |
| `PakFileMounts` entry                               | A `Pak` entry with a relative path when the pak is under the repository, else a placeholder bound in `.mounts.json` |
| Model import record `source` relative to its folder | The source's mount path (`/Content/Models/Ship.glb`)                                                                |

| Field           | Required | Rule                                                                                   |
| --------------- | -------- | -------------------------------------------------------------------------------------- |
| `SchemaVersion` | Yes      | Must equal `2`, the supported project manifest version.                                |
| `Id`            | Yes      | New non-empty GUID generated for each created project.                                 |
| `Name`          | Yes      | User-facing project name selected at creation.                                         |
| `Category`      | Yes      | Template/project category.                                                             |
| `Thumbnail`     | No       | Project/template preview path when available.                                          |
| `Mounts`        | Yes      | The project's mounts: `Source`, `Cooked` and `Pak`. Must include `Content -> Content`. |

`Project.oxy` must not persist:

- project absolute location.
- recent usage timestamps.
- operation results or diagnostics.
- cooked output state.
- output folder state.
- an absolute path: a mount this machine binds elsewhere is bound in
  `.mounts.json`.

Project creation always writes a new non-empty GUID. Predefined templates must
not ship `Project.oxy` in their `SourceFolder`; a stray `Project.oxy` rejects
the template with a `ProjectTemplate` diagnostic. A test creates two projects
from the same template and asserts the two `Id` values differ from each other.

### Mounts

`Project.oxy` declares one `Mounts` list. Each entry is a name, a kind and an
optional path relative to the project root; an entry without a path is a
placeholder, which a `.mounts.json` binds on each machine.

```json
"Mounts": [
  { "Name": "Content", "Kind": "Source", "Path": "Content" },
  { "Name": "Showcase", "Kind": "Source", "Path": "../../Media/Showcase" },
  { "Name": "Studio", "Kind": "Source" },
  { "Name": "VendorCooked", "Kind": "Cooked" },
  { "Name": "Engine2D", "Kind": "Pak", "Path": "../Paks/engine2d.pak" }
]
```

| Kind     | Holds                                                                         |
| -------- | ----------------------------------------------------------------------------- |
| `Source` | A folder of authored sources: edited, cooked, relocated and watched           |
| `Cooked` | A cooked folder (`container.index.bin`) mounted read-only as a library        |
| `Pak`    | A `.pak` file mounted read-only as a library, beneath the project's own roots |

Rules, one validator for every place that writes mounts
(`ProjectMounts.ValidateDeclarations` and the resolved table):

- a path is relative to the project root and may leave it (`../Media`); an
  absolute path is refused with `path.absolute_committed`.
- mount names match `^[A-Za-z][A-Za-z0-9_-]{0,63}$` (`mount.name_invalid`) and
  are unique ignoring case (`mount.name_duplicate`).
- reserved names are refused ignoring case: `project` and `Engine`, the mounts
  every project has, and `Cooked` and `Build`, the Content Explorer's output
  folder nodes (`ProjectAssetFiles.IsReservedMountName`).
- the project declares a `Source` mount (`mount.source_required`); `Content`
  maps to `Content` in created projects.
- source mount folders never overlap (`mount.folder_overlap`), compared on their
  canonical folders, links resolved.
- no source mount is in an output folder (`mount.folder_in_output`); see
  [Output Folders](#output-folders).
- a shared folder is mounted under the same name by every project that uses it,
  because the references inside it carry that name.
- changing a mount name changes asset identity and requires an explicit
  project-wide reference update; binding a mount to another folder keeps its
  identities.

The engine content pak is not declared: every project mounts the SDK's
`engine.pak` beneath all other content.

### Mount Bindings

Git-ignored `.mounts.json` files bind mounts per machine with absolute paths. The
editor reads the engine's hierarchy, against the installed
`oxygen.mounts.schema.json`:

```json
{
  "schema_version": 2,
  "mounts": {
    "Studio": "D:/Studio/SharedContent",
    "VendorCooked": "E:/Vendor/cooked"
  }
}
```

- the project's own file, at the project root, binds placeholders, overrides
  committed paths and adds mounts only this machine has.
- files in the folders above the project only bind placeholders and add mounts;
  one that names a committed mount is ignored with `mount.parent_ignored`.
- a file with `"root": true` stops the walk up the folders.
- the editor writes only the project's own file, through the atomic file store,
  for Bind, Override and Remove binding (`MountBindings`); a file above the
  project belongs to the studio or the machine.
- `.gitignore` of a created project lists `.mounts.json` and the output folders.

### Resolved Mount Table

`ProjectContext.Mounts` is the resolved table: for each declared or locally
added mount, its name, kind, committed path, folder or file on this machine,
which file bound it, and its state. The merge is the engine's; a case file,
`MountTableCases.json`, is run by both the engine's and the editor's tests.

| State     | Meaning                                                            |
| --------- | ------------------------------------------------------------------ |
| `Bound`   | The committed path or a `.mounts.json` names a folder              |
| `Unbound` | A placeholder no `.mounts.json` binds (`mount.unbound`)            |
| `Missing` | The bound folder or pak file is not there (`mount.folder_missing`) |

- opening never fails for an unbound or missing mount: both are warnings, and
  the mount stays in the Content Explorer with its state and Bind or Override.
- structural errors (names, overlap, output folders, absolute committed paths)
  fail opening and every mount change.
- `ProjectAssetFiles` is the only mapper between mount paths and files:
  `Find`, `FindByFilePath`, `SourceMountPathOf` and `SourceFileOf`; no component
  combines the project root with a mount's path.
- a change of any `.mounts.json` of the hierarchy during a session resolves the
  table again; a table that differs is applied as a mount change.

Mount identity example:

```text
asset:///Studio/Characters/Hero.ogeo.json
```

### Default Creation Targets

Default create target rules are asset-kind-specific:

| User selection                                                                  | Scene target      | Material target      | Geometry target     | Script target      |
| ------------------------------------------------------------------------------- | ----------------- | -------------------- | ------------------- | ------------------ |
| project root                                                                    | `/Content/Scenes` | `/Content/Materials` | `/Content/Geometry` | `/Content/Scripts` |
| `/<Mount>` authoring root                                                       | `/<Mount>/Scenes` | `/<Mount>/Materials` | `/<Mount>/Geometry` | `/<Mount>/Scripts` |
| folder under an authoring mount and matching the asset kind                     | selected folder   | selected folder      | selected folder     | selected folder    |
| folder under an authoring mount but not matching the asset kind                 | `/<Mount>/Scenes` | `/<Mount>/Materials` | `/<Mount>/Geometry` | `/<Mount>/Scripts` |
| source mount outside the project explicitly chosen as target                    | `/<Mount>/Scenes` | `/<Mount>/Materials` | `/<Mount>/Geometry` | `/<Mount>/Scripts` |
| folder under a source mount outside the project and matching the asset kind     | selected folder   | selected folder      | selected folder     | selected folder    |
| folder under a source mount outside the project but not matching the asset kind | `/<Mount>/Scenes` | `/<Mount>/Materials` | `/<Mount>/Geometry` | `/<Mount>/Scripts` |
| an unbound or missing mount, a library, `Config`, `Packages` or output folders  | `/Content/Scenes` | `/Content/Materials` | `/Content/Geometry` | `/Content/Scripts` |

Before creation, UI presents the effective target as both a virtual folder and
resulting asset URI. Example:

```text
Target folder: /Content/Materials
Asset URI: asset:///Content/Materials/Red.omat.json
```

### Output Folders

An output folder is any folder below the project root whose name starts with a
dot. This is a rule, not a list: `.` and `..` are not folder names, and the
check runs on the canonical folder below the project root, so
`Content/../.cooked` is output and `../Media`, which leaves the project, is not.
Every classification goes through `ProjectOutputFolders.Contains`.

The editor owns a project's output folders. It writes three, each with one
definition in `ProjectOutputFolders` (a constant and a path helper) from which
every path under it is built:

| Folder      | Holds                                                                                                                                                                                                              |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `.cooked`   | The publication model: `head.json` selects one immutable publication, `publications/<publicationId>.json`, which names one cooked root per project mount, `generations/<SourceKey>/`, sealed by `.generation.lock` |
| `.build`    | Working files: `cook/<OperationId>/` (locks, journal, inputs), `cook/publication.lock`, `cook/maintenance.lock`, `import-analysis/`, `inspection/`, `cache/`, `relocation/<id>/journal.json`, `play/launch.json`   |
| `.pipeline` | Scene descriptors the pipeline generates, below a cook operation's input root in `.build/cook/<OperationId>`                                                                                                       |

Rules:

- an output folder is never a mount: neither an authoring mount nor a source
  mount passed to the Player. Project validation and every mount change refuse
  one.
- the Content Explorer can show `.cooked` and `.build` as read-only `Cooked` and
  `Build` nodes. They are view state, `OutputFolderViews` in `Project.oxy`, not
  mounts.
- nothing but the editor writes in an editor project's output folders. A tool
  cooks into a root it owns, outside the project: RenderScene cooks the
  examples project into `Examples/RenderScene/.cooked/Content`, and an
  ImportTool record-only import publishes into its `--output` generations
  folder.
- a model's published generation has one publisher in an editor project, the
  editor. A `model` job cooks into its mount's generation and returns the
  record update in its report; the editor commits the update with the
  publication. The record holds what imports established, never the selected
  generation.
- each owner collects only its own folders: the editor's maintenance reclaims
  GUID-named generations, publications and operations, nothing else.
- deleting output folders never deletes authored data; the next cook rebuilds
  them.

### Model Import Records

A model's import record is at `<ProjectRoot>/Imports/<virtual path>.import.json`
(`/Content/Models/Ship.glb` has `Imports/Content/Models/Ship.glb.import.json`).
Records are committed state, not output. A record names its source by mount
path (`"source": "/Content/Models/Ship.glb"`), the same in every project that
mounts the folder; the editor refuses another form. `Imports/` lies outside
every authoring mount by convention: a mount whose path is `.` contains it, and
cook inputs skip it by its prefix.

Cooked output mapping:

```text
source: <ProjectRoot>/Content/Materials/Gold.omat.json
cooked: <ProjectRoot>/.cooked/generations/<SourceKey>/Materials/Gold.omat
index:  <ProjectRoot>/.cooked/generations/<SourceKey>/container.index.bin

source: <ProjectRoot>/Content/Scenes/Main.oscene.json
cooked: <ProjectRoot>/.cooked/generations/<SourceKey>/Scenes/Main.oscene
index:  <ProjectRoot>/.cooked/generations/<SourceKey>/container.index.bin
```

Rules:

- `.cooked/generations/<SourceKey>/container.index.bin` is the loose cooked
  index for that mount.
- `.cooked/generations/<SourceKey>/...` paths are display/diagnostic facts, not
  persisted authored identities.

### Browser Positioning

Content Explorer presents project layout as identity-aware content, not as an
undifferentiated file explorer.

Top-level browser groups:

```text
Project
  Content                         authored mount
  <ExtraSourceMount>               authored mount, inside or outside the project
  <LibraryMount>                   cooked folder or pak, read-only
  <UnavailableMount>               unbound or missing: dimmed, with its state
  Config                           project configuration
  Packages                         project packages, when present
  Cooked                           .cooked, when shown (view state)
  Build                            .build, when shown (view state)
```

Required labels:

- authoring mounts: `AUTH`; one a `.mounts.json` binds also shows its folder
  and a `This machine` badge
- unbound and missing mounts: their state, `Not bound` or `Folder missing`
- configuration: `CONFIG`
- packages: `PKG`
- cooked output: `COOKED`
- imported/intermediate output: `DERIVED`
- build output: `BUILD`

Raw source media rows under `Content/SourceMedia` use their source kind, such as
`AssetKind.Model` for a model or `AssetKind.Image` for an image. They never use
`AssetState.Cooked` unless the row represents a cooked artifact from `.cooked`.

## 6. Ownership

The table lists positive ownership. Forbidden ownership edges are defined in
[Dependency Rules](#13-dependency-rules); consumers must not infer authoring
roots or target folders outside the project layout policy.

| Owner                           | Responsibility                                                                                 |
| ------------------------------- | ---------------------------------------------------------------------------------------------- |
| `Oxygen.Editor.Projects`        | `Project.oxy`, mount validation, project creation layout validation, project cook-scope facts. |
| `Oxygen.Editor.ProjectExplorer` | predefined template presentation and create-from-template workflow.                            |
| `Oxygen.Editor.ContentExplorer` | user-facing projection of mounts, their states and bindings, config, packages, output folders. |
| `Oxygen.Editor.WorldEditor`     | scene document create/open/save under `Content/Scenes`.                                        |
| `Oxygen.Editor.MaterialEditor`  | material descriptor create/open/save under `Content/Materials` or selected material target.    |
| `Oxygen.Editor.ContentPipeline` | import/cook outputs under output folders and cooked indexes.                                   |
| `Oxygen.Managed.Assets`         | reusable asset URI, catalog, import, and cook primitives.                                      |

## 7. Data Contracts

### Template Descriptor

Each predefined template has a `Template.json` with this shape:

```json
{
  "SchemaVersion": 1,
  "TemplateId": "Games/Blank",
  "Category": "Games",
  "DisplayName": "Blank",
  "Description": "Empty gameplay project.",
  "Icon": "Media/Icon.png",
  "Preview": "Media/Preview.png",
  "SourceFolder": ".",
  "Mounts": [{ "Name": "Content", "Kind": "Source", "Path": "Content" }],
  "StarterScene": {
    "AssetUri": "asset:///Content/Scenes/Main.oscene.json",
    "RelativePath": "Content/Scenes/Main.oscene.json",
    "OpenOnCreate": true
  },
  "StarterContent": [
    {
      "AssetUri": "asset:///Content/Materials/Default.omat.json",
      "RelativePath": "Content/Materials/Default.omat.json",
      "Kind": "Material"
    }
  ]
}
```

Field rules:

- `SchemaVersion` is required and equals `1`.
- `TemplateId` is required and unique across predefined templates.
- `Category` is required and maps to the project category enum.
- `DisplayName` is required and non-empty.
- `Description` is required and non-empty.
- `Icon` is required and project-template-relative.
- `Preview` is required for predefined built-in V0.1 templates and
  project-template-relative.
- `SourceFolder` is required and template-root-relative.
- predefined built-in template `SourceFolder` must not contain `Project.oxy`.
- `Mounts` is required, follows the mount rules above, and commits no absolute
  path.
- `StarterScene` is required for built-in V0.1 templates.
- every `StarterContent` entry must be under an authoring mount.

Template metadata is not copied into `Project.oxy` as project identity.

### Predefined Templates

| TemplateId            | Category        | DisplayName    | Required media                        | Required payload                                                                                            | Starter scene                                     | OpenOnCreate |
| --------------------- | --------------- | -------------- | ------------------------------------- | ----------------------------------------------------------------------------------------------------------- | ------------------------------------------------- | ------------ |
| `Games/Blank`         | `Games`         | `Blank`        | `Media/Icon.png`, `Media/Preview.png` | required folder skeleton, `Content/Scenes/Main.oscene.json`, default material                               | `asset:///Content/Scenes/Main.oscene.json`        | Yes          |
| `Games/First Person`  | `Games`         | `First Person` | `Media/Icon.png`, `Media/Preview.png` | required folder skeleton, `Content/Scenes/FirstPerson.oscene.json`, default material, starter script folder | `asset:///Content/Scenes/FirstPerson.oscene.json` | Yes          |
| `Visualization/Blank` | `Visualization` | `Blank`        | `Media/Icon.png`, `Media/Preview.png` | required folder skeleton, `Content/Scenes/Main.oscene.json`, default material                               | `asset:///Content/Scenes/Main.oscene.json`        | Yes          |

### Created Project Manifest

After create-from-template, the project `Project.oxy` contains:

```json
{
  "SchemaVersion": 2,
  "Id": "<new-guid>",
  "Name": "<user-project-name>",
  "Category": "<template-category>",
  "Thumbnail": "Media/Preview.png",
  "Mounts": [{ "Name": "Content", "Kind": "Source", "Path": "Content" }]
}
```

`Id` is generated at creation time. `Name` is the user-selected project name.

## 8. Commands, Services, Or Adapters

### Project Creation

Create-from-template performs:

1. Validate `Template.json`.
2. Verify target folder does not exist or exists and has no filesystem entries.
3. Verify `SourceFolder` does not contain `Project.oxy`; reject the template if
   it does.
4. Copy template payload except `Template.json` to the target project folder.
5. Verify the required folder skeleton exists.
6. Generate a new non-empty project `Id`.
7. Write `Project.oxy` with the new id, user-selected name, category, thumbnail
   and `Mounts`, and `.gitignore` with `.mounts.json` and the output folders.
8. Validate the created project manifest and mounts.
9. Activate the project through the normal project activation path.
10. If `StarterScene.OpenOnCreate` is true, request opening that scene document.

When `StarterScene` is absent or `OpenOnCreate` is false, project activation
completes with an empty workspace; no scene document is opened and no diagnostic
is emitted. Predefined built-in V0.1 templates require `StarterScene` and use
`OpenOnCreate = true`.

Accepted empty project target:

- folder does not exist; or
- folder exists and contains zero files and zero child directories.

Any other target folder state is rejected before template copy starts.

Project creation fails visibly if:

- template descriptor is missing or invalid.
- target folder is not an accepted empty project target.
- template copy fails.
- required standard folders cannot be created.
- `Project.oxy` cannot be rewritten.
- generated `Id` is empty.
- required `Content` mount is missing or invalid.
- starter scene file is missing or outside an authoring mount.

### Authoring Target Resolver

`Oxygen.Editor.Projects` owns the project-layout resolver contract. Feature
projects must call this service instead of duplicating folder rules.

Contract:

```csharp
public interface IAuthoringTargetResolver
{
    AuthoringTarget ResolveCreateTarget(
        ProjectContext project,
        AssetKind assetKind,
        ContentFolderSelection? selection);
}

public sealed record AuthoringTarget(
    string MountName,
    AssetPath Folder,
    bool IsExplicitLocalMount,
    AuthoringTargetFallbackReason? FallbackReason);
```

Rules:

- `ProjectContext` supplies the active project root and its bound source mounts;
  an unbound or missing mount is never a target.
- `AssetKind` maps to the kind folders in the default creation target table.
- `ContentFolderSelection` is optional and contains the selected root/folder
  identity from Content Explorer, never a raw cooked path.
- `ResolveCreateTarget` applies the matrix in
  [Default Creation Targets](#default-creation-targets).
- `FallbackReason` is set when the resolver ignores a selected folder because it
  is not an authored target for the requested kind.

Required consumers:

- WorldEditor scene create.
- MaterialEditor material create.
- Content Explorer create-from-context actions.
- Future geometry/script/prefab/animation creation/import surfaces.

## 9. UI Surfaces

Project Explorer:

- templates show `DisplayName`, `Description`, `Icon`, `Preview`, and category.
- template detail shows starter scene and mount layout.
- create-project errors name the template field, folder path, or mount token
  that failed.

Content Explorer:

- mounts, config, packages, and output folders use the labels defined in Browser
  Positioning; unbound and missing mounts stay with their state, and their menu
  binds or overrides them.
- create actions show target folder and resulting asset URI before confirming.
- raw source media rows are importable/source rows.
- cooked rows are derived output rows and are not editable as source.

## 10. Persistence And Round Trip

New project from template must round-trip:

- unique project id.
- user-selected project name.
- category.
- optional thumbnail.
- standard `Content` source mount, and no absolute path.
- required folder skeleton.
- starter scene identity.
- starter content identities.

Scene and asset persistence stores authored identities, not absolute paths or
cooked paths, whenever a mount identity exists.

## 11. Live Sync / Cook / Runtime Behavior

Project layout does not perform live sync, cook, or runtime mount.

It provides policy consumed by those systems:

- content pipeline maps authored identities to descriptors, manifests, and
  cooked output.
- runtime integration mounts validated cooked roots.
- live sync uses authored component semantics and asset identities; it does not
  infer authoring identity from cooked paths.

## 12. Operation Results And Diagnostics

Layout-related failures use existing project/content domains:

| Condition                                 | Domain                | Required diagnostic detail                                    |
| ----------------------------------------- | --------------------- | ------------------------------------------------------------- |
| missing or invalid `Project.oxy`          | `ProjectValidation`   | manifest path and parse/validation reason                     |
| unbound or missing mount (warning)        | `ProjectContentRoots` | `mount.unbound` or `mount.folder_missing`, mount name, folder |
| invalid mount declaration or binding      | `ProjectContentRoots` | the mount table's code, mount name, file                      |
| invalid template metadata                 | `ProjectTemplate`     | template path and field name                                  |
| template copy or manifest rewrite failure | `ProjectTemplate`     | source path, target path, exception type                      |
| starter scene missing/outside mount       | `ProjectTemplate`     | starter scene URI and relative path                           |
| derived cooked index invalid              | `ContentPipeline`     | mount name and index path                                     |
| browser cannot map selection to a mount   | `AssetIdentity`       | selection path and active mount list                          |

## 13. Dependency Rules

- `Oxygen.Editor.Projects` owns project layout policy.
- `Oxygen.Editor.ContentExplorer` consumes project layout policy; it must not
  invent authoring roots from arbitrary project-root folders.
- `Oxygen.Editor.ContentPipeline` consumes mount/cook-scope policy; it must not
  define project templates.
- `Oxygen.Editor.Runtime` consumes cooked-root mount policy; it must not own
  authoring layout.
- `Oxygen.Editor.Interop` must not know project layout policy.
- `Oxygen.Managed.Assets` owns reusable URI/catalog primitives but must not own
  editor project-template workflow.

## 14. Validation Gates

Project layout/template work is complete when these checks pass:

1. Creating each predefined template writes a `Project.oxy` of version 2 with a
   non-empty unique GUID, user-selected name, category and `Content` source
   mount, and a `.gitignore` that lists `.mounts.json`.
2. Creating two projects from the same template produces two different project
   ids. Predefined templates do not ship `Project.oxy`.
3. Built-in template validation fails when `SourceFolder` contains
   `Project.oxy`.
4. Creating each predefined template creates every folder marked required in the
   folder requirements table.
5. Built-in template validation fails when `Template.json` omits `TemplateId`,
   `Category`, `DisplayName`, `Description`, `Icon`, `Preview`, `SourceFolder`,
   `Mounts`, or `StarterScene`.
6. Built-in template validation fails when required `Icon` or `Preview` media
   paths do not exist in the template payload.
7. Mount validation rejects absolute committed paths, duplicate and reserved
   mount names, overlapping source folders, output-folder mount paths and a
   project without a source mount, each with its code; the engine's and the
   editor's readers pass the shared `MountTableCases.json`.
8. A project opens with an unbound and a missing mount, each reported as a
   warning; a `.mounts.json` change during a session applies the new table.
9. Scene create resolves to `Content/Scenes/<Name>.oscene.json` and
   `asset:///Content/Scenes/<Name>.oscene.json`.
10. Material create at project root resolves to
    `Content/Materials/<Name>.omat.json` and
    `asset:///Content/Materials/<Name>.omat.json`.
11. Create actions under an extra authoring mount resolve to that mount's
    asset-kind folder and URI.
12. Create actions under a source mount outside the project use that mount;
    an unbound or missing mount resolves to the default `Content` asset-kind
    folder.
13. Content Explorer shows mounts with their states and bindings, config and
    packages roots, and the optional Cooked and Build nodes, with the required
    labels.
14. `Content/SourceMedia` rows reduce to source/importable state, not cooked
    state.
15. The selected generation's `container.index.bin` is displayed as derived
    output and is never persisted as an authored asset identity.

## 15. Open Issues

None.

## 16. Reproduction And Publication Files

Output folders are never authored mounts or creation targets. Project
version-control metadata excludes them (`.cooked/`, `.build/`) while retaining
Project.oxy, `Imports/`, Content/SourceMedia, authored descriptors and Config.
`.mounts.json` is ignored too: a mount it binds is a dependency each machine
binds itself, and the release fixture uses committed relative paths.

Input snapshots and source-recovery files live beneath
`.build/cook/<OperationId>`. Native output is cooked directly into fresh
`.cooked/generations/<SourceKey>` roots; one atomic head selects an immutable
publication. The
[content-pipeline transaction](content-pipeline.md#16-saved-inputs-and-publication-transaction)
owns layout, recovery and automatic reclamation. Interrupted transactions
recover before project validation and mounting. ED-M08 evidence lives under its
explicit owned `EvidenceRoot`, defaulting to repository
`artifacts/ed-m08/runs/<operation-id>/`; it is not inferred from the project
layout. The
[standalone qualification contract](standalone-runtime-validation.md#5-ownership-build-isolation-and-dependency-direction)
owns development build staging, evidence containment and protocol manifests.
Templates ship neither cooked products nor qualification artifacts. Ordinary
native compatibility checks each capability's actual dependencies and producer
proofs; the opt-in qualification runner uses a separate matched private
runtime/Interop/capture bundle. Neither is an editable project renderer preset
or a whole-editor qualification requirement for normal startup.

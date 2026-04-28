# Project Layout And Templates LLD

Status: `accepted`

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
- Content Browser root positioning rules.
- derived/output root positioning rules.

This LLD does not own:

- project-service API signatures; see `project-services.md`.
- scene serialization DTO internals; see `scene-authoring-model.md`.
- asset catalog query primitives; see `asset-primitives.md`.
- browser row state reduction; see `content-browser-asset-identity.md`.
- import/cook execution; see `content-pipeline.md`.
- material editor UI; see `material-editor.md`.

## 2. PRD Traceability

| Requirement | Coverage |
| --- | --- |
| `GOAL-005` | Source content, descriptors, cooked output, and mount state have a predictable user-facing layout. |
| `REQ-015` | Procedural descriptors have a stable authored-content location. |
| `REQ-016` | Scoped source import has an explicit source-media position. |
| `REQ-017` | Project content roots and authoring mounts define asset identity. |
| `REQ-018` | Cooked output roots are separated from authoring data. |
| `REQ-019` | Cooked output is discoverable for refresh/mount without becoming an authoring root. |
| `REQ-021` | Asset references preserve authored identity through mount tokens. |
| `REQ-024` | Layout and mount failures can identify authoring-root, template, cooked-output, or browser-mapping cause. |
| `REQ-036` | Content Browser navigation and creation targets are predictable. |

## 3. Architecture Links

- `ARCHITECTURE.md`: game-project filesystem contract, asset identity,
  content workflow, and save/cook/mount phase separation.
- `DESIGN.md`: save, descriptor generation, cook, catalog refresh, and mount
  refresh are distinct phases.
- `PROJECT-LAYOUT.md`: repository source-module placement rules. This LLD does
  not use `PROJECT-LAYOUT.md` for user-created project folders.
- `RULES.md`: cooked output is derived and not edited as authoring data; asset
  references preserve authoring intent.
- `project-services.md`: project manifest services, active project context,
  authoring roots, local folder mounts, and cook-scope facts.
- `content-browser-asset-identity.md`: browser projection of source,
  descriptor, cooked, missing, and broken states.
- `content-pipeline.md`: import/cook output paths and cooked index policy.

## 4. Project Layout Contract

The layout below is the V0.1 project contract. New predefined templates, new
project creation, new authored asset creation, and Content Browser defaults
must follow it.

A folder under the project root is not authored content unless it is:

- under a declared `AuthoringMounts` entry.
- under a declared `LocalFolderMounts` entry.
- classified by this LLD as project configuration or derived output.

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

  .cooked/                         # derived cooked output, never edited as source
  .imported/                       # import intermediate/cache output
  .build/                          # build/package output
  .oxygen/                         # editor-local project metadata/cache
```

Folder requirements:

| Folder | Required in template payload | Created or verified by project creation | Auto-created on first asset create | Browser group | Default creation target |
| --- | --- | --- | --- | --- | --- |
| `Project.oxy` | No; prohibited in predefined `SourceFolder` | Project creation writes final file | No | Project metadata | No |
| `Content/` | Yes | Verified after payload copy | No | Authoring mount | Yes |
| `Content/Scenes/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Scene |
| `Content/Materials/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Material |
| `Content/Geometry/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Geometry |
| `Content/Textures/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Texture |
| `Content/Audio/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Audio |
| `Content/Video/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Video |
| `Content/Scripts/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Script |
| `Content/Prefabs/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Prefab |
| `Content/Animations/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Animation |
| `Content/SourceMedia/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Raw source media |
| `Content/SourceMedia/Images/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Raw image source |
| `Content/SourceMedia/Audio/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Raw audio source |
| `Content/SourceMedia/Video/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Raw video source |
| `Content/SourceMedia/DCC/` | Yes | Verified after payload copy | Yes, with layout warning if missing after project creation | Authoring mount | Raw DCC source |
| `Config/` | Yes | Verified after payload copy | No; settings workflows create files inside it | Project configuration | No |
| `Packages/` | No | No | Package workflow creates | Project packages | No |
| `.cooked/` | No | No | Cook creates | Derived output | No |
| `.imported/` | No | No | Import creates | Derived output | No |
| `.build/` | No | No | Build/package creates | Derived output | No |
| `.oxygen/` | No | No | Editor metadata workflow creates | Editor-local metadata | No |

Predefined template validation fails with a `ProjectTemplate` diagnostic when a
folder marked `Required in template payload` is missing. After project
creation, first-use auto-creation of a missing kind folder is allowed only as a
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
- `*.oscene` is the cooked runtime output produced under `.cooked/<Mount>/...`.
- top-level `Scenes/` is not part of the V0.1 project layout.
- scene create, scene open, scene save, starter-scene template payloads, and
  Content Browser scene rows use `Content/Scenes/*.oscene.json`.
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

`SchemaVersion = 1` inherits the ED-M01 V0.1 project manifest baseline. This
LLD adds the concrete layout and template rules that must be satisfied by that
manifest version.

| Field | Required | Rule |
| --- | --- | --- |
| `SchemaVersion` | Yes | Must equal the supported V0.1 project manifest version. |
| `Id` | Yes | New non-empty GUID generated for each created project. |
| `Name` | Yes | User-facing project name selected at creation. |
| `Category` | Yes | Template/project category. |
| `Thumbnail` | No | Project/template preview path when available. |
| `AuthoringMounts` | Yes | Project-relative authoring mounts. Must include `Content -> Content`. |
| `LocalFolderMounts` | No | Absolute external authored content mounts. Empty when not used. |

`Project.oxy` must not persist:

- project absolute location.
- recent usage timestamps.
- operation results or diagnostics.
- cooked output state.
- derived root state.

Project creation always writes a new non-empty GUID. Predefined templates must
not ship `Project.oxy` in their `SourceFolder`; a stray `Project.oxy` rejects
the template with a `ProjectTemplate` diagnostic. A test creates two projects
from the same template and asserts the two `Id` values differ from each other.

### Authoring Mounts

`AuthoringMounts` are project-relative authored content roots.

Shape:

```json
"AuthoringMounts": [
  { "Name": "Content", "RelativePath": "Content" },
  { "Name": "DLC01", "RelativePath": "DLC/DLC01/Content" }
]
```

Rules:

- paths are project-relative.
- rooted paths are rejected.
- paths containing `..` segments are rejected.
- paths resolving outside `<ProjectRoot>` are rejected.
- paths under `.cooked`, `.imported`, `.build`, or `.oxygen` are rejected.
- mount names are unique case-insensitively across authoring and local mounts.
- mount names match `^[A-Za-z][A-Za-z0-9_-]{0,63}$`.
- reserved mount names are rejected case-insensitively:
  `Engine`, `Project`, `Config`, `Derived`, `Cooked`, `Imported`, `Build`,
  `Packages`, `Oxygen`.
- `Content` is required and maps to `Content`.
- changing a mount name changes asset identity and requires an explicit
  project-wide reference update.
- changing a mount backing path without changing the mount name is a storage
  relocation and preserves asset identity when the content set is equivalent.

### Local Folder Mounts

`LocalFolderMounts` are explicit absolute authored content roots outside the
project root.

Shape:

```json
"LocalFolderMounts": [
  { "Name": "StudioLibrary", "AbsolutePath": "D:\\Studio\\SharedContent" }
]
```

Rules:

- paths are absolute.
- relative paths are rejected.
- mount names follow the same uniqueness, regex, and reserved-word rules as
  authoring mounts.
- local mounts are browsable as authored content roots.
- local mounts are not default creation targets unless the user explicitly
  selects that local mount as the target in a creation workflow.
- local mount availability failures are `ProjectContentRoots` diagnostics.

Local mount identity example:

```text
asset:///StudioLibrary/Characters/Hero.ogeo.json
```

### Default Creation Targets

Default create target rules are asset-kind-specific:

| User selection | Scene target | Material target | Geometry target | Script target |
| --- | --- | --- | --- | --- |
| project root | `/Content/Scenes` | `/Content/Materials` | `/Content/Geometry` | `/Content/Scripts` |
| `/<Mount>` authoring root | `/<Mount>/Scenes` | `/<Mount>/Materials` | `/<Mount>/Geometry` | `/<Mount>/Scripts` |
| folder under an authoring mount and matching the asset kind | selected folder | selected folder | selected folder | selected folder |
| folder under an authoring mount but not matching the asset kind | `/<Mount>/Scenes` | `/<Mount>/Materials` | `/<Mount>/Geometry` | `/<Mount>/Scripts` |
| local mount root explicitly chosen as target | `/<Local>/Scenes` | `/<Local>/Materials` | `/<Local>/Geometry` | `/<Local>/Scripts` |
| folder under an explicitly targeted local mount and matching the asset kind | selected folder | selected folder | selected folder | selected folder |
| folder under an explicitly targeted local mount but not matching the asset kind | `/<Local>/Scenes` | `/<Local>/Materials` | `/<Local>/Geometry` | `/<Local>/Scripts` |
| folder under a local mount not explicitly chosen as target | `/Content/Scenes` | `/Content/Materials` | `/Content/Geometry` | `/Content/Scripts` |
| `Config`, `Packages`, or derived roots | `/Content/Scenes` | `/Content/Materials` | `/Content/Geometry` | `/Content/Scripts` |

Before creation, UI presents the effective target as both a virtual folder and
resulting asset URI. Example:

```text
Target folder: /Content/Materials
Asset URI: asset:///Content/Materials/Red.omat.json
```

### Derived And Output Roots

Derived roots are never authored content roots:

```text
.cooked/
.imported/
.build/
.oxygen/
```

Cooked output mapping:

```text
source: <ProjectRoot>/Content/Materials/Gold.omat.json
cooked: <ProjectRoot>/.cooked/Content/Materials/Gold.omat
index:  <ProjectRoot>/.cooked/Content/container.index.bin

source: <ProjectRoot>/Content/Scenes/Main.oscene.json
cooked: <ProjectRoot>/.cooked/Content/Scenes/Main.oscene
index:  <ProjectRoot>/.cooked/Content/container.index.bin
```

Rules:

- `.cooked/<MountName>/container.index.bin` is the loose cooked index for that
  mount.
- `.cooked/<MountName>/...` paths are display/diagnostic facts, not persisted
  authored identities.
- `.imported` contains import intermediate output.
- `.build` contains build/package output.
- `.oxygen` contains editor-local project metadata or cache.
- deleting derived roots must not delete authored source data.

### Browser Positioning

Content Browser presents project layout as identity-aware content, not as an
undifferentiated file explorer.

Top-level browser groups:

```text
Project
  Content                         authored mount
  <ExtraAuthoringMount>            authored mount
  <LocalFolderMount>               local authored mount
  Config                           project configuration
  Packages                         project packages, when present
  Derived
    Cooked                         .cooked
    Imported                       .imported
    Build                          .build
```

Required labels:

- authoring mounts: `AUTH`
- local authored mounts: `LOCAL`
- configuration: `CONFIG`
- packages: `PKG`
- cooked output: `COOKED`
- imported/intermediate output: `DERIVED`
- build output: `BUILD`

Raw source media rows under `Content/SourceMedia` use `AssetKind.ForeignSource`
or a more specific source kind. They never use `AssetState.Cooked` unless the
row represents a cooked artifact from `.cooked`.

## 6. Ownership

The table lists positive ownership. Forbidden ownership edges are defined in
[Dependency Rules](#dependency-rules); consumers must not infer authoring roots
or target folders outside the project layout policy.

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.Projects` | `Project.oxy`, mount validation, project creation layout validation, project cook-scope facts. |
| `Oxygen.Editor.ProjectBrowser` | predefined template presentation and create-from-template workflow. |
| `Oxygen.Editor.ContentBrowser` | user-facing projection of authoring, local, config, packages, and derived roots. |
| `Oxygen.Editor.WorldEditor` | scene document create/open/save under `Content/Scenes`. |
| `Oxygen.Editor.MaterialEditor` | material descriptor create/open/save under `Content/Materials` or selected material target. |
| `Oxygen.Editor.ContentPipeline` | import/cook outputs under derived roots and cooked indexes. |
| `Oxygen.Assets` | reusable asset URI, catalog, import, and cook primitives. |

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
  "AuthoringMounts": [
    { "Name": "Content", "RelativePath": "Content" }
  ],
  "LocalFolderMounts": [],
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
- `AuthoringMounts` is required and follows the mount rules above.
- `LocalFolderMounts` is optional; predefined built-in templates use `[]`.
- `StarterScene` is required for built-in V0.1 templates.
- every `StarterContent` entry must be under an authoring mount.

Template metadata is not copied into `Project.oxy` as project identity.

### Predefined Templates

| TemplateId | Category | DisplayName | Required media | Required payload | Starter scene | OpenOnCreate |
| --- | --- | --- | --- | --- | --- | --- |
| `Games/Blank` | `Games` | `Blank` | `Media/Icon.png`, `Media/Preview.png` | required folder skeleton, `Content/Scenes/Main.oscene.json`, default material | `asset:///Content/Scenes/Main.oscene.json` | Yes |
| `Games/First Person` | `Games` | `First Person` | `Media/Icon.png`, `Media/Preview.png` | required folder skeleton, `Content/Scenes/FirstPerson.oscene.json`, default material, starter script folder | `asset:///Content/Scenes/FirstPerson.oscene.json` | Yes |
| `Visualization/Blank` | `Visualization` | `Blank` | `Media/Icon.png`, `Media/Preview.png` | required folder skeleton, `Content/Scenes/Main.oscene.json`, default material | `asset:///Content/Scenes/Main.oscene.json` | Yes |

### Created Project Manifest

After create-from-template, the project `Project.oxy` contains:

```json
{
  "SchemaVersion": 1,
  "Id": "<new-guid>",
  "Name": "<user-project-name>",
  "Category": "<template-category>",
  "Thumbnail": "Media/Preview.png",
  "AuthoringMounts": [
    { "Name": "Content", "RelativePath": "Content" }
  ],
  "LocalFolderMounts": []
}
```

`Id` is generated at creation time. `Name` is the user-selected project name.

## 8. Commands, Services, Or Adapters

### Project Creation

Create-from-template performs:

1. Validate `Template.json`.
2. Verify target folder does not exist or exists and has no filesystem entries.
3. Verify `SourceFolder` does not contain `Project.oxy`; reject the template
   if it does.
4. Copy template payload except `Template.json` to the target project folder.
5. Verify the required folder skeleton exists.
6. Generate a new non-empty project `Id`.
7. Write `Project.oxy` with the new id, user-selected name, category,
   thumbnail, `AuthoringMounts`, and `LocalFolderMounts`.
8. Validate the created project manifest and mounts.
9. Activate the project through the normal project activation path.
10. If `StarterScene.OpenOnCreate` is true, request opening that scene document.

When `StarterScene` is absent or `OpenOnCreate` is false, project activation
completes with an empty workspace; no scene document is opened and no
diagnostic is emitted. Predefined built-in V0.1 templates require
`StarterScene` and use `OpenOnCreate = true`.

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
        ContentBrowserSelection? selection);
}

public sealed record AuthoringTarget(
    string MountName,
    string ProjectRelativeFolder,
    Uri FolderAssetUri,
    bool IsExplicitLocalMount,
    AuthoringTargetFallbackReason? FallbackReason);
```

Rules:

- `ProjectContext` supplies the active project root, authoring mounts, and
  local folder mounts.
- `AssetKind` maps to the kind folders in the default creation target table.
- `ContentBrowserSelection` is optional and contains the selected root/folder
  identity from Content Browser, never a raw cooked path.
- `ResolveCreateTarget` applies the matrix in
  [Default Creation Targets](#default-creation-targets).
- `FallbackReason` is set when the resolver ignores a selected folder because
  it is not an authored target for the requested kind.

Required consumers:

- WorldEditor scene create.
- MaterialEditor material create.
- Content Browser create-from-context actions.
- Future geometry/script/prefab/animation creation/import surfaces.

## 9. UI Surfaces

Project Browser:

- templates show `DisplayName`, `Description`, `Icon`, `Preview`, and category.
- template detail shows starter scene and mount layout.
- create-project errors name the template field, folder path, or mount token
  that failed.

Content Browser:

- authoring, local, config, packages, and derived roots use the labels defined
  in Browser Positioning.
- create actions show target folder and resulting asset URI before confirming.
- raw source media rows are importable/source rows.
- cooked rows are derived output rows and are not editable as source.

## 10. Persistence And Round Trip

New project from template must round-trip:

- unique project id.
- user-selected project name.
- category.
- optional thumbnail.
- standard `Content` mount.
- empty `LocalFolderMounts` for built-in templates.
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

| Condition | Domain | Required diagnostic detail |
| --- | --- | --- |
| missing or invalid `Project.oxy` | `ProjectValidation` | manifest path and parse/validation reason |
| missing/inaccessible authoring mount | `ProjectContentRoots` | mount name and resolved path |
| invalid authoring mount path | `ProjectContentRoots` | mount name, raw path, validation rule |
| invalid local folder mount path | `ProjectContentRoots` | mount name, absolute path, validation rule |
| invalid template metadata | `ProjectTemplate` | template path and field name |
| template copy or manifest rewrite failure | `ProjectTemplate` | source path, target path, exception type |
| starter scene missing/outside mount | `ProjectTemplate` | starter scene URI and relative path |
| derived cooked index invalid | `ContentPipeline` | mount name and index path |
| browser cannot map selection to a mount | `AssetIdentity` | selection path and active mount list |

## 13. Dependency Rules

- `Oxygen.Editor.Projects` owns project layout policy.
- `Oxygen.Editor.ContentBrowser` consumes project layout policy; it must not
  invent authoring roots from arbitrary project-root folders.
- `Oxygen.Editor.ContentPipeline` consumes mount/cook-scope policy; it must not
  define project templates.
- `Oxygen.Editor.Runtime` consumes cooked-root mount policy; it must not own
  authoring layout.
- `Oxygen.Editor.Interop` must not know project layout policy.
- `Oxygen.Assets` owns reusable URI/catalog primitives but must not own editor
  project-template workflow.

## 14. Validation Gates

Project layout/template work is complete when these checks pass:

1. Creating each predefined template writes a `Project.oxy` with a non-empty
   unique GUID, user-selected name, category, `Content` authoring mount, and
   empty built-in-template `LocalFolderMounts`.
2. Creating two projects from the same template produces two different project
   ids. Predefined templates do not ship `Project.oxy`.
3. Built-in template validation fails when `SourceFolder` contains
   `Project.oxy`.
4. Creating each predefined template creates every folder marked required in
   the folder requirements table.
5. Built-in template validation fails when `Template.json` omits
   `TemplateId`, `Category`, `DisplayName`, `Description`, `Icon`,
   `Preview`, `SourceFolder`, `AuthoringMounts`, or `StarterScene`.
6. Built-in template validation fails when required `Icon` or `Preview` media
   paths do not exist in the template payload.
7. Mount validation rejects rooted authoring paths, `..` authoring paths,
   duplicate mount names, reserved mount names, derived-root mount paths, and
   missing `Content`.
8. Local mount validation rejects relative local paths and duplicate/reserved
   mount names.
9. Scene create resolves to `Content/Scenes/<Name>.oscene.json` and
   `asset:///Content/Scenes/<Name>.oscene.json`.
10. Material create at project root resolves to `Content/Materials/<Name>.omat.json`
   and `asset:///Content/Materials/<Name>.omat.json`.
11. Create actions under an extra authoring mount resolve to that mount's
   asset-kind folder and URI.
12. Create actions under a local mount use that mount only when the local mount
    is explicitly selected as the creation target; otherwise they resolve to
    the default `Content` asset-kind folder.
13. Content Browser shows authoring, local, config, packages, cooked, imported,
    and build roots with the required labels.
14. `Content/SourceMedia` rows reduce to source/importable state, not cooked
    state.
15. `.cooked/<Mount>/container.index.bin` is displayed as derived output and is
    never persisted as an authored asset identity.

## 15. Open Issues

None.

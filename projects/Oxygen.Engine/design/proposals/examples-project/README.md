# The examples project

Status: accepted and implemented (abdes/DroidNet#120), on the
[source mounts](../source-mounts/README.md) contract. The
[Today](#today) section records the layout it replaced.

Last updated: 2026-10-29

Read: [at a glance](#at-a-glance), [scenarios](#scenarios),
[today](#today), [the project](#the-project),
[the examples](#the-examples), [tools](#tools),
[the installed showcase](#the-installed-showcase),
[migration](#migration), [dependencies](#dependencies),
[decisions](#decisions).

## At a glance

| Question     | Answer                                                                                                                                                                                                                                                                                                           |
| ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| What is it?  | `Examples/Content` reorganized as `Examples/Showcase`, an ordinary Oxygen project and an editor project that no tool writes into, in the layout every project has: a `Project.oxy`, the `Content` mount, raw media in `Content/SourceMedia`, one folder and one manifest per example, and a catalog of examples. |
| Why?         | It works today only through conventions no other project has: `../` webs between scene folders, flat `/.cooked/...` identities that collide, raw media mixed with descriptors, and an examples-only source list.                                                                                                 |
| The rule     | The examples get no special case in the cooker, ImportTool, PakTool, the runtime or the editor. If the examples need something, every project gets it.                                                                                                                                                           |
| Mounts       | One, `Content -> Content`, committed. Authored sources and raw media are separated by folder, as the project layout defines, not by mount.                                                                                                                                                                       |
| Output       | The editor's `.cooked/`, `.build/` and `.mounts.json`, ignored, never inside a mount. RenderScene's tools cook, package and play into RenderScene's own `.cooked/Content`, `.build/pak` and `.scratch`.                                                                                                          |
| Not in scope | Converting the cooker scene descriptors into editor scene documents; new examples; changes to the content of any example.                                                                                                                                                                                        |

## Scenarios

**Cook and play one example.** A developer runs RenderScene's `Tools/cook BottleOnBox`, then
`Tools/play BottleOnBox`. The first reads
`Manifests/BottleOnBox.import-manifest.json`, which names everything the
example needs, including the shared cube from `/Content/Geometry`. Nothing
else is cooked.

**Cook every example, or a group.** The build agent runs RenderScene's `Tools/cook -All`.
One ImportTool batch reads every manifest, checks the mount table once, and
cooks each shared file once. `Tools/cook -Tag showcase` cooks the examples the
SDK ships.

**Add an example.** A developer creates `Content/Scenes/FogRamp/`, writes its
descriptors and scene there, adds `Manifests/FogRamp.import-manifest.json`, and
adds one entry to `Examples.json`. No CMake list, tool script or test changes.

**Share a part between examples.** `VsmTwoCubes` needs the red cube that
`BottleOnBox` uses. The cube's descriptors live in `Content/Geometry/` and
`Content/Materials/`, and both manifests reference
`/Content/Geometry/cube.ogeo.json`. Before, the
second example reaches into `../bottle-on-box/`, and `VsmBug1Flat`, which keeps
its own `cube.geometry.json`, cooks to the same `/.cooked/Geometry/cube.ogeo`
identity: one silently replaces the other (abdes/DroidNet#81).

**Import a model of one's own.** A developer wants to try
`D:/Downloads/town4new.glb` in RenderScene. They add
`"Downloads": "D:/Downloads"` to a `.mounts.json` at or above RenderScene and
import it. The import goes to RenderScene's scratch project, `.scratch`, which
mounts `Showcase/Content`: its record stores
`"source": "/Downloads/town4new.glb"` and never enters the committed
`Imports/`. Before, the record stored the absolute path beside the committed
ones, and an examples-only `import-sources.local.json` listed such files.

**Regenerate a generated example.** A developer edits
`Tools/Generators/generate_city.py` and reruns it. It rewrites
`Content/Scenes/CityEnvironmentValidation/` and its manifest, and the next cook
picks the
changes up. The generator is no longer a file inside the content it writes.

**Refresh a downloaded asset.** A maintainer bumps the URL and hash of
`Sky.hdr` in `Content/SourceMedia/showcase-assets.json`. `Tools/fetch`
downloads and checks it into `Content/SourceMedia/Images/Showcase/`. Credits
stay next to it in `Content/SourceMedia/ASSET_CREDITS.md`.

## Today

`Examples/Content` holds 429 tracked files: 20 scene folders under `scenes/`,
raw media in `fbx/`, `glb/`, `models/` and `images/`, 11 tracked model import
records in `imports/`, four tool scripts with `.cmd` wrappers at the root, two
schemas, and about 7 GB of ignored output (`.cooked/main`, `.cooked/imports`,
`pak/`) in the same tree.

| Problem                             | Evidence                                                                                                                                                                              |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Flat identities that collide        | Every job cooks to `/.cooked/<Type>/<name>`. `vsm-bug1-flat` and `bottle-on-box` both write `/.cooked/Geometry/cube.ogeo` and `/.cooked/Materials/cube_red.omat` from different files |
| Examples reach into each other      | 14 of 20 manifests leave their folder: `../bottle-on-box/` from four scenes, `../../images/`, `../../imports/`, `../../.cooked/main` as output                                        |
| Paths into the engine sources       | `$schema` paths four levels up (`../../../../src/Oxygen/...`) in manifests and in 18 descriptors of `sdk-materials` and `surface-overrides`                                           |
| Raw media mixed with descriptors    | `bottle-on-box/waterbottle/` holds vertex and index `.bin` files and PNGs beside its descriptors; `engine-assets` and `emissive` hold PNGs                                            |
| Generators inside content           | `generate_city.py`, `UvProbe.py` in scene folders; `SurfaceCar.py`, `Vehicle.py` among the models they write                                                                          |
| Folders that are not examples       | `sdk-furniture` and `sdk-lantern` hold only a manifest of model records                                                                                                               |
| Job-level exclusions                | `prepare_showcase.py` drops four jobs of `physics_domains` (its VSM benchmark) by id                                                                                                  |
| Selection lives in CMake            | The shipped examples are a list in `CMakeLists.txt` (`OXYGEN_SHOWCASE_SCENES`)                                                                                                        |
| Absolute paths in records           | Untracked records in `imports/` store `C:/Users/...` and `F:/projects/...` sources; `import-sources.local.json` is the examples-only workaround                                       |
| Every new record edits `.gitignore` | `imports/*.import.json` is ignored, and each tracked record is a `!` line                                                                                                             |
| Mixed naming                        | `bottle-on-box`, `physics_domains`, `sdk-materials`; scene files `bottle-on-box.scene.json`, `SdkMaterials.scene.json`, `multi_script_scene.scene.json`                               |

## The project

### Layout

```text
Examples/Showcase/                       # project root: an editor project
  Project.oxy                            # Id; Mounts: Content -> Content
  Examples.json                          # the catalog of examples
  README.md
  .gitignore                             # .cooked/, .build/, .mounts.json:
                                         # the editor's outputs
  Content/                               # mount Content: authored sources
    Geometry/  Materials/                # parts several examples use
    Scenes/<Example>/                    # one folder per example with scenes
      <Scene>.scene.json                 # cooker scene descriptor(s)
      *.omat.json *.ogeo.json *.otex.json
      *.lua *.input.json *.shape.json ...
      README.md                          # optional
    SourceMedia/                         # raw media, never cooked as is
      DCC/                               # fbx, glb, gltf, blend, mesh .bin
      Images/                            # hdr, jpg, png, exr, webp
      showcase-assets.json               # downloaded files: path, URL, hash
      ASSET_CREDITS.md
  Manifests/<Example>.import-manifest.json
  Imports/<identity>.import.json         # model import records, committed
  Tools/                                 # fetch and the project's schemas
    Generators/                          # scripts that write sources
    examples.schema.json
    showcase-assets.schema.json

Examples/RenderScene/                    # owns the cooking
  Tools/                                 # cook, pak, play
    internal/                            # ExamplesTools.ps1, prepare_showcase.py
  .cooked/Content/                       # cooked by Tools/cook, ignored
  .build/pak/                            # examples.pak and its catalog, ignored
  .scratch/                              # ad-hoc imports: a scratch project
```

```json
{
  "Id": "018f8f8f-1234-7abc-8def-0123456789ab",
  "Name": "Oxygen Examples",
  "SchemaVersion": 2,
  "Mounts": [{ "Name": "Content", "Kind": "Source", "Path": "Content" }]
}
```

This is the layout of
[project layout and templates](../../../../../design/editor/lld/project-layout-and-templates.md),
which every Oxygen project follows: authored content in the `Content` mount,
raw media in `Content/SourceMedia/` with its `DCC`, `Images`, `Audio` and
`Video` folders. The examples use the two media folders they need.

### What goes where

| Zone                   | Holds                                                                                                     | Cooked?                                                    |
| ---------------------- | --------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| `Content/`             | Files written for Oxygen: descriptors, scene descriptors, scripts, input actions, physics, buffer layouts | Yes, each by a job; cooked identity mirrors its mount path |
| `Content/SourceMedia/` | Files Oxygen reads but does not define: DCC models, exported mesh buffers, images                         | Only through a descriptor or a model import record         |
| `Manifests/`           | One import manifest per example                                                                           | No: they drive the cook                                    |
| `Imports/`             | Model import records, at their model's identity, as `ModelImportRecord` puts them for any project         | No: they drive model imports                               |
| `Tools/`               | `fetch`, the generators that write sources, and the schemas of the catalogs                               | No                                                         |
| `.cooked/`, `.build/`  | The editor's outputs when it opens the project                                                            | Output, ignored                                            |

The test for a file: a file someone writes by hand or a generator writes in
an Oxygen format belongs in `Content/Geometry`, `Content/Materials` or
`Content/Scenes`; a file that
comes from a DCC tool, a camera, a download or a binary export belongs in
`Content/SourceMedia`.

`SourceMedia` holds sources only. The engine refuses a cooked identity with a
`SourceMedia` segment (`VirtualPath.cpp`, the authored rule set), so nothing is
cooked into it. A texture descriptor
`/Content/Scenes/SdkMaterials/SdkWood_base_color.otex.json` names
`/Content/SourceMedia/Images/Showcase/Wood051_1K-JPG_Color.jpg` by its mount
path and cooks to `/Content/Scenes/SdkMaterials/SdkWood_base_color.otex`. A
descriptor names a file by mount path, or by a path relative to itself that
stays in its mount.

### Cooked identities

Every cooked identity is its source's mount path with the cooked extension:
`.omat.json` becomes `.omat`, `.scene.json` becomes `.oscene`, `.lua`
becomes `.oscript`.

| Source                                                         | Cooked identity                                    |
| -------------------------------------------------------------- | -------------------------------------------------- |
| `/Content/Geometry/cube.ogeo.json`                             | `/Content/Geometry/cube.ogeo`                      |
| `/Content/Scenes/BottleOnBox/BottleOnBox.scene.json`           | `/Content/Scenes/BottleOnBox/BottleOnBox.oscene`   |
| `/Content/Scenes/MultiScript/day-night.lua`                    | `/Content/Scenes/MultiScript/day-night.oscript`    |
| Model record for `/Content/SourceMedia/DCC/Showcase/chair.fbx` | `/Content/Models/Showcase/chair.fbx`, its record's |

A model's identity is `/Content/Models/<path under SourceMedia/DCC>`: the
engine names a model by its source file's name and extension, and its
products under it (`/Content/Models/Showcase/chair.fbx/chair.oscene`). Two
sources can then never cook to one identity unless they are the same file.
A cooked root holds the one mount its identities name, so manifests declare no
layout root.

### Mount rules applied

- The `Content` mount is a committed relative path; a clone cooks with no
  `.mounts.json`.
- The raw media are committed with the examples, so no mount is needed to
  reach them. Media too large to commit would get a placeholder mount of its
  own, bound in `.mounts.json`, as in any project.
- Local additions such as `Downloads` work as in the scenario above. A
  manifest or descriptor that references one is warned with
  `mount.reference_local` before it is committed.
- `Examples/Showcase` is under `projects/Oxygen.Engine`, below the repository.
  A developer's `H:/projects/.mounts.json` applies to it like to any project.
  The examples do not set `"root": true`: they declare no placeholder, a
  parent file can only add local mounts and never retarget `Content`, and
  what it adds is logged with every run.

## The examples

### Names

An example's name is PascalCase, its folder is `Content/Scenes/<Name>/`, and
its manifest is `Manifests/<Name>.import-manifest.json`. A scene file is named
after the scene it defines.

| Today                         | Example                      | Notes                                                                     |
| ----------------------------- | ---------------------------- | ------------------------------------------------------------------------- |
| `bottle-on-box`               | `BottleOnBox`                | Its cube, floor and ground physics move to `Common`                       |
| `camera-switch`               | `CameraSwitch`               |                                                                           |
| `city-environment-validation` | `CityEnvironmentValidation`  | Written by `Tools/Generators/generate_city.py`                            |
| `compressed-textures`         | `CompressedTextures`         |                                                                           |
| `emissive`                    | `Emissive`                   | `MatEmissiveMasked_mask.png` moves to `SourceMedia/Images/Emissive/`      |
| `engine-assets`               | `EngineAssets`               | `UvProbe_*.png` move to `SourceMedia/Images/UvProbe/`                     |
| `instancing`                  | `Instancing`                 |                                                                           |
| `kenney-characters`           | `KenneyCharacters`           |                                                                           |
| `multi-script`                | `MultiScript`                |                                                                           |
| `physics_domains`             | `PhysicsDomains`             | Without its benchmark                                                     |
| (part of `physics_domains`)   | `PhysicsDomainsVsmBenchmark` | Its scene, camera script and sidecar; uses `PhysicsDomains` parts by path |
| `point-shadow-validation`     | `PointShadowValidation`      |                                                                           |
| `proc-cubes`                  | `ProcCubes`                  |                                                                           |
| `rotating-gltf`               | `RotatingGltf`               |                                                                           |
| `sdk-furniture`               | `Furniture`                  | A model-only example: a manifest and no folder until it has a scene       |
| `sdk-lantern`                 | `Lantern`                    | Its scene instances the model and owns its sky, a cubemap of `Sky.hdr`    |
| `sdk-materials`               | `SdkMaterials`               |                                                                           |
| `spot-shadow-validation`      | `SpotShadowValidation`       |                                                                           |
| `surface-overrides`           | `SurfaceOverrides`           |                                                                           |
| `vsm-bug1-flat`               | `VsmBug1Flat`                | Its own cube and floor are kept as its own, under distinct mount paths    |
| `vsm-two-cubes`               | `VsmTwoCubes`                | Uses the shared cube                                                      |

Descriptors take the editor's source names where the editor has one:
`*.material.json` → `*.omat.json`, `*.geometry.json` → `*.ogeo.json`,
`*.texture.json` → `*.otex.json`. Other descriptor kinds keep their names.

### Shared parts

A file lives in `Content/Geometry` or `Content/Materials` when two or more
examples use the same file.
Two examples that hold different files with one name keep them apart: they are
different sources with different mount paths and different identities. A
shared file is never edited for one example; an example that needs a
variation copies it into its own folder under its own name.

### The catalog

`Examples.json` lists the examples once. Tools, CMake and tests read it; no
other file lists examples.

```json
{
  "version": 1,
  "examples": [
    {
      "name": "BottleOnBox",
      "description": "A water bottle dropped on a box, with rigid-body physics.",
      "scenes": ["BottleOnBox"],
      "tags": ["physics"]
    },
    {
      "name": "SdkMaterials",
      "description": "Marble and wood materials on four cubes.",
      "scenes": ["SdkMaterials"],
      "tags": ["showcase", "materials"]
    }
  ]
}
```

| Field         | Contract                                                                                       |
| ------------- | ---------------------------------------------------------------------------------------------- |
| `name`        | The example name; its folder and manifest follow from it                                       |
| `description` | One sentence, shown by `Tools/cook -List`                                                      |
| `scenes`      | Scene names, played as `/Content/Scenes/<name>/<scene>.oscene`; empty for model-only examples  |
| `tags`        | Free words. `showcase` marks what the SDK ships; `validation` and `benchmark` mark test scenes |

The schema is `Tools/examples.schema.json`. It is part of the examples project,
not an engine contract.

### Manifests

Each manifest names every job its example needs, shared ones included, so an
example cooks alone. It names no `output`: the tool that runs it chooses the
cooked root.

```json
{
  "$schema": "oxygen.import-manifest.schema.json",
  "version": 1,
  "project_root": "..",
  "mounts": [{ "name": "Content", "kind": "Source", "path": "Content" }],
  "jobs": [
    {
      "id": "/Content/Materials/cube_red.omat",
      "type": "material-descriptor",
      "source": "/Content/Materials/cube_red.omat.json",
      "virtual_path": "/Content/Materials/cube_red.omat"
    },
    {
      "id": "/Content/Geometry/cube.ogeo",
      "type": "geometry-descriptor",
      "source": "/Content/Geometry/cube.ogeo.json",
      "virtual_path": "/Content/Geometry/cube.ogeo",
      "depends_on": ["/Content/Materials/cube_red.omat"]
    },
    {
      "id": "/Content/Scenes/VsmTwoCubes/VsmTwoCubes.oscene",
      "type": "scene-descriptor",
      "source": "/Content/Scenes/VsmTwoCubes/VsmTwoCubes.scene.json",
      "virtual_path": "/Content/Scenes/VsmTwoCubes/VsmTwoCubes.oscene",
      "depends_on": ["/Content/Geometry/cube.ogeo"]
    }
  ]
}
```

- Each `virtual_path` follows the identity rule above. The rule is a
  convention of the examples, checked by `Tools/cook -Check`; the cooker
  accepts any identity, as for any project.
- A job's id is its identity, so ids never clash across the manifests of one
  batch, and a job several manifests name is one job.
- `$schema` is a name. Validation uses the schemas the tools embed, so no
  manifest or descriptor points into the engine sources.
- Model jobs name records by project path:
  `"record": "Imports/Content/Models/Lantern.glb.import.json"`.

### Model import records

A record lives at its model's identity under `Imports/`: the model
`/Content/Models/Showcase/chair.fbx` has the record
`Imports/Content/Models/Showcase/chair.fbx.import.json`. It stores `source` as
a mount path (`"/Content/SourceMedia/DCC/Showcase/chair.fbx"`) and its UUIDv7
source identity inside; giving the model another identity moves the record.
Every record in `Imports/` is committed. Ad-hoc imports from RenderScene and DemoShell go to
RenderScene's `.scratch` project, with identities under `/Content/Scratch/`, so
no local record ever sits among the committed ones.

## Tools

RenderScene owns the cooking. Its tools are PowerShell 7.4 with a `.cmd`
wrapper in `Examples/RenderScene/Tools/`. Each finds the examples project beside
RenderScene (`share/oxygen/Showcase` in the SDK) and the engine tools through
the build selection it uses today (`-Preset`, `-BuildTree`, `-Config`,
`-ToolPath`). They write only into RenderScene.

| Command                       | Does                                                                                                                                                 |
| ----------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Tools/cook <Name>...`        | One ImportTool `batch` with the named examples' manifests, into `.cooked/Content` (the global `--cooked-root`)                                       |
| `Tools/cook -Tag <tag>`       | The same, for the examples with the tag                                                                                                              |
| `Tools/cook -All`             | The same, for every example in `Examples.json`                                                                                                       |
| `Tools/cook -List`            | Names, tags and descriptions from `Examples.json`                                                                                                    |
| `Tools/cook -Check`           | Every example has its manifest, its scenes exist, every identity follows the identity rule, and every identity a manifest references is one it cooks |
| `Tools/pak`                   | `.build/pak/examples.pak` from `.cooked/Content`, which holds the models the manifests name                                                          |
| `Tools/play <Name> [<Scene>]` | Plays a scene of an example; the first scene by default                                                                                              |

The examples project keeps `Tools/fetch [-Check]`, which downloads and checks
the files of `SourceMedia/showcase-assets.json` at their recorded paths, and
`Tools/Generators/<script>`, which rewrite their own outputs (`generate_city.py`,
`UvProbe.py`, `SurfaceCar.py`, `Vehicle.py` (Blender)).

There is no model import tool: a model is cooked by the manifests of the
examples that use it, through its record.

ImportTool gains a `batch` form that takes several `--manifest` options and a
global `--cooked-root`. It reads every manifest first, so mount conflicts are
checked once and each shared source is cooked once. These are general
ImportTool features: an editor project with many scenes uses them the same way.

Script options that exist only for the old layout go: `-ContentRoot`
(the project root is fixed), `-SourceList` and `import-sources.*.json` (a
local mount replaces them), `-SourceKey` (the project's `Id`), `-CookedRoot`
and `-OutputDir` (the outputs are fixed).

## The installed showcase

The SDK ships the examples tagged `showcase` as the same project, reduced, at
`share/oxygen/Showcase`: `Project.oxy`, `Examples.json` filtered to the tag,
their manifests, the `Content` files their jobs and records reach, those
records, and `Tools/` without the generators. Beside it,
`share/oxygen/RenderScene` holds RenderScene's `Tools/`, the content cooked
from the project (`.cooked/Content`) and its pak (`.build/pak`). An SDK user
runs the same RenderScene tools as a contributor.

`prepare_showcase.py --tag showcase` follows manifests and records by mount
path, refuses a file outside the project, and removes what an earlier staging
left. Its job-level exclusions go: the benchmark is its own example without the
tag. `Examples/RenderScene/Showcase.cmake` stages the project, stages the build
selection script in `Tools/` as an installed SDK has it, runs the staged
RenderScene `Tools/cook -All` and `Tools/pak`, and validates the cooked root.

## Migration

The move is one change, made by a one-time script that is not kept:

1. Move files to the layout, with `git mv`, so history follows.
2. Rename descriptors to the editor's source names.
3. Rewrite every `/.cooked/...` reference (descriptors, scenes, records,
   tests, C++ example code) to the new `/Content/...` identity, from a map
   the script builds from the moves.
4. Rewrite every manifest: mounts, mount-path sources and new identities.
5. Rewrite records' `source` and `virtual_path`.
6. Write `Project.oxy`, `Examples.json` and `.gitignore`.
7. Delete `.cooked/` and `pak/`; the next cook rebuilds them.

The script fails on any reference it cannot map, so nothing is left pointing at
the old layout. A recook is required anyway by the loose cooked index version
change of the source mounts proposal.

## Dependencies

| Dependency                                                                                                                            | Change                                                                                                                    |
| ------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| `OXYGEN_EXAMPLE_SCENES_DIR` (Cooker and Scripting `test/CMakeLists.txt`)                                                              | Points at `Content/Examples`; the Lua type check walks it                                                                 |
| RenderScene `RuntimePaths.cpp`, `MainModule.cpp`, `main_impl.cpp`                                                                     | Cooked root `RenderScene/.cooked/Content`; the `Showcase/Content` source mount; no installed-only default scene or skybox |
| DemoShell `DemoMain.cpp`, `FileBrowserService.cpp`, `ContentVm.cpp`; VortexBasic                                                      | The `Content` mount folder; the file browser starts in `Content/SourceMedia/`; ad-hoc imports in RenderScene's `.scratch` |
| InputSystem, LightBench, Physics `MainModule.cpp`                                                                                     | `content_root` stays the project root; their own `.cooked` folders are unchanged                                          |
| `Examples/RenderScene/Showcase.cmake`, `prepare_showcase.py`                                                                          | Stage the project and RenderScene's content; the showcase set from the `showcase` tag                                     |
| `tools/cmake/tests/test_showcase.py`, `tools/presets/tests/test_build_selection.py`                                                   | The new layout, catalog and script names                                                                                  |
| `conanfile.py` exclusions, `.pre-commit-config.yaml` binary exclusions                                                                | `.cooked/`, `.build/`, `.scratch/`, `.mounts.json`, `Content/SourceMedia/DCC/`                                            |
| Engine docs naming the old paths (ImportTool and PakTool READMEs, `PROJECT_LAYOUT.md`, `BUILD_SYSTEM.md`, runtime and cooker designs) | Rewritten for the layout                                                                                                  |

## Decisions

- **An ordinary project.** The examples follow the project layout and the
  mount rules every project follows; no tool or runtime code knows them.
- **The standard project layout.** One `Content` mount; raw media in
  `Content/SourceMedia`, which the engine never cooks into. No layout of the
  examples' own, and no layout root: a cooked root holds the mount its
  identities name.
- **Identities mirror mount paths.** No flat `/.cooked/<Type>/` names; two
  files can never cook to one identity.
- **One folder and one manifest per example.** Each example cooks alone;
  shared parts live in `Content/Geometry` and `Content/Materials` and are
  cooked once in a batch.
- **Records at their identity.** `Imports/<identity>.import.json`, all
  committed; ad-hoc imports live in RenderScene's ignored `.scratch` project.
- **An editor project; RenderScene owns the cook.** No tool writes into the
  project; RenderScene's tools cook, package and play it into RenderScene's own
  folders. There is no import tool and no `.cooked/tools`.
- **A scene owns its look.** The Lantern scene declares its sky; no tool or
  app code picks a scene or a skybox for the showcase.
- **One catalog.** `Examples.json` is the only list of examples, with tags for
  groups; CMake, tools and tests read it.
- **Generators are tools.** They live in `Tools/Generators/` and write into
  `Content/Scenes` or `Content/SourceMedia`.
- **No local source lists.** A developer's own files are local mounts in
  `.mounts.json`; `import-sources.local.json` is removed.
- **Scene descriptors stay cooker scene descriptors.** Converting them to
  editor scene documents is a separate decision.

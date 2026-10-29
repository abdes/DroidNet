# The examples project

Status: proposal. Nothing here is implemented, scheduled or accepted. It is
written against `Examples/Content` as it is today, and it depends on the
[source mounts](../source-mounts/README.md) proposal.

Last updated: 2026-10-29

Read: [at a glance](#at-a-glance), [scenarios](#scenarios),
[today](#today), [the project](#the-project),
[the examples](#the-examples), [tools](#tools),
[the installed showcase](#the-installed-showcase),
[migration](#migration), [dependencies](#dependencies),
[decisions](#decisions).

## At a glance

| Question     | Answer                                                                                                                                                                                                                                                                          |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| What is it?  | `Examples/Content` reorganized as an ordinary Oxygen project, in the project layout every project has: a `Project.oxy`, the `Content` mount, raw media in `Content/SourceMedia`, one folder and one manifest per example, a catalog of examples, and derived output kept apart. |
| Why?         | It works today only through conventions no other project has: `../` webs between scene folders, flat `/.cooked/...` identities that collide, raw media mixed with descriptors, and an examples-only source list.                                                                |
| The rule     | The examples get no special case in the cooker, ImportTool, PakTool, the runtime or the editor. If the examples need something, every project gets it.                                                                                                                          |
| Mounts       | One, `Content -> Content`, committed. Authored sources and raw media are separated by folder, as the project layout defines, not by mount.                                                                                                                                      |
| Output       | `.cooked/` and `.build/`, ignored, never inside a mount.                                                                                                                                                                                                                        |
| Not in scope | Converting the cooker scene descriptors into editor scene documents; new examples; changes to the content of any example.                                                                                                                                                       |

## Scenarios

**Cook and play one example.** A developer runs `Tools/cook BottleOnBox`, then
`Tools/play BottleOnBox`. The first reads
`Manifests/BottleOnBox.import-manifest.json`, which names everything the
example needs, including the shared cube from `/Content/Common`. Nothing else
is cooked.

**Cook every example, or a group.** The build agent runs `Tools/cook -All`.
One ImportTool batch reads every manifest, checks the mount table once, and
cooks each shared file once. `Tools/cook -Tag showcase` cooks the examples the
SDK ships.

**Add an example.** A developer creates `Content/Examples/FogRamp/`, writes its
descriptors and scene there, adds `Manifests/FogRamp.import-manifest.json`, and
adds one entry to `Examples.json`. No CMake list, tool script or test changes.

**Share a part between examples.** `VsmTwoCubes` needs the red cube that
`BottleOnBox` uses. The cube's descriptors move to `Content/Common/` and both
manifests reference `/Content/Common/Geometry/cube.ogeo.json`. Today the
second example reaches into `../bottle-on-box/`, and `VsmBug1Flat`, which keeps
its own `cube.geometry.json`, cooks to the same `/.cooked/Geometry/cube.ogeo`
identity: one silently replaces the other (abdes/DroidNet#81).

**Import a model of one's own.** A developer wants to try
`D:/Downloads/town4new.glb` in RenderScene. They add
`"Downloads": "D:/Downloads"` to the project's `.mounts.json` and import it.
The record stores `"source": "/Downloads/town4new.glb"`, and `.gitignore`
keeps the record local. Today the record stores the absolute path, and an
examples-only `import-sources.local.json` lists such files.

**Regenerate a generated example.** A developer edits
`Tools/Generators/generate_city.py` and reruns it. It rewrites
`Content/Examples/CityEnvironmentValidation/`, and the next cook picks the
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
Examples/Content/                        # project root
  Project.oxy                            # Mounts: Content -> Content
  Examples.json                          # the catalog of examples
  README.md
  .gitignore                             # .mounts.json, .cooked/, .build/
  Content/                               # mount Content: authored sources
    Common/                              # parts several examples use
      Geometry/  Materials/  Physics/
    Examples/<Example>/                  # one folder per example
      <Scene>.scene.json                 # cooker scene descriptor(s)
      *.omat.json *.ogeo.json *.otex.json
      *.lua *.input.json *.shape.json ...
      README.md                          # optional
    SourceMedia/                         # raw media, never cooked as is
      DCC/                               # fbx, glb, gltf, blend, mesh .bin
      Images/                            # hdr, jpg, png, exr, webp
      showcase-assets.json               # downloaded files: URL, size, hash
      ASSET_CREDITS.md
  Manifests/<Example>.import-manifest.json
  imports/                               # model import records
  Tools/                                 # cook, import, pak, play, fetch
    Generators/                          # scripts that write sources
    internal/
  .cooked/                               # derived, ignored
    tools/Content/                       # cooked by Tools/cook
    imports/<source_identity>/           # model generations and head.json
  .build/pak/                            # examples.pak and its catalog
```

```json
{
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
| `imports/`             | Model import records, at the place `ModelImportRecord` puts them for any project                          | No: they drive model imports                               |
| `Tools/`               | Scripts that cook, import, pak, play, fetch and generate                                                  | No                                                         |
| `.cooked/`             | Cooked folders and model generations                                                                      | Output, ignored                                            |
| `.build/`              | Paks and their catalogs                                                                                   | Output, ignored                                            |

The test for a file: a file someone writes by hand or a generator writes in
an Oxygen format belongs in `Content/Common` or `Content/Examples`; a file that
comes from a DCC tool, a camera, a download or a binary export belongs in
`Content/SourceMedia`.

`SourceMedia` holds sources only. The engine refuses a cooked identity with a
`SourceMedia` segment (`VirtualPath.cpp`, the authored rule set), so nothing is
cooked into it. A texture descriptor
`/Content/Examples/SdkMaterials/SdkWood_base_color.otex.json` reads
`/Content/SourceMedia/Images/Showcase/Wood051_1K-JPG_Color.jpg` and cooks to
`/Content/Examples/SdkMaterials/SdkWood_base_color.otex`.

### Cooked identities

Every cooked identity is its source's mount path with the cooked extension:
`.omat.json` becomes `.omat`, `.scene.json` becomes `.oscene`, `.lua`
becomes `.oscript`.

| Source                                                         | Cooked identity                                      |
| -------------------------------------------------------------- | ---------------------------------------------------- |
| `/Content/Common/Geometry/cube.ogeo.json`                      | `/Content/Common/Geometry/cube.ogeo`                 |
| `/Content/Examples/BottleOnBox/BottleOnBox.scene.json`         | `/Content/Examples/BottleOnBox/BottleOnBox.oscene`   |
| `/Content/Examples/MultiScript/day-night.lua`                  | `/Content/Examples/MultiScript/day-night.oscript`    |
| Model record for `/Content/SourceMedia/DCC/Showcase/chair.fbx` | The record's `virtual_path`, `/Content/Models/Chair` |

A model's outputs land under its record's `virtual_path`, chosen once in
`/Content/Models/<Model>/`, as for any project. Two sources can then never
cook to one identity unless they are the same file.

### Mount rules applied

- The `Content` mount is a committed relative path; a clone cooks with no
  `.mounts.json`.
- The raw media are committed with the examples, so no mount is needed to
  reach them. Media too large to commit would get a placeholder mount of its
  own, bound in `.mounts.json`, as in any project.
- Local additions such as `Downloads` work as in the scenario above. A
  manifest or descriptor that references one is warned with
  `mount.reference_local` before it is committed.
- `Examples/Content` is under `projects/Oxygen.Engine`, below the repository.
  A developer's `H:/projects/.mounts.json` applies to it like to any project.
  The examples do not set `"root": true`: they declare no placeholder, a
  parent file can only add local mounts and never retarget `Content`, and
  what it adds is logged with every run.

## The examples

### Names

An example's name is PascalCase, its folder is `Content/Examples/<Name>/`, and
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
| `sdk-lantern`                 | `Lantern`                    | Same                                                                      |
| `sdk-materials`               | `SdkMaterials`               |                                                                           |
| `spot-shadow-validation`      | `SpotShadowValidation`       |                                                                           |
| `surface-overrides`           | `SurfaceOverrides`           |                                                                           |
| `vsm-bug1-flat`               | `VsmBug1Flat`                | Its own cube and floor are kept as its own, under distinct mount paths    |
| `vsm-two-cubes`               | `VsmTwoCubes`                | Uses the `Common` cube                                                    |

Descriptors take the editor's source names where the editor has one:
`*.material.json` → `*.omat.json`, `*.geometry.json` → `*.ogeo.json`,
`*.texture.json` → `*.otex.json`. Other descriptor kinds keep their names.

### What `Common` holds

A file moves to `Common` when two or more examples use the same file today.
Two examples that hold different files with one name keep them apart: they are
different sources with different mount paths and different identities. A
`Common` file is never edited for one example; an example that needs a
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

| Field         | Contract                                                                                        |
| ------------- | ----------------------------------------------------------------------------------------------- |
| `name`        | The example name; its folder and manifest follow from it                                        |
| `description` | One sentence, shown by `Tools/cook -List`                                                       |
| `scenes`      | Scene names, played as `/Content/Examples/<name>/<scene>.oscene`; empty for model-only examples |
| `tags`        | Free words. `showcase` marks what the SDK ships; `validation` and `benchmark` mark test scenes  |

The schema is `Tools/examples.schema.json`. It is part of the examples project,
not an engine contract.

### Manifests

Each manifest names every job its example needs, shared ones included, so an
example cooks alone:

```json
{
  "$schema": "oxygen.import-manifest.schema.json",
  "version": 1,
  "project_root": "..",
  "mounts": [{ "name": "Content", "kind": "Source", "path": "Content" }],
  "output": "../.cooked/tools/Content",
  "layout": { "virtual_mount_root": "/Content" },
  "jobs": [
    {
      "type": "material-descriptor",
      "source": "/Content/Common/Materials/cube_red.omat.json",
      "virtual_path": "/Content/Common/Materials/cube_red.omat"
    },
    {
      "type": "geometry-descriptor",
      "source": "/Content/Common/Geometry/cube.ogeo.json",
      "virtual_path": "/Content/Common/Geometry/cube.ogeo"
    },
    {
      "type": "scene-descriptor",
      "source": "/Content/Examples/VsmTwoCubes/VsmTwoCubes.scene.json",
      "virtual_path": "/Content/Examples/VsmTwoCubes/VsmTwoCubes.oscene"
    }
  ]
}
```

- Each `virtual_path` follows the identity rule above. The rule is a
  convention of the examples, checked by `Tools/cook -Check`; the cooker
  accepts any identity, as for any project.
- Job ids and `depends_on` are kept where a job needs another's output first,
  as today.
- `$schema` is a name. Validation uses the schemas the tools embed, so no
  manifest or descriptor points into the engine sources.
- Model jobs name records by mount-free project paths:
  `"record": "imports/01a0e7c7-....import.json"`.

### Model import records

Records stay in `imports/` and store `source` as a mount path, for example
`"/Content/SourceMedia/DCC/Showcase/chair.fbx"`. Their `virtual_path` moves from
`/.cooked/models/...` to `/Content/Models/<Model>`.

`.gitignore` ignores `imports/*.import.json` and tracks the records the
manifests name, as today. `Tools/import -Check` lists records that a manifest
names and git does not track, so a missing `!` line fails the check instead of
a clean clone.

## Tools

Every script is PowerShell 7.4 with a `.cmd` wrapper. Each finds the project
root as the parent of `Tools/` and the engine tools through the build selection
it uses today (`-Preset`, `-BuildTree`, `-Config`, `-ToolPath`).

| Command                         | Does                                                                                                                   |
| ------------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| `Tools/cook <Name>...`          | One ImportTool `batch` with the named examples' manifests                                                              |
| `Tools/cook -Tag <tag>`         | The same, for the examples with the tag                                                                                |
| `Tools/cook -All`               | The same, for every example in `Examples.json`                                                                         |
| `Tools/cook -List`              | Names, tags and descriptions from `Examples.json`                                                                      |
| `Tools/cook -Check`             | Every example has its manifest, every example with scenes has its folder, and every identity follows the identity rule |
| `Tools/import [-Record <file>]` | Imports every record in `imports/`, or one                                                                             |
| `Tools/import -Check`           | Records named by manifests that git does not track                                                                     |
| `Tools/pak [-Tag <tag>]`        | `.build/pak/examples.pak` from `.cooked/tools/Content` and the selected examples' model generations                    |
| `Tools/play <Name> [<Scene>]`   | Plays a scene of an example; the first scene by default                                                                |
| `Tools/fetch`                   | Downloads and checks the files of `SourceMedia/showcase-assets.json`                                                   |
| `Tools/Generators/<script>`     | Rewrites its own outputs: `generate_city.py`, `UvProbe.py`, `SurfaceCar.py`, `Vehicle.py` (Blender)                    |

ImportTool gains a `batch` form that takes several `--manifest` options. It
reads every manifest first, so mount conflicts are checked once and each shared
source is cooked once. This is a general ImportTool feature: an editor project
with many scenes uses it the same way.

Script options that exist only for the old layout go: `-ContentRoot`
(the project root is fixed), `-SourceList` and `import-sources.*.json` (a
local mount replaces them), `-SourceKey` (taken from `Project.oxy`).

## The installed showcase

The SDK ships the examples tagged `showcase` as the same project, reduced:
`Project.oxy`, `Examples.json` filtered to the tag, their manifests, the
`Content` files their jobs and records reach, the tracked
records, and `Tools/`. An SDK user runs the same `Tools/` commands as a
contributor.

`prepare_showcase.py` follows manifests and records through the mount table
instead of `../` paths, and refuses a file outside the `Content` mount. Its job-level
exclusions go: the benchmark is its own example without the tag. CMake reads
the tag from `Examples.json` instead of `OXYGEN_SHOWCASE_SCENES`.

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

| Dependency                                                                                                                            | Change                                                                                                                            |
| ------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| `OXYGEN_EXAMPLE_SCENES_DIR` (Cooker and Scripting `test/CMakeLists.txt`)                                                              | Points at `Content/Examples`; the Lua type check walks it                                                                         |
| RenderScene `RuntimePaths.cpp`, `MainModule.cpp`, `main_impl.cpp`                                                                     | Cooked root `.cooked/tools/Content`; skybox `/Content/SourceMedia/Images/Showcase/Sky.hdr` through the mount table; source mounts |
| DemoShell `DemoMain.cpp`, `FileBrowserService.cpp`, `ContentVm.cpp`; VortexBasic                                                      | Source mounts instead of script source roots; the file browser starts in `Content/SourceMedia/`; records in `imports/`            |
| InputSystem, LightBench, Physics, TexturedCube `MainModule.cpp`                                                                       | `content_root` stays the project root; their own `.cooked` folders are unchanged                                                  |
| `Examples/Content/CMakeLists.txt`, `prepare_showcase.py`                                                                              | Stage the project layout; the showcase set from the `showcase` tag                                                                |
| `tools/cmake/tests/test_showcase.py`, `tools/presets/tests/test_build_selection.py`                                                   | The new layout, catalog and script names                                                                                          |
| `conanfile.py` exclusions, `.pre-commit-config.yaml` binary exclusions                                                                | `.cooked/`, `.build/`, `.mounts.json`, `Content/SourceMedia/DCC/`                                                                 |
| Engine docs naming the old paths (ImportTool and PakTool READMEs, `PROJECT_LAYOUT.md`, `BUILD_SYSTEM.md`, runtime and cooker designs) | Rewritten for the layout                                                                                                          |

## Decisions

- **An ordinary project.** The examples follow the project layout and the
  mount rules every project follows; no tool or runtime code knows them.
- **The standard project layout.** One `Content` mount; raw media in
  `Content/SourceMedia`, which the engine never cooks into. No layout of the
  examples' own.
- **Identities mirror mount paths.** No flat `/.cooked/<Type>/` names; two
  files can never cook to one identity.
- **One folder and one manifest per example.** Each example cooks alone;
  shared parts live in `Common` and are cooked once in a batch.
- **One catalog.** `Examples.json` is the only list of examples, with tags for
  groups; CMake, tools and tests read it.
- **Generators are tools.** They live in `Tools/Generators/` and write into
  `Content/Examples` or `Content/SourceMedia`.
- **No local source lists.** A developer's own files are local mounts in
  `.mounts.json`; `import-sources.local.json` is removed.
- **Scene descriptors stay cooker scene descriptors.** Converting them to
  editor scene documents is a separate decision.

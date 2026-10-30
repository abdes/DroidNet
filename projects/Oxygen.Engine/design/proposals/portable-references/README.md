# Portable references

Status: proposal. It changes the reference contract of
[source mounts](../source-mounts/README.md) and the editor's
[asset identity](../../../../../design/editor/lld/asset-identity.md),
[project layout](../../../../../design/editor/lld/project-layout-and-templates.md),
[content pipeline](../../../../../design/editor/lld/content-pipeline.md) and
[Content Explorer](../../../../../design/editor/lld/content-explorer-asset-identity.md).

Last updated: 2026-10-30

The [design](design.md) gives the component design and the
[plan](plan.md) the slices and the debt ledger.

Read: [at a glance](#at-a-glance), [the studio](#the-studio),
[needs](#needs), [rules](#rules), [scenarios](#scenarios),
[proof](#proof), [today](#today), [other engines](#other-engines),
[engine](#engine), [editor](#editor), [migration](#migration),
[tests](#tests), [decisions](#decisions),
[accepted designs this touches](#accepted-designs-this-touches).

## At a glance

| Question     | Answer                                                                                                                                                                                                  |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| What is it?  | One contract that lets many games, developers and git repositories share content as sources, loose cooked roots or paks, import raw files with per-project settings, and promote assets into libraries. |
| Why?         | Every reference spells its mount name, so content resolves into whatever the host calls by that name; import records live outside the content they describe; identities are repeated inside files.      |
| Core change  | References inside a mount are relative to the referring asset; every imported asset is one authored file at its identity, whose source is a file reference; shared content never references a game.     |
| What stays   | Absolute mount paths across mounts, in manifests and in everything cooked: `AssetKey`s, the loose index, paks and the runtime resolver are unchanged.                                                   |
| Not in scope | New tools: a library is an ordinary editor project, cooked by the editor or a script and packed by PakTool, into any folder.                                                                            |

## The studio

The setup every scenario below uses. Paths are examples; nothing depends on
them.

| Thing              | Where                                                                                           | Git                                 | What it is                                                                                              |
| ------------------ | ----------------------------------------------------------------------------------------------- | ----------------------------------- | ------------------------------------------------------------------------------------------------------- |
| Game `Racer`       | `H:/studio/games/Racer`                                                                         | Its own repository                  | An editor project: `Project.oxy`, its own mount `Content`                                               |
| Game `Dungeon`     | `H:/studio/games/Dungeon`                                                                       | Its own repository                  | Same shape                                                                                              |
| Library `Vehicles` | `H:/studio/libraries/Vehicles`                                                                  | Its own repository                  | An editor project whose one source mount is `Vehicles`, folder `Vehicles/`                              |
| Library `Props`    | `H:/studio/libraries/Props`                                                                     | Its own repository                  | Same shape, mount `Props`. Each library project's cook publishes `.cooked/` with one root for its mount |
| Shipped `Props`    | `Props.pak`, built by the studio or downloaded from a web site; `Props/`, its loose cooked root | In a game repository, or not in git | The `Props` library as a bundle (S1); the loose root is the project's cooked output for its one mount   |
| Raw media          | `D:/Media/Raw`                                                                                  | Not an Oxygen project               | Plain model, image and audio files                                                                      |
| Downloads          | `C:/Users/<dev>/Downloads`                                                                      | No                                  | Never mounted                                                                                           |

`Racer/Project.oxy` declares its mounts once, with no absolute path:

```json
"Mounts": [
  { "Name": "Content", "Kind": "Source", "Path": "Content" },
  { "Name": "Vehicles", "Kind": "Cooked" },
  { "Name": "Props", "Kind": "Cooked" },
  { "Name": "RawMedia", "Kind": "Source" }
]
```

Both libraries are `Cooked`: the kind says the game reads cooked content; the
binding says which form this machine uses, a loose cooked root or a pak.
`Dungeon` declares `Props` the same way and has no `Vehicles`. Each machine
binds the placeholders once for every game, in the parent
`H:/studio/.mounts.json` (git-ignored, found up the folder hierarchy):

```json
{
  "version": 1,
  "mounts": {
    "Vehicles": "libraries/Vehicles/.cooked",
    "Props": "//share/oxygen/Props.pak",
    "RawMedia": "D:/Media/Raw"
  }
}
```

On this machine a developer of `Vehicles` reads its latest local cook and
every game reads the shared `Props.pak`. A developer who never opens the
`Vehicles` project binds `Vehicles` to `//share/oxygen/Vehicles.pak` instead,
and the build machine binds every library to its pak; the committed
`Project.oxy` is the same for all of them. A game's own `.mounts.json` wins
over the parent file. A library project binds nothing for its own mount: it
has a committed path.

Libraries are mounted cooked: as a loose cooked root or a pak. Mounting a
library's source folder works (the `Source` rows below) but is not the
idiomatic setup: the game then edits the library's files in place and cooks
them itself.

## Needs

| Id  | Need                                                                                                                                                                |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| N1  | Many games and developers at once; games in git                                                                                                                     |
| N2  | Library bundles maintained in their own projects, shared as sources, loose cooked roots or paks, with any asset kind, inputs and scripts                            |
| N3  | A library shipped as a pak, mounted by any game, including a pak downloaded from a web site                                                                         |
| N4  | Raw models and media from local storage, outside any project, imported by each project with its own settings                                                        |
| N5  | Ad-hoc downloads imported into one project, for that project only                                                                                                   |
| N6  | Promote any developed asset (scene, imported model, material, input, script) from a game into a shared library, whether the library is mounted writable or as a pak |
| N7  | Rename, move, delete, undo and redo, copy and paste on imported assets that are not read-only                                                                       |

## Rules

### R1. Own and shared mounts

A project's mount is **own** when its folder lies inside the project root
(`Racer/Content`), and **shared** otherwise (`Vehicles`, `RawMedia`, `Props`).
The role is computed from the canonical folders of the resolved table, so the
same library folder is own in its library project and shared in every game.

### R2. References

A reference field holds an absolute virtual path (`/Vehicles/Truck/Paint.omat`)
or a relative reference (`Paint.omat`, `../Textures/Brick.otex`,
`./Brick.otex`): text that does not start with `/`, made of leading `..`
segments or a single leading `.`, then canonical segments. `.` and `..`
anywhere else are refused (`vpath.segment.dot`). Every authored file, editor
scene documents included, stores this text; `asset:///` URIs are not written
in files. Segments are plain text with no percent-encoding.

A relative reference resolves against the parent folder of the referring
file's identity, for every format: a buffer container names its sibling
products and an input source names its own action as `Car.input/Jump.oiact`.
The result must keep the referrer's mount and may not climb above its root
(`vpath.reference_outside_mount`), and it is checked as an authored canonical
path. In an input mapping, text ending with `.oiact` is a reference and any
other text is a local action name; an action's name may not end with `.oiact`. Resolution happens once, when the
file is read; everything downstream sees canonical absolute paths.

One spelling per case:

| Target                           | Spelling                                                                           |
| -------------------------------- | ---------------------------------------------------------------------------------- |
| An asset in the referrer's mount | Relative, the shortest path. Absolute is refused (`vpath.reference_self_absolute`) |
| An asset in another mount        | Absolute                                                                           |

File references (a texture's `source`, `sources[].file`, a buffer's `source`,
a geometry's `buffers[].uri`, a model record's `source`) follow the same
grammar and resolve to a file through `MountTable::LocateReference`, which
allows `SourceMedia`. It is re-expressed over the same resolution, so one
rule has one code: `import.reference_outside_mount` is deleted.

### R3. Shared content never references own mounts

A file in a shared mount may reference its own mount, other shared mounts and
`Engine`, never an own mount of the project that reads it
(`reference.into_project_mount`). A library therefore never depends on a game,
and every game reads it the same way.

### R4. Identity comes from location

An authored file's identity is its mount path minus its authoring suffix, and
no file repeats it: `virtual_path` is removed from the collision shape,
physics material and texture descriptors and from the model import record,
and the editor's identity field for every format goes with it. A container
names its products relative to itself (`"virtual_path": "Truck_p0_vb.obuf"`
in `Truck.buffers.json`). Import
manifests keep absolute job identities: a manifest is one project's cook
instruction and declares its mounts.

### R5. An imported asset is one authored file at its identity

Every asset made from a source file is an authored file placed at the asset's
identity, which names its source by a file reference (R2):

| Asset   | Authored file                        | Identity                       | `source`                                              |
| ------- | ------------------------------------ | ------------------------------ | ----------------------------------------------------- |
| Texture | `Content/Textures/Brick.otex.json`   | `/Content/Textures/Brick.otex` | `Brick.png` beside it, or `/RawMedia/Walls/Brick.png` |
| Model   | `Content/Cars/Truck.fbx.import.json` | `/Content/Cars/Truck.fbx`      | `Truck.fbx` beside it, or `/RawMedia/Cars/Truck.fbx`  |

The model's authored file is its import record: settings, `source_identity`,
surface provenance, node continuity, published nodes and fingerprint. Its
products are named under its identity (`/Content/Cars/Truck.fbx/Paint.omat`).
The record's name keeps the source's extension, which gives the model its
importer.

A source beside its asset with the asset's name, or inside the folder that
holds that source, is the asset's **companion**: it moves, is renamed, copied
and deleted with the asset. A source in another mount is a reference: the
asset never moves, renames or deletes it. Any number of assets may name one
source, each with its own settings and identity.

`Imports/` and its cook-input prefix rule are deleted. A record is found at its
identity; the few queries that need every record (source relocation,
`source_identity` uniqueness, Content Explorer rows) enumerate the bound
`Source` mounts. A manifest's model job names the model identity only; its
`record` field is deleted.

### R6. Libraries

A library is an editor project with one source mount named after the library
(`Vehicles`), mounted under that name by every game. Shared as sources, its
files are portable by R2 to R4. Shared cooked, its loose root or pak carries
absolute paths and `AssetKey`s fixed when it was cooked, so a game switches a
library between `Source` and `Cooked` by editing the mount's `Kind`, and
between a loose root and a pak by editing only the binding, with no reference
rewrite. `Cooked` is the idiomatic kind; `Source` is supported but not
recommended.

The `Pak` kind is merged into `Cooked`. A `Cooked` mount binds a plain loose
cooked root (a folder with `container.index.bin`), a library project's
`.cooked` folder or a `.pak` file; the binding's target decides how it is
read, and a `Source` mount bound to a file is `mount.path_kind`. A `.cooked`
folder is the editor's publication format, so only the editor reads it: it
reads `head.json`, takes the selected publication's root for the mount's
name, and hands the engine that generation's folder wherever it hands over
mounts. When the
library project publishes again, the editor sees `head.json` change and
replaces the mounted generation with the new one
(`MountLooseCookedGeneration` with `replaces`), after the R10 check of the
new view; the Player's launch file receives the generation selected at
launch. Each generation has a fresh `SourceKey`, so nothing identifies a
library by `SourceKey`: its identity is its root name (R10).

Mount table resolution, in the editor's `MountTableBuilder` and the engine's
`ResolveMountTable`, checks every bound `Cooked` mount:

| Finding                            | When                                                                                  |
| ---------------------------------- | ------------------------------------------------------------------------------------- |
| `mount.library_root_mismatch`      | Its one root differs from the declared name (`Cars` declared, `/Vehicles/...` inside) |
| `mount.library_root_multiple`      | It holds more than one root (`/Vehicles/...` and `/Content/...`)                      |
| `mount.library_external_script`    | A script asset whose source is external: only a `Source` mount can supply it          |
| `mount.library_dependency_missing` | An asset the pak uses from another layer is not provided below it with its type (R10) |
| `mount.library_format_unsupported` | A pak or index format version this engine cannot read                                 |

Every pak has a browse index and names its library (R10), so no finding is
needed for a pak that cannot be named by path. A mount with one of these
findings is `Invalid`. Like `Missing`, it does not
fail opening: it stays in Content Explorer with its state and finding,
contributes no assets and is never added to a cook's or run's cooked roots, so
it cannot mask a game's assets or answer a reference under a wrong name. A
cooked root with embedded scripts and a pak built by PakTool, which seals
external scripts, pass. A project's own `.cooked` output is not a library
mount; `engine.pak` is undeclared and holds `/Engine`.

Adding a `Cooked` mount takes its name from its one root and places
it above the layers it depends on; the name is never typed, so a mismatch can
only come from a hand edit or a replaced file.

A library project declares its library mount instead of `Content`; the rule
that every project has a `Source` mount (`mount.source_required`) stays.

### R7. Read-only and writable

`Cooked` mounts are read-only. Shared `Source` mounts are writable,
and every write into one goes through the newer-on-disk guard, as today.

### R8. Operations

All operations run as one relocation transaction with Undo and Redo, as today.

| Operation                | Contract                                                                                                                                                                                                                                                                    |
| ------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Rename, move             | The asset's file and companions move; its own relative references and its referrers' references are respelled by R2; products follow their container; a model's `source_identity` is unchanged                                                                              |
| Move into a shared mount | Also moves, by R3, every asset and companion the moved assets need from own mounts. The review lists them, and the references that become absolute in the game                                                                                                              |
| Copy, paste, duplicate   | Creates new files at unique paths with respelled references; companions in an own mount are copied with them; a copy into a shared mount also copies, by R3, what it needs from own mounts; a copied model record gets a new `source_identity`; referrers are not rewritten |
| Delete                   | Recycles the asset and the companions no other asset of the loaded mounts names; never a source in another mount; the review lists the referrers first                                                                                                                      |
| Import                   | Writes the authored file at the chosen identity in one of the project's own mounts, copying the source in only when it is in no mount. Shared settings are made in the library project, where the library mount is own                                                      |

### R9. Other projects of a shared library

A project sees only its own references. A rename, move or delete in a shared
mount respells or reports the referrers of the projects open in this editor;
other games that mount the library are not read and nothing is stored for them.
The rename, move and delete reviews of an asset in a shared mount say so,
naming the mount. Another game finds what broke on its next open: its
reference check reports each reference that no longer resolves, and the
problem's **Retarget** action repoints that one reference through the asset
picker, as an ordinary edit with Undo.

### R10. Pak dependencies are what a pak uses

A pak that references assets of another layer (a library pak, `engine.pak`)
records, per dependency, the library's name (its one root, R6) and what it
uses there: each asset key with its asset type, each texture key and each
physics resource key with its type, the three kinds of cross-layer reference
the planner resolves. The catalog also names the pak's own library, and every
pak embeds its browse index (`--embed-browse-index` is deleted), so the
library of a providing asset is always known. A pak is identified by
`SourceKey` and a loose generation gets a fresh one on every cook, so the
root name is the one identity both kinds share. It records no content
version and no catalog digest of the dependency. PakTool's planner already
resolves every reference into a dependency and checks its type; it writes that
list into the catalog instead of the dependency's exact identity.

Mounting a pak checks its requirements against the layers below it as the
runtime resolves them, patches and tombstones included
(`PatchResolutionPolicy`): every required key must resolve, below the pak, to
an asset of the required type in a layer whose root is the required library
name, a pak, a patch of it or a loose generation alike. Mounting or refreshing a lower layer that would break a
requirement of a pak above it is refused the same way, so a library patch that
deletes an asset a game uses is caught before the view changes. A failure
lists the missing or retyped keys; the editor reports it as
`mount.library_dependency_missing`.

Patch bases are unchanged: a patch still names the exact layers its deltas
apply to, by `SourceKey`, content version and catalog digest, because it
describes a difference from those exact bytes. Dependencies only use keys.

Packing a project, a game or a library that uses another library, requires
every library it depends on to be a pak. PakTool takes dependencies only as
paks (`--dependency-pak`). The editor's pak build checks the mount table
first and fails with `pak.dependency_not_pak`, listing every library mount
bound to a loose root or a `.cooked` folder, before PakTool runs. PakTool
never sees a loose library: a reference no dependency pak resolves stays
`pak.plan.asset_reference_missing`, naming the library from the reference's
root. A shipped pak therefore depends on exactly the library paks it ships
with; loose libraries are for development.

A game pak is therefore rebuilt only when a library removes or retypes an asset
the game uses, whether that change ships as a new full pak or as a patch.

These requirements belong to paks. In the editor a game is not a pak: what it
uses is its references, and a library change that removes one shows as an
unresolved reference (R9).

### R11. Asset packages

An asset package carries assets from one project into another, whatever their
kind: scenes, imported models with their records and sources, materials,
textures, geometry, physics, inputs and scripts. It is how a game promotes an
asset into a library it cannot write, such as one mounted as a pak. The package
itself is the record of the promotion: it says what left the game, where it
goes in the library and what it was when it left.

**Export** (Content Explorer, on assets or folders of a writable source mount)
collects the selection and its closure. The package root is the deepest
folder that holds all of them; export asks for the folder it lands in, a
library mount name and folder (`/Props/Racer`), and keeps every path below the
root, so `/Content/Levels/Garage/Garage.oscene` and
`/Content/Materials/Rust.omat` become `/Props/Racer/Levels/Garage/Garage.oscene`
and `/Props/Racer/Materials/Rust.omat`. The review lists every asset with its
destination, then export writes one `.zip` file:

| Part           | Contents                                                                                                                                                                                                                                                            |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `package.json` | Format version; the destination; per asset its origin identity in the game (`/Content/Levels/Garage/Garage.oscene`), its destination identity (`/Props/Racer/Levels/Garage/Garage.oscene`), its type and the SHA-256 of each of its files; the dependencies (below) |
| Files          | Every selected asset with its companions, and the closure R3 requires: every asset and file they need from the game's own mounts, at their paths below the package root                                                                                             |
| Sources        | A file reference into another mount (a record's `source` in `RawMedia`) is copied in as a companion and respelled relative, so the package imports and cooks anywhere                                                                                               |
| Dependencies   | Asset references into other mounts (`/Engine/...`, `/Props/...`) stay absolute and are listed by mount and asset path: the package needs them and does not carry them                                                                                               |

Because references inside the root are relative (R2) and identities come from
locations (R4), the files are written as they are; only copied sources are
respelled. References into the destination mount (a game scene naming
`/Props/Furniture/Chair.fbx/...`) stay absolute in the package and are
respelled relative by Import package, as R2 requires inside a mount. A model record keeps its `source_identity`, so the surface overrides
of the packaged scenes still match their models. An asset in a read-only mount
cannot be exported: it has no sources.

**Import package** runs in the project that owns the destination mount, the
library project, and extracts every asset at its destination identity as one
transaction with Undo, respelling references into its own mount relative. It never overwrites: if any destination file exists, or
the destination mount is not the project's own writable mount, the import is
refused and lists why. A dependency the library does not mount is reported as
an unresolved reference, and by R3 a dependency on a game mount cannot occur.

**Replace with library** runs in the game on the package file, once the
library is mounted again (as sources or as a new pak). For each asset in
`package.json`:

| Local asset                                | Library                                              | Result                                                                                   |
| ------------------------------------------ | ---------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| Unchanged since export (its digests match) | Provides the destination identity with the same type | Replaced: every game referrer is rewritten to the destination, the local copy is deleted |
| Unchanged                                  | Does not provide it, or with another type            | Kept, and listed as not in the library                                                   |
| Changed since export, or deleted           | Any                                                  | Kept as it is and listed; it is not replaced                                             |

The review shows the three lists before anything is written, and the replace is
one transaction with Undo. Running it again with the same package after a later
library release replaces what has arrived since; nothing else is stored. An
asset the library team renamed after import shows as not in the library, and
the game's referrers of it are fixed with Retarget (R9).

After a replace the game holds no copy: changes to promoted assets are made in
the library project, which the library team owns. There is no re-promotion
over library assets.

The package format is the editor's; its schema,
`oxygen.asset-package.schema.json`, lives with the editor's other authoring
formats.

## Scenarios

Each row says where the files sit, what Content Explorer shows and what a
relocation does. "As today" means behavior the accepted designs already
define.

### Libraries

| #   | Scenario                                                                                      | Files                                                                                                                                                                                         | Content Explorer                                                                                                                                                                  | Relocation                                                                                                                                                                                                                                                                                      |
| --- | --------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| L1  | A library developer imports `Truck.fbx` from Downloads into `Vehicles/Cars`                   | `Vehicles/Cars/Truck/Truck.fbx` and its bundle, copied; record `Vehicles/Cars/Truck/Truck.fbx.import.json`, `source: "Truck.fbx"`                                                             | In the library project: one `Truck.fbx` Model row; bundle images and buffers as their own rows; the record is not a row                                                           | Rename, move, delete inside the library, as R8                                                                                                                                                                                                                                                  |
| L2  | Racer mounts `Vehicles` as sources (supported, not idiomatic)                                 | Nothing copied; Racer's scenes reference `/Vehicles/Cars/Truck/Truck.fbx/...` absolutely                                                                                                      | Under the `Vehicles` mount: the same rows as in the library project, writable                                                                                                     | Inside `Vehicles`, as R8 with the newer-on-disk guard; across projects, other projects by R9                                                                                                                                                                                                    |
| L3  | Dungeon uses `Props` as a loose cooked root, plain or the project's `.cooked`                 | `//share/oxygen/Props/container.index.bin` and its cooked files                                                                                                                               | Under `Props`: the cooked assets from the index, read-only, pickable                                                                                                              | None: read-only                                                                                                                                                                                                                                                                                 |
| L4  | Racer uses `Props` as a pak                                                                   | `//share/oxygen/Props.pak`, built by PakTool from the `Props` cooked root; the mount is the same `Cooked` mount, bound to the file                                                            | As L3                                                                                                                                                                             | None                                                                                                                                                                                                                                                                                            |
| L5  | Racer switches `Vehicles` between sources, a loose root and a pak                             | `Source` to `Cooked` changes `Kind` in `Project.oxy`; loose root to pak changes only the binding in `.mounts.json`                                                                            | `Vehicles` rows show the same paths in every form; read-only when cooked                                                                                                          | None needed: references are unchanged. Racer records whose source is in `Vehicles` (V3 below) now report `import.source_outside_mount`, because cooked content holds no sources                                                                                                                 |
| L9  | The `Vehicles` project cooks again while Racer is open with `Vehicles` bound to its `.cooked` | The library project writes a new generation and moves `head.json`                                                                                                                             | Racer's `Vehicles` rows update to the new generation's assets                                                                                                                     | Racer's editor swaps the generation and reruns its reference check: a removed asset Racer uses is an unresolved reference (R9). If a pak mounted above depends on a removed asset, the swap is refused, the old generation stays and the mount reports `mount.library_dependency_missing` (R10) |
| L6  | A library with inputs and scripts                                                             | `Vehicles/Input/Car.input.json` names its actions relatively; `Vehicles/Scripts/drive.lua`                                                                                                    | As any asset                                                                                                                                                                      | As R8. Scripts cannot `require` each other (`kUnsupportedRequire`), so a script names no path; scene script slots and parameters are structural references                                                                                                                                      |
| L7  | A loose root cooked with external scripts is bound to a `Cooked` mount                        | Its index holds a script with `external_source_path`                                                                                                                                          | `Props` is `Invalid` with `mount.library_external_script`                                                                                                                         | None                                                                                                                                                                                                                                                                                            |
| L8  | A library cooked from a mount named `Content`                                                 | Its index holds `/Content/...`                                                                                                                                                                | Declared as `Props`: `Invalid`, `mount.library_root_mismatch`. Declared as `Content`: refused, `mount.name_duplicate`                                                             | None                                                                                                                                                                                                                                                                                            |
| L10 | The `Props` library uses `Vehicles` assets                                                    | The `Props` project mounts `Vehicles` as `Cooked`; its scenes reference `/Vehicles/...` absolutely; `Props.pak` is packed with `--dependency-pak Vehicles.pak` and records what it uses (R10) | A game that mounts `Props` must mount `Vehicles` below it; without it, or with a `Vehicles` that lacks a used asset, `Props` is `Invalid` with `mount.library_dependency_missing` | None: read-only in the game                                                                                                                                                                                                                                                                     |

### Raw media with per-project settings

| #   | Scenario                                                                                                                   | Files                                                                                                                    | Content Explorer                                                                                               | Relocation                                                                       |
| --- | -------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| V1  | Racer imports `D:/Media/Raw/Cars/Truck.fbx` (4 GB) into `/Content/Cars`                                                    | Nothing copied. Record `Racer/Content/Cars/Truck.fbx.import.json`, `source: "/RawMedia/Cars/Truck.fbx"`                  | Under `RawMedia`: `Truck.fbx` as a raw source row with Import. Under `Content/Cars`: the `Truck.fbx` Model row | Moving or renaming Racer's model moves only the record; the raw file never moves |
| V2  | Dungeon imports the same file with other settings                                                                          | Record `Dungeon/Content/Vehicles/Truck.fbx.import.json`, its own settings and `source_identity`                          | As V1, in Dungeon                                                                                              | Independent of Racer                                                             |
| V3  | With `Vehicles` mounted as sources only: Racer wants other settings for the library model `/Vehicles/Cars/Truck/Truck.fbx` | Record `Racer/Content/Cars/TruckLow.fbx.import.json`, `source: "/Vehicles/Cars/Truck/Truck.fbx"`; nothing copied         | Both models: the library's under `Vehicles`, Racer's under `Content`                                           | Racer's record moves alone; the library's companion stays                        |
| V4  | Two settings of one raw file in one game                                                                                   | `Content/Cars/Truck.fbx.import.json` and `Content/Cars/TruckLod.fbx.import.json`, both naming `/RawMedia/Cars/Truck.fbx` | Two Model rows                                                                                                 | Each independently                                                               |
| V5  | `RawMedia` is unbound on a new developer's machine                                                                         | Records committed, source absent                                                                                         | `RawMedia` shows `Unbound` with Bind; the models show their source as unbound                                  | Allowed: no relocation reads the source                                          |
| V6  | The raw file changes on disk                                                                                               | The record's fingerprint no longer matches                                                                               | Every project that names it shows the model stale, as today                                                    | None                                                                             |

### Downloads

| #   | Scenario                                                           | Files                                                                                                                        | Content Explorer                                                                 | Relocation |
| --- | ------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- | ---------- |
| D1  | Import `Downloads/Lamp.glb` into `/Content/Props` as `Lamp`        | `Content/Props/Lamp/Lamp.glb` and its bundle, copied; record `Content/Props/Lamp/Lamp.glb.import.json`, `source: "Lamp.glb"` | One `Lamp.glb` Model row; Downloads is never shown                               | As R8      |
| D2  | Import `Downloads/Brick.png` as a texture into `/Content/Textures` | `Content/Textures/Brick.png` copied; `Content/Textures/Brick.otex.json`, `source: "Brick.png"`                               | One `Brick.otex` Texture row; `Brick.png` is its companion, not a row of its own | As R8      |
| D3  | A model needs a file outside its folder                            | Refused with `DependencyOutsideSource`, as today; a missing file is skipped and reported                                     | Nothing is added                                                                 | None       |

### Promotion

The idiomatic promotion goes through a package (P4 to P9) and works the same
for a library mounted as a loose cooked root or a pak. A library mounted as a
writable source can also take a direct move (P1 to P3).

| #   | Scenario                                                                                                          | What happens                                                                                                                                                                                       | References                                                                                                                          |
| --- | ----------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| P1  | Move Racer's downloaded `Content/Props/Lamp` folder to `Vehicles/Props`                                           | The record and its companion bundle move                                                                                                                                                           | Products become `/Vehicles/Props/Lamp/Lamp.glb/...`; Racer's scenes are respelled absolute; `source_identity` and overrides survive |
| P2  | Move the material `Content/Materials/Rust.omat.json` to `Vehicles`                                                | The material and, by R3, the textures and images it names in `Content` move                                                                                                                        | Relative inside the moved set; Racer's other referrers become absolute `/Vehicles/...`                                              |
| P3  | Move Racer's model V1 (source in `RawMedia`) to `Vehicles`                                                        | The record moves; the review offers to copy the source in as a companion, or to keep `/RawMedia/...` and make `Vehicles` need `RawMedia`                                                           | As P1                                                                                                                               |
| P4  | Promote Racer's `Content/Levels/Garage` scene, with its models, materials, inputs and scripts, to `/Props/Racer`  | Export writes `Garage.zip`: the scene, its closure from `Content` under the root `/Content`, the records with their sources copied in, the digests, and the dependencies on `/Engine` and `/Props` | Unchanged in Racer                                                                                                                  |
| P5  | The `Props` team imports `Garage.zip`                                                                             | Every asset lands at its destination under `Props/Racer/`; an existing file refuses the whole import                                                                                               | Relative references are unchanged; references into `/Props` become relative; models keep their settings and `source_identity`       |
| P6  | The `Props` project is cooked and packed; Racer's binding picks up the new `Props.pak`                            | Racer's local `Garage` and the library's `/Props/Racer/Levels/Garage` both exist                                                                                                                   | Racer's references into `Props` still resolve                                                                                       |
| P7  | **Replace with library** on `Garage.zip`                                                                          | Unchanged assets the library provides are replaced and deleted locally; the others are listed and kept                                                                                             | Racer's references become `/Props/Racer/...`. Undo restores the local copies and the references                                     |
| P8  | The `Props` team removed one imported material before packing, or a developer edited a local texture after export | P7 lists the material as not in the library and the texture as changed since export; both stay local; the library scene uses the library's assets                                                  | Racer keeps its references to the two local assets until a later package, or the user deletes them                                  |
| P9  | Export an asset from a pak-mounted library                                                                        | Refused: a read-only asset has no sources                                                                                                                                                          | None                                                                                                                                |

### Shipped libraries

A library is developed as an ordinary project and shipped as a pak: a
self-contained bundle that any game mounts, whether it was built in the studio
or downloaded from a web site. Nothing about the pak depends on where it came
from or where it is stored.

| #   | Scenario                                                                                   | Files                                                                                                                            | Content Explorer                                                                                                                                        | Result                                                                                                                                                                                                                           |
| --- | ------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| S1  | The `Props` library is shipped                                                             | `Props.pak`, built from the project's cooked `Props` root by PakTool, with its browse index; scripts sealed                      | None: the pak is a file                                                                                                                                 | It holds `/Props/...` only, its catalog lists the layers it depends on (`engine.pak`, and any library pak it uses), and it passes R6                                                                                             |
| S2  | A developer downloads `Props.pak` and adds it to Racer                                     | Kept in the repository (`Racer/Paks/Props.pak`, committed path) or anywhere on the machine (placeholder bound in `.mounts.json`) | **Add library** picks the file; the mount name is read from the pak's one root, never typed. The `Props` mount lists its assets, read-only and pickable | `Project.oxy` gains `{ "Name": "Props", "Kind": "Cooked", "Path": "Paks/Props.pak" }`, placed above the layers it depends on                                                                                                     |
| S3  | Racer uses a pak asset                                                                     | Racer's scenes reference `/Props/Furniture/Chair.fbx/Chair.ogeo` absolutely                                                      | As any asset                                                                                                                                            | Cooks and runs from the pak; nothing is recooked                                                                                                                                                                                 |
| S4  | A new `Props.pak` no longer provides an asset Racer uses, or provides it with another type | As S2                                                                                                                            | Racer's references to it are unresolved (R9); a library pak above `Props` that uses it is `Invalid` with `mount.library_dependency_missing`             | When Racer is packed, its pak records what it uses; the Player refuses to mount it on a `Props.pak` that lacks one (R10)                                                                                                         |
| S5  | The pak was built by an engine whose pak format this engine cannot read                    | As S2                                                                                                                            | `Invalid`, `mount.library_format_unsupported`                                                                                                           | Rebuilt from the library project with this engine                                                                                                                                                                                |
| S6  | Two libraries each have a `Chair`                                                          | `Props.pak` holds `/Props/Furniture/Chair.fbx`, `Interiors.pak` holds `/Interiors/Furniture/Chair.fbx`                           | Two assets under two mounts                                                                                                                             | Library names are unique, and a library's name is the namespace of its assets: the paths, and the `AssetKey`s hashed from them, differ. A second mount of the same root is the same library, refused with `mount.name_duplicate` |
| S7  | A new `Props.pak`, or a `Props` patch pak, replaces or extends the old one                 | Same path, or a patch mounted above `Props.pak`                                                                                  | Assets the new content adds, changes or removes show as such; removed ones are unresolved references for Racer (R9)                                     | Racer's pak keeps mounting while every asset it uses is still provided with its type (R10); only removing or retyping one of them requires rebuilding it                                                                         |
| S8  | Racer ships                                                                                | Its own content pak and every library pak it mounts                                                                              | None                                                                                                                                                    | The Player launch file mounts them in the game's priority order; each pak's dependencies are below it                                                                                                                            |
| S9  | Racer is packed while `Vehicles` is bound to its `.cooked`                                 | Nothing is written                                                                                                               | The pak build fails with `pak.dependency_not_pak`, naming `Vehicles`                                                                                    | The author packs `Vehicles`, binds Racer's `Vehicles` to the pak (L5, no `Project.oxy` change) and packs again; a build machine binds every library to its pak                                                                   |

### Everyday operations

| #   | Scenario                                     | Result                                                                                                                                                    |
| --- | -------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| E1  | Rename `Lamp.glb` to `Lantern.glb`           | `Lamp.glb` and `Lamp.glb.import.json` become `Lantern.glb` and `Lantern.glb.import.json`; products and referrers follow; overrides survive; Undo restores |
| E2  | Rename a model whose source is in `RawMedia` | The record alone is renamed                                                                                                                               |
| E3  | Copy and paste `Lamp` into another folder    | A new record with a new `source_identity` and a copy of the companion bundle; the original's referrers are unchanged                                      |
| E4  | Duplicate a model whose source is shared     | A new record naming the same source: a second set of settings, nothing copied                                                                             |
| E5  | Delete a model                               | The record and the companions no other asset names are recycled; a shared source is never deleted; Undo restores                                          |
| E6  | A model file renamed outside the editor      | The record names a missing companion: the model reports its source missing, as any missing source; the renamed file is a raw source row with Import       |
| E7  | Record deleted outside the editor            | The model and its products are gone; referrers report unresolved references; the source shows as a raw row                                                |
| E8  | Undo a promotion                             | Moves everything back and restores every respelled reference, as one transaction                                                                          |

## Proof

| Need | Satisfied by                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| N1   | Committed files hold no absolute path and no machine fact: references are relative or mount paths (R2), identities are locations (R4), records sit in their mounts (R5). Placeholders are bound per machine, once for every game, by a parent `.mounts.json`, and the binding picks a library's loose root or pak, so developers and the build machine share one `Project.oxy` (R6). Library and game repositories are independent: a game commits only references into the library's mount name                                      |
| N2   | Source sharing: R2 to R4 make a library folder resolve the same in every project, and R3 keeps it free of game references (L2, L6). Cooked sharing: R6 fixes one root per library and checks it, so paths are the same in every form and switching forms rewrites nothing (L3 to L5, L7 to L10). Inputs and scripts follow the same references (L6); a library may use another library (L10)                                                                                                                                          |
| N3   | A pak is self-contained by R6: one root, a browse index, sealed scripts, and the assets it uses from other layers (R10). Adding it reads its name and checks its dependencies and format (S2, S4, S5), so where it came from does not matter; a game references it like any library, ships it with its own pak, and survives library updates and patches that keep what it uses (S3, S7, S8)                                                                                                                                          |
| N4   | R5 lets any number of records name one source by mount path, each in its own project's mount with its own settings and `source_identity`; nothing is copied (V1 to V4), and import always writes into the importing project's own mount (R8). A library model gets per-game settings the same way (V3)                                                                                                                                                                                                                                |
| N5   | Import copies a source that is in no mount into the game's own mount as the asset's companion (D1, D2); it is the game's alone, because own mounts are not shared (R1)                                                                                                                                                                                                                                                                                                                                                                |
| N6   | A library mounted as a writable source takes the asset by an ordinary move with the closure R3 requires (P1 to P3). Any library takes it as a package: export carries the asset, its closure and its sources with relative references intact and records origin, destination and digests; import lands it unchanged at its destination without overwriting anything; Replace with library switches the game to exactly the assets the library now provides and that were not changed since export, and lists the rest (R11, P4 to P9) |
| N7   | R8 and R9: companions follow their asset, references are respelled by R2, products follow containers, `source_identity` keeps surface overrides, and one transaction gives Undo and Redo (E1 to E8). Read-only mounts refuse every write (R7)                                                                                                                                                                                                                                                                                         |

The contract does not reach projects that are not open: a rename or delete
in a shared library breaks other games' references, which R9 makes visible
at their next open and repairable one by one, without storing anything.

## Today

| Reference                           | Where it is resolved                                                                     | Form today                                                                               |
| ----------------------------------- | ---------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| Material `textures.*.virtual_path`  | `TextureReferenceResolver.cpp`; editor `CookSourceAnalyzer.ResolveTextureDescriptorPath` | Absolute only; the editor looks the mount up by name                                     |
| Scene descriptor references         | `SceneDescriptorImportJob.cpp`                                                           | Absolute only; `canonical_*_path` schema patterns refuse `.` and `..`                    |
| Geometry, physics, input references | Their import jobs and request builders                                                   | Absolute only                                                                            |
| Editor scene documents              | `SceneReferenceFields`, `AssetPathData`                                                  | Absolute `asset:///Mount/...` URIs                                                       |
| Texture and buffer file references  | `MountTable::LocateReference`                                                            | Mount path or relative: already portable                                                 |
| Repeated identity `virtual_path`    | Texture `identity_conflict`, physics material job; editor `AssetFileType.IdentityField`  | Refused when it differs from the job's identity: wrong as soon as the mount name differs |
| Model import record                 | `<ProjectRoot>/Imports/<virtual path>.import.json`                                       | Outside every mount; `source` and `virtual_path` spell the mount name                    |
| Cooked and pak mount names          | Not checked                                                                              | A declared name may differ from the paths inside                                         |

In F1 Validation, which mounts `Examples/Showcase/Content` as `Showcase`, 11
materials name `/Content/...` textures and the cook fails with "texture
descriptor is missing" without saying where it looked. The Showcase content
holds 42 absolute `virtual_path` and 22 absolute `source` values that name
`/Content`.

## Other engines

| Engine | Stored form                                       | Content under another name or place                                |
| ------ | ------------------------------------------------- | ------------------------------------------------------------------ |
| USD    | Anchored `./` and `../` paths, resolved per layer | Works: a tree is relocatable whatever its root is called           |
| Bevy   | `source://path`; loaders resolve relative paths   | Relative references survive; a renamed source breaks absolute ones |
| Unreal | Absolute `/Plugin/...` paths                      | Breaks; redirectors and resaves repair renames                     |
| Godot  | `res://` path plus `uid://`                       | Survives through the UID                                           |
| Unity  | GUIDs in `.meta` sidecars                         | Survives while the `.meta` files travel with the assets            |
| O3DE   | Asset ids, scan-folder-relative source paths      | Survives inside a gem; gems never reference project content        |

Only relative text paths survive a rename without a second identity. Unity and
Godot keep import settings beside the asset; O3DE gems never reference project
content, which is R3.

## Engine

| Area                                        | Change                                                                                                                                                                                                                                                                                                                        |
| ------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Content/VirtualPath.h`                     | `ResolveReference(anchor, reference)` and `RelativeReference(anchor, target)`, with `kReferenceOutsideMount` and `kReferenceSelfAbsolute`                                                                                                                                                                                     |
| `Content/VirtualPathCases.json`             | A `references` table: anchor, reference, expected path or error. The engine and `Oxygen.Managed.Core` run it                                                                                                                                                                                                                  |
| `Content/Docs/virtual_paths.md`             | A "References" section                                                                                                                                                                                                                                                                                                        |
| Import jobs                                 | Material, scene, geometry, physics and input jobs resolve each reference with the job identity before `ResolveMountedAssetReference`, and report the written and resolved forms                                                                                                                                               |
| Buffer containers                           | Products resolve relative to the container                                                                                                                                                                                                                                                                                    |
| Texture and physics material jobs           | The in-file identity checks are removed                                                                                                                                                                                                                                                                                       |
| `ModelImportRecord.cpp`                     | The record is the job's authored file: its identity is its path minus `.import.json`; `source` goes through `LocateReference`; `Imports/` scanning is removed                                                                                                                                                                 |
| `SourceMounts` (`ResolveMountTable`)        | `Pak` merged into `Cooked`, read by its binding's target; the `Cooked` library checks and the `Invalid` state (R6)                                                                                                                                                                                                            |
| Pak and loose-root reading for mounts       | The root, browse index, format version and catalog dependencies of a library, read without loading it; dependency checks follow R10; the engine never reads a `.cooked` folder                                                                                                                                                |
| `PakCatalog`, PakTool planner, `AddPakFile` | Dependencies become the library's root name plus required asset, texture and physics resource keys with their types; the catalog names its own library and always has a browse index; the planner writes them; mounting and refreshing check them through `PatchResolutionPolicy` (R10); the catalog format version is raised |
| Schemas                                     | A `reference` definition replaces `canonical_*_path` in reference fields; identity `virtual_path` properties and the record's `source` pattern and `virtual_path` are removed                                                                                                                                                 |

## Editor

| Area                                                       | Change                                                                                                                                                                                              |
| ---------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Oxygen.Managed.Core` `AssetPath`                          | `ResolveReference(anchor, text)` and `ToReference(anchor)`, running the shared cases                                                                                                                |
| `Oxygen.Editor.Projects` mounts                            | Own and shared roles on `ResolvedMount`; `Pak` merged into `Cooked`; the library checks and `Invalid` state in `MountTableBuilder`; the pak build's check that every library binding is a pak (R10) |
| `Oxygen.Managed.Assets` readers, writers, `AssetFileTypes` | Readers resolve with the file's identity, writers emit `ToReference`; `IdentityField` removed; the model record is the model's authored file and the companion rule (R5) is part of the type table  |
| Scene document serialization (`Oxygen.Editor.World`)       | Reference fields keep the written text; the reader resolves with the scene's identity, the writer respells                                                                                          |
| `CookSourceAnalyzer`, `CookInputResolver`, discovery       | Work on resolved references; diagnostics carry the written text, the resolved path and the file looked up                                                                                           |
| Relocation (`AuthoredReferenceFormat`, planner)            | R8: companions, respelling, the shared-mount closure and the R3 refusal; copied records get a new `source_identity`                                                                                 |
| Asset packages (`Oxygen.Editor.ContentPipeline`)           | Export, Import package and Replace with library (R11), as relocation transactions; `oxygen.asset-package.schema.json`; Content Explorer commands on assets and folders                              |
| `ReferenceCheck`                                           | Resolves relative references; reports `reference.into_project_mount` and the R2 refusals; Retarget on an unresolved reference (R9); `.build/references.json` records written and resolved forms     |
| Model import (`SceneImportRequest`, intake)                | Destination is one of the project's own mounts; a source in a mount is referenced, a source in no mount is copied as the companion                                                                  |
| Content Explorer                                           | Raw source rows in shared mounts with Import; the record and companion are never rows of their own; the `Invalid` mount state                                                                       |
| Add library                                                | Picks a `.pak`, a cooked root or a `.cooked` folder, names the `Cooked` mount from its root, places it above its dependencies, writes the committed path or the placeholder and binding             |

## Migration

Existing content is migrated by editing its data, with no reader, converter or
repair of the old forms in the engine, the tools or the editor, as for
`Project.oxy` version 1. Old files fail with the ordinary errors of the new
contract.

A one-time script, kept outside the product, rewrites each project:

1. respells every authored reference by R2, resolved against the project's own
   mount table as its author meant it;
2. removes identity `virtual_path` fields and makes buffer products relative;
3. moves each `Imports/<Mount>/<path>.import.json` to `<mount folder>/<path>.import.json`,
   removes `virtual_path` and respells `source` by R2, then deletes `Imports/`;
4. respells scene documents' references;
5. reports every file it rewrote and every reference it could not resolve.

| Project                                    | Mounts                 | Notes                                                                                                                                                                                                                                                                                                                               |
| ------------------------------------------ | ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `projects/Oxygen.Engine/Examples/Showcase` | `Content` → `Showcase` | Becomes a library project (R6): its mount is renamed `Showcase`, identities become `/Showcase/...`, and its import manifests follow. Its 11 records name sources in `SourceMedia/DCC`, respelled relative (`../SourceMedia/DCC/Lantern.glb`)                                                                                        |
| `Oxygen Projects/BurgerPiz`                | `Content`              | 1 record; stays a game: nothing mounts this project                                                                                                                                                                                                                                                                                 |
| `Oxygen Projects/Complex Import`           | `Content`              | 2 records                                                                                                                                                                                                                                                                                                                           |
| `Oxygen Projects/F1 Validation`            | `Content`, `Showcase`  | 3 records; its `Showcase` mount names the Showcase library folder, which now matches its library name. Its two `asset:///.cooked/...` scene references name an output folder and are fixed by hand                                                                                                                                  |
| `Oxygen Projects/Instancing`               | `Content`, `BurgerPiz` | 6 records. Its `BurgerPiz` mount is a raw media folder (`F:/projects/BurgerPiz`), not the BurgerPiz project. The record of `/BurgerPiz/Models/BurgerPiz.glb` moves into Instancing's own mount as `/Content/BurgerPiz/BurgerPiz.glb`, naming its source `/BurgerPiz/Models/BurgerPiz.glb` (V1), and the one scene reference follows |
| `Oxygen Projects/Test`                     | `Content`              | 1 record                                                                                                                                                                                                                                                                                                                            |
| `Oxygen Projects/Vortex`                   | `Content`              | No records                                                                                                                                                                                                                                                                                                                          |

`Oxygen Projects` is `%LOCALAPPDATA%/DroidNet/Oxygen Editor/Oxygen Projects`.
Libraries are migrated before the projects that mount them, and every project
is then cooked. Five old editor sidecars already sit at new record paths and
three legacy material sidecars end with `.import.json`; they are a retired
format and are deleted. F1 Validation references no Showcase asset, so it
cannot check the library scenarios; the `Examples/Studio` workspace of the
[design](design.md#studio-example) does. No project in `Oxygen Projects` declares a `Pak` mount; the
script rewrites any it finds to `Cooked`.

Every pak is rebuilt with the new catalog format, `engine.pak` and the example
paks included; no reader of the old catalog is kept.

## Tests

| Test                                                                                                                                                                                                                                                                                                               | Home                                                                                           |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------- |
| Reference grammar, resolution and refusals                                                                                                                                                                                                                                                                         | `VirtualPathCases.json`: `VirtualPath_test.cpp`, `VirtualPathCasesTests.cs`                    |
| A library cooks the same under its own project and mounted in a game                                                                                                                                                                                                                                               | Cooker import tests; `CookSourceAnalyzer` integration tests                                    |
| Buffer products and geometry references relative to the container                                                                                                                                                                                                                                                  | Cooker geometry and buffer container tests                                                     |
| A descriptor or record with an identity field is refused                                                                                                                                                                                                                                                           | Cooker schema tests                                                                            |
| A record names a companion, a source in another mount, or one in no mount                                                                                                                                                                                                                                          | `ModelImportRecord` tests; editor model import tests                                           |
| Library checks and `Invalid` state for every finding of R6                                                                                                                                                                                                                                                         | `MountTableBuilder` tests; engine `SourceMounts` tests with cooked and pak fixtures            |
| Add library names and orders a pak mount; a downloaded pak with a missing dependency or an unreadable format is `Invalid`                                                                                                                                                                                          | ContentExplorer UI tests; `MountTableBuilder` tests with pak fixtures                          |
| R10: a library update or patch that keeps the used assets mounts; one that removes or retypes a used asset is refused, at mount and at a lower-layer refresh                                                                                                                                                       | `PakCatalog` tests; `Oxygen.Content` pak mount tests with base, patch and dependency fixtures  |
| A `Cooked` mount reads a loose root, a `.cooked` folder or a pak by its binding; a `.cooked` binding follows `head.json`, swaps generations and keeps the old one when a pak above would break; packing with a library bound to a loose root fails with `pak.dependency_not_pak` in the editor before PakTool runs | Editor mount tests with publication fixtures; `Oxygen.Content` generation tests; PakTool tests |
| Retarget repoints one unresolved reference, with Undo                                                                                                                                                                                                                                                              | `ReferenceCheck` and Problems tests                                                            |
| Own and shared roles; `reference.into_project_mount`                                                                                                                                                                                                                                                               | `MountTableBuilder` and `ReferenceCheck` tests                                                 |
| Every row of R8, including the shared-mount closure and Undo                                                                                                                                                                                                                                                       | `AssetRelocationTests`, `SharedMountTests`                                                     |
| Export closure, copied sources, digests and dependencies; import refuses any existing file; Replace with library for each row of its table, with Undo                                                                                                                                                              | ContentPipeline asset package tests; ContentExplorer UI tests                                  |
| Content Explorer rows for records, companions and raw sources in shared mounts                                                                                                                                                                                                                                     | ContentExplorer unit tests                                                                     |
| Writers spell same-mount references relative and others absolute                                                                                                                                                                                                                                                   | Asset writer and scene document serialization tests                                            |

## Decisions

- **References inside a mount are relative to the referring asset.** The only
  text-path form that survives any mount name and any copy of a folder; file
  references already work this way.
- **The anchor is the referrer's virtual path,** so containers, records and
  documents resolve the same way, and downstream sees canonical paths only.
- **One spelling per case.** Same mount relative, other mounts absolute; an
  absolute self-reference is refused where it is written.
- **Identity is never repeated.** The location is the only identity.
- **Every imported asset is one authored file at its identity.** Textures and
  models follow one rule; the source is a companion or a reference, so per
  project settings need no copies and shared settings travel with the asset.
- **Shared content never references a game.** Libraries stay readable by every
  game, as O3DE gems are.
- **A library has one name, the name of its mount.** Cooked paths are fixed at
  cook time; one name lets a game switch kinds with no rewrite, and the checked
  root keeps libraries from masking a game's assets.
- **A pak is the shippable library.** Its name, dependencies and format are
  read from the pak itself when it is added, so a downloaded pak and a studio
  pak are mounted the same way.
- **A pak depends on the assets it uses, not on its dependency's bytes.** The
  game's cooked data holds only keys of library assets, so an exact digest
  forced rebuilds for every library change while letting a patch remove a used
  asset unchecked; checking used keys and types against the resolved view does
  neither. Patch bases stay exact, because a patch is a difference from bytes.
- **A library is identified by its root name.** Loose generations get a fresh
  `SourceKey` on every cook, so only the root name identifies a library the same
  way as a loose root and as a pak; a `.cooked` binding follows the library's
  publications, so loose libraries update as paks do.
- **One cooked kind; the binding picks loose or pak.** `Kind` is committed
  and the same for every developer, while the form of a library is a machine
  fact: a library developer reads a local cook, others and the build machine
  read the pak. A separate `Pak` kind forced a committed change to pack.
- **Paks are packed only against library paks.** A shipped game must depend on
  the exact bundles it ships with; the editor refuses a loose library mount
  before PakTool runs, and PakTool only ever reads dependency paks.
- **Import writes into the project's own mounts.** Settings in a shared mount
  would be imposed on every game; a library's settings are made in its project.
- **No new tools.** A library is an editor project; cooking and packing are the
  existing editor cook, ImportTool and PakTool.
- **Promotion travels as a package, and the package is its record.** It
  names origin, destination and digests, so Replace with library replaces
  exactly what arrived unchanged and lists the rest; no state is kept in the
  game or the library. A package never overwrites library assets: promoted
  assets are changed in the library project after the replace.
- **No legacy code.** Old content is migrated as data.
- **Other projects are reported, not redirected.** A library rename breaks
  other games' references visibly, and Retarget repairs each one. A redirect
  table in the library was rejected: it only grows and every reader would
  carry it forever. Refusing renames from games was rejected: it reduces the
  surprise, not the breakage.
- **Rejected: a mount-root form (`~/Textures/Brick.otex`).** It does not
  survive copying a subfolder into another mount, and adds a second spelling.
- **Rejected: GUID identities.** The path is the identity and `AssetKey`
  derives from it; a second identity needs sidecars and a redirect store.
- **Rejected: aliasing a mount's authoring name in `.mounts.json`.** It keeps
  the author's name inside the content and collides with the game's mounts.

## Accepted designs this touches

| Document                                                                                                                 | Change                                                                                                                                                            |
| ------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [Source mounts](../source-mounts/README.md)                                                                              | Naming rules, "Changing a mount", `Pak` merged into `Cooked` (kinds, `mount.path_kind`, `.mounts.json`), descriptor references, records in mounts, library checks |
| `src/Oxygen/Content/Docs/virtual_paths.md`                                                                               | References section                                                                                                                                                |
| `src/Oxygen/Content/Docs/patch_resolution_contract.md`, `src/Oxygen/Cooker/Tools/PakTool/README.md`, `Data/PakCatalog.h` | Dependency requirements (R10) replace exact dependency identities; patch bases unchanged                                                                          |
| `src/Oxygen/Cooker/Tools/ImportTool/README.md`                                                                           | Records as authored files; buffer products relative                                                                                                               |
| `design/editor/lld/asset-identity.md`                                                                                    | 5.4 References, 7.1 Identity spellings, 7.4 Model import settings, 7.6 Identity and reference fields                                                              |
| `design/editor/lld/project-layout-and-templates.md`                                                                      | Own and shared mounts, library projects without `Content`, `Invalid` state, `Imports/` deleted, records                                                           |
| `design/editor/lld/content-pipeline.md`                                                                                  | 7.4 Engine Content References, 8.4 Asset Relocation, 8.6 Reference Check; model records and the stale "bundles under `Content/SourceMedia/DCC`" rule              |
| `design/editor/lld/content-explorer-asset-identity.md`                                                                   | 9.5 Asset Relocation (R8), raw source rows, records and companions                                                                                                |

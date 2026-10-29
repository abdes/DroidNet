# Source mounts

Status: proposal. Nothing here is implemented, scheduled or accepted. It is
written against the engine and tools as they are today and changes no accepted
document.

Last updated: 2026-10-29

Read: [at a glance](#at-a-glance), [scenarios](#scenarios),
[today](#today), [the mount contract](#the-mount-contract),
[the `.mounts.json` file](#the-mountsjson-file),
[recorded source identity](#recorded-source-identity),
[tools and runtime](#tools-and-runtime),
[the examples project](#the-examples-project), [decisions](#decisions),
[accepted designs this touches](#accepted-designs-this-touches). Editor impacts
are in
[editor source mounts](../../../../../design/editor/proposals/source-mounts/README.md).

## At a glance

| Question          | Answer                                                                                                                                                                                                                             |
| ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| What is it?       | Named mounts as the one way every import, cook, pak and runtime path finds source files: `/<Mount>/<path in mount>`, with the folder behind each name declared once and overridable locally.                                       |
| Why?              | Source locations are guessed today: from the output's folder, a manifest root, a script source root, a record's folder or an absolute path. Two files can pass for one, and paths break across machines and drives.                |
| Declared where?   | Committed: one mount list in `Project.oxy` or the import manifest, with relative paths or placeholders, never absolute paths. Local: git-ignored `.mounts.json` files, found up the folder hierarchy, where absolute paths belong. |
| What it fixes     | abdes/DroidNet#81 (sources taken for one producer); shared and external content on any drive; projects cloned on another machine; `import-sources.local.json` and script source roots.                                             |
| Examples          | `Examples/Content` becomes an ordinary Oxygen project in the standard layout, with one `Content` mount; see [the examples project](../examples-project/README.md).                                                                 |
| Not in the design | Cooked-content mounting (`AddLooseCookedRoot`, paks), which already works by cooked index; converting the examples' scene descriptors into editor scene documents.                                                                 |

## Scenarios

**A studio library on another drive.** A studio keeps shared materials and
textures in `D:\Studio\SharedContent`. Project A declares a mount `Studio` in
`Project.oxy` with no path, a placeholder. Each developer's `.mounts.json` binds
`Studio` to their own copy: `D:\Studio\SharedContent` on one machine,
`E:\Libraries\Studio` on another, `/mnt/studio` on the build agent. A
developer with ten projects under `H:\projects` writes the binding once, in
`H:\projects\.mounts.json`, and every project below finds it. A material
of project A references `/Studio/Materials/Steel.omat`. It cooks and plays on
every machine, and the cooked index records the source as
`/Studio/Materials/Steel.omat.json` everywhere.

**Media elsewhere in the repository.** A project keeps its heavy raw media in
`Media/Showcase` at the repository root, outside the project tree. It commits
the mount `Showcase -> ../../Media/Showcase`, a relative path, so a fresh clone
cooks without any local file. A developer who keeps a larger, unversioned set
on another drive overrides `Showcase` in the project's own `.mounts.json`.

**Engine examples, one or all.** An engine developer cooks the `BottleOnBox`
example alone, then all examples in one batch. Four examples use the same cube
and floor from `/Content/Common`. In the batch, each shared file is cooked once
and recorded once; a second cook of it is a re-cook, never a collision. Another
example that ships its own `floor.omat.json` in its own folder has a different
mount path, so cooking it to the same identity is refused.

**A cloned project with an unbound mount.** A new team member clones project A
and opens it before creating `.mounts.json`. The project opens. The `Studio`
mount shows as unbound. Cooked content and paks from `Studio` still load,
because they are addressed by name. A cook that needs a `Studio` source fails
with `import.mount_unbound`, naming the mount and the `.mounts.json` to create.

**Engine content.** The engine content build writes its generated sources into
the build tree and cooks them as the `Engine` mount. Nothing about it is
machine-specific, and nothing of it is configured by users.

## Today

| Place                  | How a source location is found today                                                                                   | Problem                                                                           |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| Loose cooked index     | `ImportRequest::SourceVirtualPath()`: the output identity's folder plus the source file's name                         | Two files with one name pass for one source (#81)                                 |
| Import manifest        | Job `source` and model `record` resolve against `root` (`--root`, else the manifest folder; `ImportManifest.cpp:1306`) | Unnamed; relative `../` webs between manifests                                    |
| Manifest script jobs   | `script.source_root`, against the manifest folder                                                                      | A second root concept                                                             |
| Single import commands | Positional source against the working directory; `--content-root` for model records                                    | No identity beyond the file path                                                  |
| Model import records   | `source` relative to the record's folder inside the content root, else absolute (`ModelImportRecord.cpp:602`)          | Absolute, machine-specific paths in committed records                             |
| External scripts       | `external_source_path` relative to the script source root (`ScriptImportPipelineCommon.h:57`)                          | Resolved at pak time by `--script-source-root`, at runtime by `ScriptSourceRoots` |
| PakTool script sealing | `--script-source-root`, first root that holds the file (`ScriptSealing.cpp:222`)                                       | A third root list, unnamed                                                        |
| Runtime and Player     | `PathFinderConfig::WithScriptSourceRoots`, launch file `script_source_roots` (`LaunchFile.cpp:116`)                    | Unnamed roots, searched in order                                                  |
| Examples               | `import-sources.local.json`: names to absolute files, consumed only by `import_models.ps1`                             | A private, examples-only mechanism                                                |

No engine tool or runtime component names source folders today. Cooked
content is different: the runtime mounts cooked indexes and paks
(`VirtualPathResolver`, `ContentMounts.h`) and resolves virtual paths through
them. That mechanism is unchanged here.

## The mount contract

### A mount

A mount is a name and a folder. For a `Source` mount, its name is the first
segment of every path in it: the file `<folder>/Materials/Red.omat.json` of
mount `Content` is `/Content/Materials/Red.omat.json`. The name is the
identity: content, cooked indexes and references store names, never folders.
A `Cooked` or `Pak` mount is different: its name labels the cooked root or pak
in the table and in a content priority order, and the virtual paths inside it
are the ones that were cooked into it. Tools and the runtime mount it through
the cooked-content mechanism that is unchanged here; no source is ever read
from it.

A mount has three kinds of facts: what is **declared** in files, what is
**calculated** from them for one run, and the **rules** a run checks.

#### Declared

| Property | Declared in                                     | Contract                                                                                                                                                         |
| -------- | ----------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Name`   | The committed list; `.mounts.json`; `--mount`   | `^[A-Za-z][A-Za-z0-9_-]{0,63}$`                                                                                                                                  |
| `Kind`   | The committed list only                         | `Source` (a folder of source files, the default), `Cooked` (a cooked folder) or `Pak` (a `.pak` file). A mount that only `.mounts.json` adds is a `Source` mount |
| `Path`   | The committed list                              | Relative to the project root, and may leave it (`../../Media/Showcase`). Absent for a placeholder. Never absolute                                                |
| `Path`   | `.mounts.json`                                  | Absolute, the usual form, or relative to that file's folder                                                                                                      |
| `Path`   | `--mount <Name>=<dir>` (single import commands) | Absolute, or relative to the working directory                                                                                                                   |

The committed list is the `Mounts` list of `Project.oxy` in the editor and the
`mounts` of an import manifest for tools. A `Cooked` or `Pak` mount whose
location is local is declared there as a placeholder and bound in
`.mounts.json`.

#### Calculated

Calculated values exist only while a run lasts. They are logged, never
stored.

| Value               | How                                                                                                                                                                                 |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Folder              | The `Path` from the first [source that provides it](#where-mounts-come-from), made absolute against the folder of the file that holds it, then made [canonical](#canonical-folders) |
| Origin              | Which file or option gave the folder: `--mounts`, a named `.mounts.json`, the committed list, or none                                                                               |
| State               | `Bound`; `Unbound` (a placeholder nothing binds); `Missing` (a folder that does not exist)                                                                                          |
| A file's mount path | `/<Name>/<the file's path below the Folder>`, computed from the file's canonical path; a file below no Folder has none                                                              |

#### Rules

| Rule                 | Contract                                                                                                                                                                                                                                                                                        |
| -------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Unique names         | Names are unique, ignoring case, across a run's mount table                                                                                                                                                                                                                                     |
| `Engine`             | Reserved for the engine content build; a project mount named `Engine` is refused                                                                                                                                                                                                                |
| Name follows content | A folder is mounted under one name by every project that mounts it, because the references inside it carry that name. Mounting a studio library as `Studio` here and `Shared` there breaks every reference between its files in one of the two projects ([changing a mount](#changing-a-mount)) |
| Kind is fixed        | `.mounts.json` binds or overrides a `Path`; it never changes a `Kind`                                                                                                                                                                                                                           |
| Absolute paths       | Allowed only in `.mounts.json` ([below](#absolute-paths-only-in-mountsjson))                                                                                                                                                                                                                    |
| No overlap           | Two Folders may not be the same folder or contain one another: a file has exactly one mount path ([why](#why-mounts-may-not-overlap))                                                                                                                                                           |
| Not in output        | A Folder may not be or lie inside an [output folder](#output-folders)                                                                                                                                                                                                                           |

### Absolute paths only in `.mounts.json`

`.mounts.json` is the only file a person writes that may hold an absolute path.
Every other file that is written by hand or committed holds relative paths or
mount paths: `Project.oxy`, import manifests, descriptors, model import records
and scripts. A tool or the editor refuses an absolute path in one of them with
`path.absolute_committed`, naming the file and the field.

Two carriers that are not authored files also pass absolute paths. Neither is
committed or stored:

| Carrier             | Examples                                                                                                                                                                                                                                    |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Command lines       | `--root`, `--cooked-root`, `--cooked-context-root`, `--mounts`, `--mount`                                                                                                                                                                   |
| Generated run files | A Player launch file the editor writes under `.build/` for one Play session, from the calculated Folders, rewritten on every Play; the `--mounts` file the editor writes there for one cook, so the cook uses the table the editor resolved |

### Output folders

An output folder is any folder below the project root whose name starts with
a dot. Nothing authored lives there and no mount may reach into one. The rule
classifies; it grants nothing: a tool writes, rewrites and deletes only the
output folders it owns, and never touches a dot-named folder that is not its
own (`.git`, `.vs`, `.vscode` are output folders by the rule and belong to
other tools). This is the rule the code already applies: the editor's
`ProjectOutputFolders.Contains` treats every dot-named folder as output, and
the engine refuses a cooked identity with a dot-named segment
(`VirtualPath.cpp`). The rule is not a list, so a new output folder needs no
change to it. The ones in use today:

| Folder      | Written by                                                                                 |
| ----------- | ------------------------------------------------------------------------------------------ |
| `.cooked`   | Engine tools and the editor: cooked content and model generations                          |
| `.build`    | The editor: paks, catalogs and generated Play run files; the examples' paks in this design |
| `.pipeline` | The editor: generated scene descriptors (`SceneDescriptorGenerator`)                       |

`.` and `..` are not folder names: the check runs on canonical folders, which
have neither, so a committed `../../Media/Showcase` is never taken for output.
Folders outside the project root are not checked by name; a mount at
`D:/Users/me/.studio` is valid.

The editor's layout also specifies `.imported` and `.oxygen`, which nothing
writes or reads. The
[editor proposal](../../../../../design/editor/proposals/source-mounts/README.md#output-folders-retire-imported-and-oxygen)
retires them; the engine's `virtual_paths.md` and `VirtualPathCases.json` stop
naming `.imported`.

A mount folder may not be an output folder or lie inside one, for example
`Cache -> .cooked/imports`. Everything in a mount can be imported or cooked,
so a cook would read its own output and record it as a source; and the tool
that owns an output folder deletes, rewrites and collects it, so a source kept
there can vanish.
The check is `mount.folder_in_output`, made on canonical folders like the
overlap check.

### Where mounts come from

A tool or the editor builds one mount table before it reads any source. Each
name gets its folder from the first of these that provides it:

1. **A mounts file named on the command line** (`--mounts`).
2. **The project's own `.mounts.json`**, at the project root: it may
   override a committed path, bind a placeholder or add a local mount.
3. **The committed declaration**: the `Mounts` list of `Project.oxy` in the
   editor, the import manifest's `mounts` for tools, a single command's
   `--mount` options.
4. **Parent `.mounts.json` files**, above the project root, nearest first,
   stopping after a file marked `"root": true` ([hierarchy](#the-hierarchy)).
   They only bind placeholders and add local mounts: a name with a committed
   path has already been decided by step 3.
5. **Nothing**: the mount is unbound.

`/Engine` is not part of a project's table. Engine content reaches a project as
cooked content (the engine content pak), which needs no source folder.

The **project root** is the folder relative mount paths and `.mounts.json`
belong to: the folder of `Project.oxy` in the editor, the manifest's
`project_root` for tools (below), and for a single import command `--root`,
else the working directory.

### What makes a mount table valid

A table is checked once, after `.mounts.json` is applied and every relative
path is resolved against the project root and made canonical
([canonical folders](#canonical-folders)):

| Check                                                                | Result                                                                                                |
| -------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| Invalid or duplicate name                                            | Error `mount.name_invalid` / `mount.name_duplicate`                                                   |
| An absolute path outside `.mounts.json`                              | Error `path.absolute_committed`, naming the file and the field                                        |
| Two mounts with overlapping folders                                  | Error `mount.folder_overlap`, naming both mounts                                                      |
| A folder inside an output folder                                     | Error `mount.folder_in_output`                                                                        |
| A bound folder that does not exist                                   | Warning `mount.folder_missing`; a source needed from it is an error at that point                     |
| A `Pak` mount bound to a folder, or another kind bound to a file     | Error `mount.path_kind`                                                                               |
| A parent `.mounts.json` names a mount the project commits a path for | Warning `mount.parent_ignored`, naming the file; the committed path is used                           |
| A placeholder `.mounts.json` leaves unbound                          | Warning `mount.unbound`; a source needed from it is `import.mount_unbound`                            |
| One batch maps one name to two folders                               | Error `mount.name_conflict` before any job runs; two spellings of one canonical folder are one folder |

The last check is what makes many manifests safe. A batch reads every manifest
before cooking. Manifests may declare the same mounts, or different ones; they
may not map one name to two folders. That holds for a single run. Separate runs
into one cooked folder are not compared: the index records names, not folders,
so it cannot tell that `Content` meant another folder last time.

### Why mounts may not overlap

With `Studio -> C:/Studio` and `StudioFree -> C:/Studio/Free`, the file
`C:/Studio/Free/Rock.png` would be both `/Studio/Free/Rock.png` and
`/StudioFree/Rock.png`. A mount path is an identity, so the file would have
two:

- **Identities would depend on the machine.** This is the deciding reason.
  Mounts can be added locally, also from a parent `.mounts.json`. A developer
  whose parent file adds `StudioFree` would record and reference
  `/StudioFree/Rock.png`; the build agent, without it, `/Studio/Free/Rock.png`.
  A local file may say where a name lives; it may never change what a file is
  called.
- **One file would collide with itself.** Two manifests naming it under the two
  paths and cooking it to one identity are refused with
  `import.identity_collision`, though the source is one file.
- **One file would cook twice.** Under two identities, it is cooked, packed
  and recooked twice.

Nesting with the inner mount taking its folder away from the outer one, as file
system mount points do, was rejected. It makes the paths unambiguous, but
declaring a mount, even locally, would then rename files that already belong
to another mount and break every reference to their old paths on that machine.
A project that wants a short name for part of a mount references
`/Studio/Free/...`; content that is really separate gets a folder outside
`C:/Studio`.

### Canonical folders

Path text does not tell whether two folders are the same. A junction or
symbolic link (`C:/Studio/Free` linking to `D:/Free`), a `subst` drive, a
mapped drive and its UNC path (`S:/` and `//server/share`), and 8.3 short names
(`C:/STUDIO~1`) all reach one folder by different text. The overlap check would
miss them and give files two mount paths again.

Every mount folder is therefore made canonical before it is checked or used:
resolved through the file system to its final path (`GetFinalPathNameByHandle`
on Windows, `realpath` elsewhere), with links and short names expanded. A folder
that does not exist yet is canonicalized through its deepest existing parent;
one with no existing parent (a removed drive, an unreachable share) keeps its
text form and is `Missing`. The same canonical form is used to turn a file into its mount path, so a file
reached through a link maps to the mount that holds its final location, or is
refused with `import.source_outside_mount`. Folders are compared ignoring case
on Windows and as the file system reports elsewhere.

The canonical folder is a local fact like any folder: it is logged with the
mount table and never stored.

### Changing a mount

| Change                                | Effect                                                                                   |
| ------------------------------------- | ---------------------------------------------------------------------------------------- |
| Folder changed, same files            | None: names and recorded identities are unchanged                                        |
| Folder changed to different files     | The ordinary changed-source rule: cooked output from the old files is stale and recooked |
| Name changed                          | An identity change: every reference to the old name is updated, as for any asset rename  |
| Placeholder bound, unbound or rebound | A folder change, with the same rules                                                     |

## The `.mounts.json` file

`.mounts.json` is the one place where a machine says where mounts live. It is
local: the project's `.gitignore` lists it, and project templates write that
entry. Nothing committed refers to it by path.

```json
{
  "$schema": "oxygen.mounts.schema.json",
  "version": 1,
  "root": false,
  "mounts": {
    "Studio": "D:/Studio/SharedContent",
    "Showcase": "E:/Media/Showcase"
  }
}
```

| Field     | Contract                                                                                                                                                                                                                          |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `version` | `1`. Another value is refused with `mount.file_version`                                                                                                                                                                           |
| `root`    | Optional, default `false`. `true` stops the search: no `.mounts.json` above this one is read                                                                                                                                      |
| `mounts`  | Object: mount name to folder (a `.pak` file for a `Pak` mount). An absolute path, the usual form, or a path relative to this file's folder. `/` or `\` separators                                                                 |
| Override  | A name the committed declaration has. In the project's own file: replaces its folder, or binds its placeholder. In a parent file: binds its placeholder only; a name with a committed path is ignored with `mount.parent_ignored` |
| Addition  | A name the committed declaration lacks: a local mount, available to this machine's imports and cooks                                                                                                                              |

### The hierarchy

`.mounts.json` resolves the way `.editorconfig` does: the nearer, more
specific statement wins. Starting at the project root, every folder up to the
file-system root may hold one; the search stops after a file with
`"root": true`. A relative path in a file resolves against that file's own
folder.

Nearness puts the project's committed declaration ahead of every parent file.
Only the project's own `.mounts.json`, beside `Project.oxy`, overrides a
committed path, as `.git/info/exclude` and `.env.local` are local to the
project they adjust. A parent file binds placeholders and adds local mounts
for every project below it; it can never retarget a mount a project commits.
For a name several projects bind from one parent file, those projects declare
it as a placeholder.

```text
H:/projects/.mounts.json         Studio -> D:/Studio/SharedContent
H:/projects/game-a/.mounts.json  Showcase -> E:/Media/Showcase
H:/projects/game-a/Project.oxy   Content -> Content, Showcase -> ../Media, Studio
H:/projects/.mounts.json         Content -> D:/Elsewhere (ignored)
H:/projects/game-b/.mounts.json  root: true, Studio -> F:/Studio-Old
```

| Project  | `Content`                                                                                              | `Showcase`                        | `Studio`                                     |
| -------- | ------------------------------------------------------------------------------------------------------ | --------------------------------- | -------------------------------------------- |
| `game-a` | `H:/projects/game-a/Content`, committed; the parent's `Content` is ignored with `mount.parent_ignored` | `E:/Media/Showcase`, its own file | `D:/Studio/SharedContent`, the parent file   |
| `game-b` | Its committed path                                                                                     | Its committed path                | `F:/Studio-Old`; the parent file is not read |

A mount that a parent file adds and a project does not declare is a local mount
of every project below it, with the same validation: when its folder overlaps
one of a project's mounts, the error names the mount and the `.mounts.json`
that added it.

### Governed use

- **Only mounts.** `.mounts.json` holds nothing else: no settings, no cooked
  roots, no tool options. Any other key is refused with `mount.file_invalid`.
- **Never required.** A project with no `.mounts.json` anywhere above it is
  valid. Committed relative mounts work as they are; placeholders are reported
  unbound.
- **Never written by tools.** The editor writes the project root's
  `.mounts.json` only when the user binds or overrides a mount in its Mounts
  dialog, and never a parent file. Import, cook and pak tools only read them.
- **Reported.** Every run that resolves mounts logs each mount's folder and
  the file or option it came from, so a binding from a parent file is never a
  surprise.
- **Local mounts stay local.** Content that references a mount only
  `.mounts.json` declares cannot be opened elsewhere. Saving or cooking such a
  reference from committed content is a warning, `mount.reference_local`, so a
  developer sees the dependency before committing it.
- **Paths are not identities.** No cooked index, record, descriptor or pak
  stores a folder from `.mounts.json`.
- **One schema.** `oxygen.mounts.schema.json` is engine-owned, installed with
  the other schemas, embedded in the tools that read the file, and read by the
  editor from the installed SDK.

## Recorded source identity

The loose cooked index records, for every asset and texture descriptor, the
source that produced it (`AssetEntry.source_path_offset`,
`FileRecord.source_path_offset`). The cooker replaces an entry when the same
source cooks it again and refuses it with `import.identity_collision` when
another source does.

The recorded value becomes the source's **mount path**: `/<Mount>/<path in
mount>`, computed from the file's location through the mount table. The
output identity no longer takes part.

| Scenario                                                          | Recorded source                                         | Outcome                       |
| ----------------------------------------------------------------- | ------------------------------------------------------- | ----------------------------- |
| `Content/Materials/Red.omat.json` cooked twice                    | `/Content/Materials/Red.omat.json` both times           | Replaced                      |
| Two examples ship `floor.omat.json`, cooked to one identity       | `/Content/Examples/A/...` and `/Content/Examples/B/...` | Second refused                |
| Four examples cook `/Content/Common/Materials/cube_red.omat.json` | The same path from every manifest                       | Re-cooks                      |
| `Studio` bound to `D:` here and `/mnt/studio` on the build agent  | `/Studio/Materials/Steel.omat.json` on both             | Same source                   |
| A model `Ship.glb` with its textures and materials                | `/Content/SourceMedia/DCC/Ship.glb` for each            | All belong to the model       |
| A sidecar completing a scene                                      | The scene's recorded source, as today                   | No collision with the scene   |
| A source in no mount                                              | None                                                    | `import.source_outside_mount` |
| `Red.omat.json` written `red.omat.json` in a manifest             | Compared ignoring case, as identities are               | Same source                   |

The field keeps its type and position. Its content changes for every entry, so
the loose cooked index version goes from 6 to 7 and older roots are rejected
and recooked, never misread.

## Tools and runtime

### Import manifests

```json
{
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
    }
  ]
}
```

| Field                       | Contract                                                                                                                                                                                                                                                                                                                          |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `project_root`              | Relative to the manifest's folder. Default: the manifest's folder. `--root` replaces it, as the editor's generated manifests need                                                                                                                                                                                                 |
| `mounts`                    | The committed declaration: `name`, optional `kind` and optional `path` per mount, with the rules above. `.mounts.json` files are applied over it                                                                                                                                                                                  |
| `layout.virtual_mount_root` | The identity prefix of the output root: a file at `X` in `output` has the identity `<root>/X`. A manifest that declares `mounts` sets it to the mount its identities live in (`/Content`); its default, `/.cooked`, would put them under an output folder. Every job's `virtual_path` lies under it, as the cookers already check |
| Job `source`                | A mount path (`/Content/...`), the form manifests use; or a path relative to `project_root` that lies in a mount                                                                                                                                                                                                                  |
| Every path                  | Relative. An absolute path in any field is refused with `path.absolute_committed`; generated manifests pass absolute folders as options                                                                                                                                                                                           |
| Model `record`              | A mount path or a file path, with the same rule                                                                                                                                                                                                                                                                                   |
| Removed                     | `script.source_root` (defaults and per job): a script's source is located by its mount path                                                                                                                                                                                                                                       |

ImportTool options:

| Option                       | Contract                                                                                                             |
| ---------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| `--root <dir>`               | The project root, replacing every manifest's `project_root`                                                          |
| `--mounts <file>`            | A mounts file applied above every `.mounts.json` found; with `"root": true` it is the only one read                  |
| `--mount <N>=<dir>`          | Single import commands: declares a mount; repeatable. Their positional source must lie in one                        |
| `batch --manifest <file>...` | Several manifests in one run and one mount table: conflicts are checked once and a shared source is cooked once      |
| Removed                      | `--script-source-root` of the `script` command; `--content-root` of `fbx`/`gltf` becomes the project root (`--root`) |

The manifest schema (`oxygen.import-manifest.schema.json`) gains
`project_root` and `mounts` at manifest level only, so the per-job key lists
are unchanged except for the removed `script.source_root`.

### Model import records

A record's `source` is stored as a mount path:
`"source": "/Content/SourceMedia/DCC/Ship.glb"`. Committed records then never
hold absolute paths, and a model whose mount moves keeps its record. Records
stay at `<project_root>/imports/<source_identity>.import.json`, and their
generations at `<project_root>/.cooked/imports/<source_identity>/` with
`head.json`, as `ModelImportRecord` places them today. `LocateRecord` matches
sources by mount path.

### External scripts

An external script asset stores its source as a mount path
(`external_source_path = "/Content/Scripts/move.lua"`), instead of a path
relative to a script source root. Three places resolve it through a mount
table:

| Where              | Today                                                | With mounts                                                                                |
| ------------------ | ---------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| PakTool sealing    | `--script-source-root`, repeatable, first match      | `--root` and `--mounts`, as ImportTool; one folder per name, no search                     |
| Runtime resolver   | `PathFinderConfig::ScriptSourceRoots()`, first match | `PathFinderConfig::SourceMounts()`: name to folder; `ExternalFileResolver` splits the name |
| Player launch file | `script_source_roots`: list of folders               | `source_mounts`: object of name to folder, the `.mounts.json` shape                        |

Hot reload watches the folders of the mounts that hold loaded scripts. The
launch file's existing `mounts` key lists cooked roots and paks; it keeps that
meaning, and the source table gets its own key to keep the two apart.

### PakTool

PakTool reads cooked folders and paks, which already carry `/<Mount>/...`
virtual paths; it needs no source mounts except for script sealing. It does not
copy the loose index's recorded sources into paks.

### Engine content

`WriteEngineContentSources` writes manifests that declare
`mounts: [{ "name": "Engine", "path": "." }]`, with the generated sources
folder as their project root and job sources as `/Engine/...` mount paths.
They hold no absolute path: the CMake rule passes the cooked folder as
`--cooked-root`. The generated sources folder also gets a `.mounts.json` with
`"root": true` and no mounts, so no file above the build tree can rebind
`Engine`. The build stays self-contained and unconfigured.

## The examples project

`Examples/Content` becomes an ordinary Oxygen project in the standard project
layout: one committed `Content` mount with raw media in `Content/SourceMedia`,
one folder and manifest per example, and a catalog of examples. Its design,
migration and dependencies are in
[the examples project](../examples-project/README.md).

## Decisions

- **Mount names are identities; folders are local facts.** Everything stored
  (indexes, records, descriptors, script assets, paks) uses mount paths, so
  content survives drives, machines and moved projects.
- **One mount list with kinds.** A project or manifest declares one mount
  list; each entry has a name, a kind (`Source`, `Cooked`, `Pak`) and an
  optional path. One list keeps one name space and one validator.
- **No absolute paths in committed files.** Projects, manifests, descriptors,
  records and scripts hold relative paths, mount paths or placeholders.
  Absolute paths come only from `.mounts.json`, command lines and generated run
  files.
- **`.mounts.json` resolves up the folder hierarchy.** The nearer statement
  wins, as in `.editorconfig`: the project's own file, then its committed
  declaration, then parent files, which only bind placeholders and add local
  mounts. Relative paths resolve against their file, and `"root": true` stops
  the search. A parent file can never retarget a mount a project commits.
- **Placeholders are first-class.** An unbound mount is a warning until a
  source in it is needed; cooked content from it keeps working.
- **One mount table per run.** A batch checks every manifest's mounts together
  and refuses one name mapped to two folders, and overlapping folders, before
  any job runs.
- **Mounts never overlap.** A file has exactly one mount path on every machine.
  Nesting mounts was rejected, because a local mount would then rename files
  of another mount.
- **Folders are compared canonically.** Links, `subst` drives, UNC paths and
  short names are resolved before mounts are checked or files are mapped, so
  no alias gives a file a second mount path.
- **The cooker records mount paths.** The loose cooked index's source field
  becomes the source's mount path; the version goes to 7. This replaces the
  derivation #81 reports and makes "same source" mean "same file".
- **Mounts replace script source roots.** Cooking, sealing and running
  external scripts resolve one mount path through one table; script source
  roots, `--script-source-root` and `script_source_roots` are removed.
- **The engine owns the mount file format.** `oxygen.mounts.schema.json` is an
  engine schema; the editor reads it from the installed SDK and owns
  `Project.oxy`.
- **The examples are an Oxygen project.** They follow the project layout, cook
  through mounts like any project, and need no special case in a tool.
- **Example scenes stay cooker descriptors.** Converting them to editor scene
  documents is separate work; every other example source uses the editor's
  naming so the editor cooks it.

## Accepted designs this touches

| Document                                                                                                                 | Change                                                                                     |
| ------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------ |
| `src/Oxygen/Cooker/Tools/ImportTool/README.md`                                                                           | Manifest `project_root` and `mounts`; `--root`, `--mounts`, `--mount`; mount-path sources  |
| `src/Oxygen/Cooker/Tools/PakTool/README.md`                                                                              | Script sealing by mounts; `--script-source-root` removed                                   |
| `src/Oxygen/Cooker/Docs/Import/async_import_pipeline_v2.md`                                                              | Records with mount-path sources                                                            |
| `src/Oxygen/Data/LooseCookedIndexFormat.h`, LooseCooked README                                                           | Source field meaning; version 7                                                            |
| `src/Oxygen/Player` launch schema and docs                                                                               | `source_mounts` replaces `script_source_roots`                                             |
| `design/editor/lld/project-layout-and-templates.md`, `project-services.md`, `content-pipeline.md`, `script-authoring.md` | See [editor source mounts](../../../../../design/editor/proposals/source-mounts/README.md) |

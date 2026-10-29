# Editor source mounts

Status: proposal. Nothing here is implemented, scheduled or accepted. It is
written against the editor as it is today and changes no accepted document.

Last updated: 2026-10-29

Read: [at a glance](#at-a-glance), [scenarios](#scenarios),
[today](#today), [the project's mounts](#the-projects-mounts),
[changes by area](#changes-by-area), [migration](#migration),
[decisions](#decisions),
[accepted designs this touches](#accepted-designs-this-touches).

The mount contract, the `.mounts.json` format and the tool and runtime changes
are in the engine proposal,
[source mounts](../../../../projects/Oxygen.Engine/design/proposals/source-mounts/README.md).
This document is what that contract means for the editor.

## At a glance

| Question         | Answer                                                                                                                                                                                                                                                                                                                      |
| ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| What changes?    | `Project.oxy` declares one `Mounts` list: a name, a kind and a relative path or none (a placeholder) per mount, never an absolute path. Git-ignored `.mounts.json` files bind mounts with absolute paths per machine: the project's own file binds and overrides, parent files only bind placeholders and add local mounts. |
| For the user     | A project cloned on any machine opens; shared content on any drive is authored, cooked and played like `Content`; missing or unbound mounts are visible, never silent.                                                                                                                                                      |
| Main code change | One resolved mount table in `ProjectContext`, used by every component that maps a mount to a folder, replacing about 20 direct `ProjectRoot.Combine(RelativePath)` calls.                                                                                                                                                   |
| Recorded paths   | Cook inputs, provenance, model records and script sources store mount paths (`/Content/...`), not project-relative or absolute paths.                                                                                                                                                                                       |
| Retired          | The separate `AuthoringMounts`, `LocalFolderMounts` and `PakFileMounts` lists and their committed absolute paths; script source roots in Play.                                                                                                                                                                              |

## Scenarios

**Clone and open.** A developer clones a project whose `Project.oxy` declares
`Content -> Content`, `Showcase -> ../../Media/Showcase` and a placeholder
`Studio`. The project opens. The Content Explorer shows `Studio` as unbound,
with a Bind action. Binding it writes the project's `.mounts.json` and the
mount's assets appear. Nothing committed changed. A developer whose
`H:\projects\.mounts.json` already binds `Studio` for every project below it
opens the project with `Studio` bound.

**Author in a shared mount.** The developer opens `/Studio/Materials/Steel.omat`
from `D:\Studio\SharedContent`, edits it and saves. The material cooks, plays
in Live Preview, and its cooked source is recorded as
`/Studio/Materials/Steel.omat.json`. Renaming it inside `Studio` updates the
references of the open project, as in `Content`.

**Override locally.** A developer keeps `Showcase` on another drive. In the
Mounts dialog, they override its folder; the editor writes the override to
`.mounts.json` and reloads that mount. The committed relative path stays.

**A mount that goes away.** A USB drive holding a bound mount is removed. The
mount shows as missing; its cooked content keeps loading; a cook that needs one
of its sources fails with a diagnostic naming the mount and its folder.

## Today

From the code, not the layout LLD:

| Fact                                                                                                                                           | Evidence                                                                                              |
| ---------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| `ProjectMountPoint(Name, RelativePath)` is project-relative; `LocalFolderMount` and `PakFileMount` persist absolute paths in `Project.oxy`     | `Oxygen.Editor.Projects/src/ProjectMountPoint.cs:17`, `LocalFolderMount.cs:16`, `PakFileMount.cs:17`  |
| `LocalFolderMounts` are browse-only or cooked libraries: the cook reads authoring mounts only, contrary to the LLD's "authored content roots"  | `CookInputResolver.cs:111-125`, `CookedContentOrdering.cs:37-80`, `ContentMountsViewModel.cs:53`      |
| An outside-tree folder added in the Content Explorer becomes a `LocalFolderMount`, so it cannot be cooked or edited                            | `ContentFoldersViewModel.cs:325-338`                                                                  |
| Opening fails when any mount folder is missing (`OXE.PROJECT.VALIDATION_FAILED`); a mount that fails to restore in session is dropped silently | `ProjectValidationService.cs:162-225`, `ContentFoldersViewModel.Mounts.cs:167-219`                    |
| `ProjectAssetFiles` maps mounts to folders, but about 20 consumers compute `ProjectRoot.Combine(mount.RelativePath)` themselves                | `ContentPipelineService.cs:312`, `PlaySession.cs:218`, `AssetCatalog.cs:273`, and others listed below |
| Cook inputs and provenance store project-relative paths and refuse `../`                                                                       | `CookInputResolver.cs:55-92`, `CookProvenance.SourceFacts.cs:98-101`                                  |
| Model records store an outside-tree source as an absolute path                                                                                 | `Oxygen.Editor.Models/src/ModelImportSettings.cs:189-201`                                             |
| Relocation writes only inside the project root                                                                                                 | `Relocation/RelocationPaths.cs:50-77`                                                                 |
| Play passes the authoring mount folders as unnamed `script_source_roots`, dropping missing ones                                                | `LivePreview/Play/PlaySession.cs:217-268`                                                             |
| The LLD rejects `..` and outside-root paths; the code checks only rooted paths and dot folders                                                 | `project-layout-and-templates.md:243-251`, `ProjectValidationService.cs:128-170`                      |

## The project's mounts

### Declared in `Project.oxy`

`Project.oxy` declares one `Mounts` list. Each entry is a name, a kind and an
optional path, relative to the project root. An absolute path is refused with
`path.absolute_committed`.

```json
"Mounts": [
  { "Name": "Content", "Kind": "Source", "Path": "Content" },
  { "Name": "Showcase", "Kind": "Source", "Path": "../../Media/Showcase" },
  { "Name": "Studio", "Kind": "Source" },
  { "Name": "VendorCooked", "Kind": "Cooked" },
  { "Name": "Engine2D", "Kind": "Pak", "Path": "../Paks/engine2d.pak" }
]
```

A developer's `.mounts.json` at the project root binds and overrides them with
absolute paths; one in a parent folder only binds placeholders and adds local
mounts:

```json
{
  "version": 1,
  "mounts": {
    "Studio": "D:/Studio/SharedContent",
    "VendorCooked": "E:/Vendor/cooked"
  }
}
```

| Kind     | Holds                                                                  | Today                                            |
| -------- | ---------------------------------------------------------------------- | ------------------------------------------------ |
| `Source` | A folder of authored sources: edited, cooked, relocated and watched    | `AuthoringMounts`, inside the tree only          |
| `Cooked` | A cooked folder (`container.index.bin`) mounted read-only as a library | `LocalFolderMounts` with an index, absolute path |
| `Pak`    | A `.pak` file mounted read-only as a library                           | `PakFileMounts`, absolute path                   |

One list keeps one name space, one validator and one place to bind. Every
entry follows the engine's mount rules: name pattern, names unique ignoring
case, `Engine` and the editor's reserved names refused, no overlap, no folder
in an output folder. A shared folder is mounted under the same name by every
project, because the references inside it carry that name; the Mounts dialog
cannot check that, so it is a studio convention like the folder itself. `Content -> Content` of kind `Source` stays required. The
content priority order (`CookedContentOrder`) lists `Cooked` and `Pak` mounts by
name, as it lists libraries today.

### Resolved per machine

`ProjectContext` holds the **resolved mount table**: for each declared or
locally added mount, its name, kind, committed path, resolved folder or file,
and where that came from:

| Binding     | Meaning                                                            |
| ----------- | ------------------------------------------------------------------ |
| `Committed` | The `Project.oxy` relative path, resolved against the project root |
| `Local`     | A `.mounts.json` overrides or binds it; the table keeps which file |
| `LocalOnly` | A `.mounts.json` adds it; `Project.oxy` does not declare it        |
| `Unbound`   | A placeholder with no binding                                      |
| `Missing`   | Bound, but the folder or file does not exist                       |

The merge is the engine's rule. The editor implements it in C#, and a shared
case file, in the manner of `src/Oxygen/Content/VirtualPathCases.json`, is run
by both the engine's and the editor's tests so the two readers cannot drift.

## Changes by area

### Model, persistence and validation

| Component                                                                                                                               | Change                                                                                                                                                                                                                                                                                                                                       |
| --------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ProjectMountPoint`, `LocalFolderMount`, `PakFileMount`, `ProjectInfo` descriptor, `FromJson`/`ToJson`                                  | One `ProjectMount` (`Name`, `Kind`, optional `Path`, `IsExpanded`) in one `Mounts` list; `..` allowed; absolute paths refused; schema version 2                                                                                                                                                                                              |
| New `ProjectMounts` reader (Projects)                                                                                                   | Reads the `.mounts.json` hierarchy from the project root upward, stopping after `"root": true`, against the installed `oxygen.mounts.schema.json`; writes only the project root's file, for Bind and Override                                                                                                                                |
| `ProjectContext`, `ProjectContext.FromProjectInfo`, `CookedContentOrdering`                                                             | `Mounts`, the resolved table above, replaces `AuthoringMounts`, `LocalFolderMounts` and `PakFileMounts`; libraries are its `Cooked` and `Pak` mounts                                                                                                                                                                                         |
| `ProjectValidationService`                                                                                                              | Opening never fails for `Unbound` or `Missing`; it reports them as warnings with their own codes; structural errors (names, overlap, output folders) still fail                                                                                                                                                                              |
| `MountChange.Validate`, `ContentMountsViewModel.ValidateName`, `ProjectCreationService`, `LocalTemplatesSource`, `ProjectOutputFolders` | One validator implementing the engine rules, replacing four partial copies; overlap and file-to-mount mapping use canonical folders, as the engine defines. `ProjectOutputFolders.Contains` checks the canonical folder below the project root, so `..` in a committed path (`../../Media`) is no longer taken for a dot-named output folder |
| `ProjectManagerService.Metadata.cs`                                                                                                     | Unchanged for `Project.oxy`; `.mounts.json` writes go through the same atomic file store                                                                                                                                                                                                                                                     |
| Templates (`ProjectCreationService`, `LocalTemplatesSource`)                                                                            | Write `.gitignore` with `.mounts.json` and the output folders                                                                                                                                                                                                                                                                                |

### Resolution

`ProjectAssetFiles` becomes the only mapper between mount paths and files, on
top of the resolved table. Every component below stops combining
`ProjectRoot` with `RelativePath` and asks it instead:

- Content pipeline: `ContentPipelineService.cs:297,312-332`,
  `ContentPipelineService.Incremental.cs:160`, `.Inspection.cs:106,167`,
  `Import/ImportSourceRetention.cs`, `Import/ModelSourceAnalyzer.cs`,
  `Inspection/ProjectAssetKeyIndex.cs`, `Snapshots/CookSourceAnalyzer.cs`,
  `Cooking/AutomaticCookService.cs`.
- Assets and scripts: `Assets/AssetCatalog.cs:273,562`,
  `Scripting/ProjectScriptFacts.cs`, `Input/ProjectInputFacts.cs`,
  `Input/InputSourceFiles.cs`.
- Import dialogs: `Textures/TextureImportTarget.cs`,
  `ModelEditor/Importing/ModelImportDialogViewModel*.cs`,
  `ContentExplorer/Importing/TextureImportDialogViewModel.cs`.
- Content Explorer: `ContentExplorerViewModel.cs:262`,
  `Panes/Assets/AssetsViewModel.cs`, `Layouts/AssetsLayoutViewModel.cs`.
- Play: `LivePreview/Play/PlaySession.cs:218`,
  `LivePreview/Play/RuntimeProblemReader.cs:53`.

### Cook pipeline

| Component                                       | Change                                                                                                                                                                                                                                                                                                                         |
| ----------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `ContentCookInput`                              | `SourceRelativePath` becomes `SourceMountPath` (`/Content/...`)                                                                                                                                                                                                                                                                |
| `CookInputResolver`                             | Resolves through the table; any bound authoring mount, inside or outside the tree; `Unbound`/`Missing` give a coded failure                                                                                                                                                                                                    |
| `ContentImportManifestBuilder`                  | Writes the project's committed `mounts` and mount-path job sources; `--root` is the project root; the resolved table is passed as a generated `--mounts` file under `.build/` with `"root": true`, so the cook uses the table the editor shows instead of resolving the hierarchy a second time; `GetScriptSourceRoot` removed |
| `SceneDescriptorGenerator` output               | A generated scene descriptor in `.pipeline` is the scene job's `source` (the file read), with `derived_from` set to the authored scene's mount path (`/Content/Scenes/X.oscene.json`), its recorded source; `.pipeline` is never a mount                                                                                       |
| `CookInputSnapshot`                             | Snapshots keep the mount path as each input's identity                                                                                                                                                                                                                                                                         |
| `CookProvenance`, `CookProvenance.SourceFacts`  | Stores mount paths; `ValidRelativePath` becomes mount-path validation; `CreateProducerLookup` resolves through the table                                                                                                                                                                                                       |
| `ExactSourcePath` callers                       | Check case against the mount folder, not the project root                                                                                                                                                                                                                                                                      |
| `CookPublicationDocument.ConfigurationIdentity` | Hashes mount names, kinds and committed paths, never resolved folders, so a local override does not invalidate publications; changed files are caught by source facts as today                                                                                                                                                 |

### Model import records

`ModelImportSettings.WithSource`/`ResolveSource` store and read `source` as a
mount path; `ModelImportRecords` and `ModelRecordLookup` index records by mount
path. The engine record format makes the same change.

### Relocation

`RelocationPaths.GetWritableMounts`, `ToPhysical`, `ToVirtual` and
`FindWritableMount` accept every bound authoring mount, inside or outside the
project tree, and `AuthoredReferenceIndex` follows. Moves between two mounts
remain identity changes with reference review.

A relocation updates the references of the open project only. Projects that
mount the same folder and are not open are not updated or warned. They
recover when they are next opened, by the person who owns them, with the
studio's source control history or the library's release notes. The
editor makes that recovery possible, never automatic:

| Guarantee                       | How                                                                                                                                                                                                                                                                      |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| The project opens               | A reference to a moved or deleted asset stays as written, an unresolved mount path (`OXE.ASSETID.Resolve.Missing`); nothing is dropped or rewritten. Its cooked output keeps loading until the next cook.                                                                |
| Every broken reference is found | A reference check, run on open, after a mount change and on demand, lists every unresolved reference in the project: the referencing file, the property, the missing mount path and the code. It reads `AuthoredReferenceIndex`, the index relocation already maintains. |
| Tools can read the list         | The check also writes it to `.build/references.json`, so a script or an AI agent can work from it without the editor's UI.                                                                                                                                               |
| References can be repaired      | References are mount paths in text files (descriptors, scenes, records), and a cooked identity mirrors its source's mount path. An old-to-new map, from `git log -M --name-status` in the library's repository or from release notes, is enough to rewrite them.         |
| Repairs are picked up           | The open project reloads files changed outside the editor (`editor-documents.md` 5.2.3); the reference check and the next cook confirm that nothing is left.                                                                                                             |

The editor writes no redirect files into a shared mount. Source control
already records every move, and redirect files would pile up in the library
for every project that once used it.

### The same file in two sessions

Two editor sessions, in one project or in two projects that mount the same
folder, may open the same file. The editor handles this as IDEs and DCC tools
do (Visual Studio, VS Code, Maya, Blender). It holds no lock. It saves only
over the version it read, and it watches the file. Exclusive editing belongs
to source control: Perforce exclusive checkout, or `git lfs lock` for binary
sources.

This is the contract the editor already has for files changed outside it
(`editor-documents.md` 5.2.3). A session that saves or relocates a file is,
to every other session, an outside change:

| In session B, the file open in A is | Session A shows                                                                                                  |
| ----------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| Saved, and A has no unsaved edits   | The file reloaded                                                                                                |
| Saved, and A has unsaved edits      | The conflict banner: Reload, Keep Mine, Save a Copy. A's save is refused while the file differs from what A read |
| Renamed, moved or deleted           | The document stays open, marked missing on disk, until A saves it again or closes it                             |

What source mounts add is reach: every bound mount gets a catalog and watcher
(see [Assets and watchers](#assets-and-watchers)), so the document service
sees changes to a shared file as it sees changes to `Content`.

### Scripts, Play and Live Preview

| Component                                                                   | Change                                                                                                                      |
| --------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `PlaySession`, `Runtime/Player/PlayerLaunchFile`                            | Launch file `source_mounts` (name to folder) instead of `script_source_roots`                                               |
| `RuntimeProblemReader.ResolveSource`                                        | Resolves the script's mount path through the table                                                                          |
| `PathFinderSettings`, `EngineStartupSettingsExtensions`, Interop `Config.h` | `SourceMounts` replaces `ScriptSourceRoots`; Live Preview passes the table too, so external scripts resolve in the viewport |

### Assets and watchers

`AssetCatalog` prepares one catalog and watcher per bound authoring mount,
inside or outside the tree. An `Unbound` or `Missing` mount has no catalog and
no watcher, and gets one when it becomes bound. The project-files catalog
excludes only mounts inside the project tree, as today.

### User interface

| Surface                                                                                                 | Behavior                                                                                                                                                                                                                                                    |
| ------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Content Explorer mount tree (`AuthoringMountPointTreeItemAdapter`, `ContentFoldersViewModel.Mounts.cs`) | Unbound and missing mounts stay in the tree with their state and a tooltip naming the folder or binding; Bind and Override actions; never dropped silently                                                                                                  |
| Mounts dialog (`ContentMountsViewModel`, `ContentMountRow`)                                             | Add a mount with a relative path or as a placeholder; override or bind locally (writes `.mounts.json`); shows each mount's binding                                                                                                                          |
| Add folder (`ContentFoldersViewModel.MountLocalFolderAsync`)                                            | An outside-tree source folder becomes an authoring mount; the user chooses a committed relative path (shared through the repository) or a placeholder bound locally (this machine only), as the two differ in what is committed, not in where the folder is |
| Creation targets (`AuthoringTargetResolver`)                                                            | Unbound and missing mounts are never targets                                                                                                                                                                                                                |
| Import dialogs                                                                                          | Targets and source mounts come from the resolved table                                                                                                                                                                                                      |

The visual design of the tree states and the dialog rows follows the editor's
mockup practice before implementation.

### Session

`ProjectSessionHost` and `MountChange` apply a `.mounts.json` change as a mount
change: every file of the hierarchy is watched, and a change in any of them
rebuilds the resolved table, the affected catalogs and Live Preview's source
mounts in one transaction.

### Output folders: retire `.imported` and `.oxygen`

The engine proposal defines an output folder by rule: any dot-named folder
below the project root. Five names are specified today; two of them have no
writer and no reader and are not needed by this design or any planned one:

| Folder      | Specified in                                               | Used by code                                                                                          | Outcome |
| ----------- | ---------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- | ------- |
| `.cooked`   | Project layout LLD                                         | Cook, model generations, Play                                                                         | Kept    |
| `.build`    | Project layout LLD                                         | Paks, catalogs, Play run files                                                                        | Kept    |
| `.pipeline` | Project layout LLD                                         | `SceneDescriptorGenerator` writes generated scene descriptors                                         | Kept    |
| `.imported` | Project layout LLD ("import intermediate/cache output")    | Nothing writes it. The Content Explorer shows it as a built-in `Imported` node, which is always empty | Retired |
| `.oxygen`   | Project layout LLD ("editor-local project metadata/cache") | Nothing. The `.oxygen-` hits in the code are temporary file-name prefixes, not this folder            | Retired |

Model generations, the only import output, live in the editor's mount
generations in `.cooked/generations`, and no design plans editor state inside
the project folder. A future output folder
needs no specification: the rule already covers it. Retiring the two removes
specification and UI, never behavior:

| Place                                                                                                                                  | Change                                                                                                                                         |
| -------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `design/editor/lld/project-layout-and-templates.md`                                                                                    | The layout tree, the folder table, the `.gitignore` template and the derived-folder notes drop both; the output folder rule replaces the lists |
| `design/editor/lld/asset-identity.md`, `assets-module.md`, `content-pipeline.md`                                                       | Their lists of derived folders become the rule, with `.cooked`, `.build` and `.pipeline` as examples                                           |
| `KnownVirtualFolderMount.Imported`, `ContentFoldersViewModel`, `ContentMountsViewModel`, `ProjectRootTreeItemAdapter`                  | The `Imported` built-in node and its `.imported` checks are removed; dot-named folders are hidden by the rule                                  |
| `projects/Oxygen.Editor.ContentExplorer/docs/content-folders-ui.md`                                                                    | The `Imported` node is removed                                                                                                                 |
| `ProjectOutputFolders`, `FileSystemAssetCatalog`, `VirtualPath.Rules.cs` comments; `Oxygen.Projects` and `Oxygen.Managed.Core` READMEs | Examples name `.cooked`, `.build`, `.pipeline` only                                                                                            |
| Engine `Content/Docs/virtual_paths.md`, `VirtualPathCases.json`, `ProjectOutputFoldersTests`                                           | Examples and cases use `.cooked`; the rule they test is unchanged                                                                              |

Closed milestone plans (`ED-M06`, `ED-M06A`) are records and stay as they are.

## Migration

Existing projects are migrated by editing their data; no reader of the old
form is kept.

| Old `Project.oxy` (version 1)                    | New `Project.oxy` (version 2) and `.mounts.json`                                                                    |
| ------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------- |
| `AuthoringMounts` entry                          | A `Mounts` entry of kind `Source` with the same name and path                                                       |
| `LocalFolderMounts` entry without a cooked index | A `Source` placeholder of the same name, bound in `.mounts.json` to the old absolute path                           |
| `LocalFolderMounts` entry with a cooked index    | A `Cooked` placeholder, bound in `.mounts.json`                                                                     |
| `PakFileMounts` entry                            | A `Pak` entry with a relative path when the pak is under the repository, else a placeholder bound in `.mounts.json` |

The owner's projects are edited by hand from this table; the editor refuses
version 1 with its existing unsupported-version diagnostic.

## Tests

Tests that pin today's rules change with them: `ProjectValidationAndContextTests`,
`ProjectAssetFilesTests`, `CookedContentOrderingTests`,
`ProjectManagerServiceTests`, `TestProjectTests` and `testing/TestProject.cs`
(Projects); `LocalTemplatesSourceTests`; `MountTransactionTests`;
`ProjectSessionTests`; `ContentMountsViewModelTests`,
`ContentFoldersViewModelTests`, `CookedLibraryFoldersTests`,
`ContentPriorityTests`, `ExplorerNavigationTests`, `ExplorerRevealTests`;
`CookedLibraryCatalogTests`, `AssetFeedTests`; `CookedContentMountTests`,
`CookedLibraryResolutionTests`, `IncrementalCookTests`, `ScriptCookTests`,
`RelocationPathsTests`, `AssetRelocationTests`, `AuthoredReferencesTests`;
`ModelImportRecordsTests`. New tests cover the shared merge cases, opening
with unbound and missing mounts, cooking and relocating in an outside-tree
mount, a `.mounts.json` change during a session, the reference check after
a shared asset moved while the project was closed (listed, written to
`.build/references.json`, clear after the rewrite), and a save in one session
of a file another session has open, clean and dirty.

## Decisions

- **One mount list with kinds.** `Project.oxy` declares `Source`, `Cooked`
  and `Pak` mounts in one list, with one name space and one validator.
- **No absolute paths in `Project.oxy`.** Mount paths are relative to the
  project root or absent; absolute paths live only in `.mounts.json` files,
  found up the folder hierarchy as the engine proposal defines.
- **Outside-tree authoring mounts are authoring mounts.** They are edited,
  cooked, relocated and watched like `Content`; `LocalFolderMounts` as a
  browse-only kind is retired.
- **Opening never fails on a mount's folder.** Unbound and missing mounts are
  visible states with actions, and their cooked content stays available.
- **One resolved table.** `ProjectContext` owns it and `ProjectAssetFiles` is
  the only mapper; no component combines the project root with a mount path.
- **Stored paths are mount paths.** Cook inputs, provenance, model records and
  script sources store `/Mount/...`.
- **Output folders are a rule, not a list.** Any dot-named folder below the
  project root is output. `.imported` and `.oxygen`, specified but never
  written or read, are removed from the designs, the docs and the Content
  Explorer.
- **Other projects recover; they are not updated.** A relocation updates the
  open project only. Every other project opens with its broken references
  kept and listed, also in `.build/references.json`, and is repaired from
  source control history. No redirect files are written.
- **No locks between sessions.** Each session saves only over the version it
  read and reacts to another session's change as to any outside change;
  exclusive editing is source control's job.
- **Publication identity ignores local bindings.** Overriding a folder does not
  invalidate publications; changed files are found by source facts.

## Accepted designs this touches

| Document                                            | Change                                                                                                                                                                                                                       |
| --------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `design/editor/lld/project-layout-and-templates.md` | The `Mounts` list and its kinds, mount rules (relative outside-tree paths, placeholders, the `.mounts.json` hierarchy), the output folder rule without `.imported` and `.oxygen`, the diagnostics table and acceptance tests |
| `design/editor/lld/project-services.md`             | Manifest fields, normalization, the resolved table in `ProjectContext`                                                                                                                                                       |
| `design/editor/lld/content-pipeline.md`             | Mount-path cook inputs and provenance; the "absolute local mounts" portability note                                                                                                                                          |
| `design/editor/lld/script-authoring.md`             | Source mounts replace script source roots                                                                                                                                                                                    |

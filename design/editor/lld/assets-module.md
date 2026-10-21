# Assets Module LLD

Status: `agreed`

Last Updated: 2026-10-20 01:32

## 1. Purpose

Define `Oxygen.Editor.Assets`, the editor's live view of the active project's
assets, within the architecture of
[editor module architecture](editor-module-architecture.md#71-assets): what it
owns, what it exposes, what it depends on and how it is tested.

Assets answers "what assets can this project use, what state is each one in, and
which asset does a given reference denote", for every feature that shows or
picks an asset: the content explorer, the scene inspector, the material editor,
the cooked inspector and the problems panel.

## 2. PRD Traceability

| ID         | Coverage                                                                                                  |
| ---------- | --------------------------------------------------------------------------------------------------------- |
| `GOAL-005` | One entry per logical asset, with its source, cooked, cook and preview state, for every surface.          |
| `REQ-012`  | Material slots pick materials through `AssetRule.For(AssetKind.Material)`.                                |
| `REQ-013`  | Pickers and the explorer show a material's base colour (`MaterialPreviewColor`).                          |
| `REQ-020`  | Each entry carries source, cooked and cook state; `AssetStatusText` is the one wording of it.             |
| `REQ-021`  | References resolve by `AssetPath` through `IAssetFeed.ResolveAsync`; pickers never use cooked path text.  |
| `REQ-024`  | Missing, broken, needs-cooking and preview-failed states are distinct, with the preview's failure reason. |
| `REQ-026`  | Preview availability per entry, from what the running preview acknowledged (`IAssetRuntimeStatusSource`). |

## 3. Architecture Links

- [editor-module-architecture.md](editor-module-architecture.md): the layers and
  the module's place among them.
- [asset-identity.md](asset-identity.md): identities, references and collisions,
  which the cook reports.
- [asset-family-modules.md](asset-family-modules.md): the family models, among
  them Scripting's `Script` and its facts.
- [editor-documents.md](editor-documents.md): the one file watcher and how open
  documents follow their files.
- [content-pipeline.md](content-pipeline.md),
  [content-cooking-workflows.md](content-cooking-workflows.md): cook
  publications, cook status and cook runs the feed reads.
- [live-preview.md](live-preview.md): the module that implements the preview
  port.
- [content-explorer-asset-identity.md](content-explorer-asset-identity.md): how
  the explorer shows identities.

## 4. Current Baseline

The steps that bring the code to this design, and what each has done, are
tracked in the
[issue-069 plan](../plan/issue-069-di-topology-and-decomposition.md).

## 5. Target Design

### 5.1 Responsibility

Assets composes four inputs into one entry per logical asset:

1. the project, library and engine content catalogs, read through the content
   pipeline's published cooked content (5.7);
2. cook freshness and cook activity, from the content pipeline;
3. which open documents have unsaved changes
   ([editor documents](editor-documents.md#535-status-the-user-sees));
4. the preview's acknowledged state, through the `IAssetRuntimeStatusSource`
   port, which LivePreview implements (5.6).

It applies the identity rules once (cooked companions, aliases, library
overrides), so no consumer re-derives them. It owns no collision logic:
collisions are refused by authoring and reported by the cook
([asset identity](asset-identity.md#56-collisions)).

It does not own how assets are browsed (folders, query, sort, layouts, rows: the
explorer), how they are created, moved or deleted (ContentPipeline), how they
are opened (Shell commands) or how the engine loads them (LivePreview).

### 5.2 Decisions

1. **The engine through a port.** The feed needs one value from the engine per
   entry, its preview availability. It reaches the engine through a port the
   module owns (5.6), so neither Assets nor the explorer references
   `Oxygen.Editor.Runtime`.
2. **The catalog reads a contract.** The catalog reads cook publications, whose
   format ContentPipeline owns, through a read contract (5.7). Its tests fake
   that contract instead of writing the pipeline's private files.
3. **A fixed inclusion policy.** The feed includes every asset of the project,
   its mounted libraries and the engine; it takes no filter. Searching and
   filtering belong to the explorer's query; what a property accepts is an
   `AssetRule` (5.4).
4. **Shared display vocabulary.** Display text that several features show
   (badges, status wording) is the module's shared vocabulary and lives in
   Assets as plain strings, in `AssetStatusText`. `AssetEntry` carries data
   only: its badges and their tooltips are `AssetStatusText` functions of an
   entry, and the explorer's rows present them.
5. **Script facts are not assets.** Script facts are Scripting's `Script`,
   computed by ContentPipeline with the input file checks
   ([asset family modules](asset-family-modules.md)); templates are written by
   ContentPipeline and the code editor is launched by the Shell. None of it is
   in Assets.
6. **File types are shared.** The pipeline's file types (each source suffix,
   what it is, and the cooked descriptor it cooks into) are shared with the
   native engine and the tools outside the editor, so they live in
   Oxygen.Managed.Assets as `AssetFileTypes`, mirroring the engine's `AssetType`
   values and `LooseCookedLayout` extensions. ContentPipeline, Assets, the
   explorer and the scene editor all read them from there; Assets only maps them
   to its `AssetKind` (`AssetKinds.FromFileName`).
7. **One rule type for references.** A property that references an asset says
   what it accepts with an `AssetRule` (5.4); no asset kind has a picker service
   of its own.
8. **Names say what the feed holds.** It includes the engine content and mounted
   libraries, not only the project's own assets, so it is `IAssetFeed` of
   `AssetEntry` and the catalog is `AssetCatalog`; `Project` names only what is
   relative to the project. `AssetEntry` shares its simple name with the cooked
   index record `Oxygen.Managed.Assets.Persistence.LooseCooked.AssetEntry`; only
   persistence code imports that namespace.
9. **One registration module.** Assets registers its services (catalog, feed)
   through its own registration module, which the composition root calls; the
   preview port's implementation is registered by LivePreview.

### 5.3 Public surface

```text
Oxygen.Editor.Assets              AssetCatalog, IAssetPublicationTarget, IAssetFeed, AssetFeed,
                                  AssetFeedExtensions, AssetRule, AssetEntry, AssetKind, AssetKinds,
                                  AssetState, AssetRuntimeAvailability, AssetCookActivity,
                                  AssetOrigin, AssetIdentity, AssetStatusText, AssetInformation,
                                  AssetInformationFact
Oxygen.Editor.Assets.Materials    MaterialPreviewColor
Oxygen.Editor.Assets.Runtime      IAssetRuntimeStatusSource, AssetRuntimeStatus, AssetRuntimeRequest,
                                  AssetRuntimeContentState
```

| Type                           | Notes                                                                                                                         |
| ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------- |
| `IAssetFeed`, `AssetFeed`      | `Items`, `RefreshAsync(CancellationToken)`, `ResolveAsync(AssetPath, CancellationToken)`                                      |
| `AssetEntry`                   | One logical asset; `AssetEntry.Missing(AssetPath)` stands for a reference no catalog knows                                    |
| `AssetIdentity`                | `FromCookedSource`: every identity rule in one place                                                                          |
| `AssetKinds`                   | `FromCookedType`, `FromCooked`, `FromFileName`; the explorer and the scripting inspector classify cooked types with it        |
| `AssetCatalog`                 | The asset catalog (5.7), the one file watcher; readers depend on `IAssetCatalog` and `IAssetFolderSource` from Managed.Assets |
| `IAssetPublicationTarget`      | `UsePublicationAsync`: publication owners hand the catalog the publication they accepted                                      |
| `AssetRule`                    | What a property that references an asset accepts (5.4)                                                                        |
| `AssetFeedExtensions`          | `Assets(rule, keeping)`: the feed's entries through a rule; `Find(AssetPath)`                                                 |
| `MaterialPreviewColor.TryRead` | Reading a descriptor's base colour is domain logic; the explorer's tile only paints it                                        |
| `AssetStatusText`              | The status wording and tone every surface uses (`ForEntry`, `GetText`, `GetTone`, `GetDescription`)                           |
| `IAssetRuntimeStatusSource`    | The preview port (5.6)                                                                                                        |

The reducer (`AssetIdentityReducer`) is internal: the feed creates it, and no
consumer needs another. So are the catalog's parts: `AssetCatalogSnapshot`
(records together with the publication they were read from) and
`IAssetCatalogReader` (what the feed reads from the catalog, which its unit
tests fake). The feed and catalog implementations are public so that the
registration module, and tests at that level, can construct them.

### 5.4 What a property accepts

Every picker that assigns an asset to a property (material and geometry slots,
the skybox cubemap, the metering mask, input mapping contexts, scene references
to scripts, input actions and physics sidecars, script parameters, a material's
textures) reads the feed through an `AssetRule`: the project's assets of one
kind, and whether they must already have cooked output.

There is a rule for every kind the pipeline cooks into an asset, read from the
file types (`AssetFileTypes`): material, geometry, scene, texture, script, input
action, input mapping context and physics scene. A kind added to the file types
is accepted by its properties with no code in `AssetRule`; the rule of any other
kind accepts nothing, so a script parameter of an engine type the editor does
not list offers nothing. The cooked requirement is not a fact of the kind but of
the reference: a property that stores the cooked asset the engine loads (a scene
reference, a script parameter) uses `AssetRule.ForCooked(kind)`; the others use
`AssetRule.For(kind)`, and the editor cooks what they reference when it is
needed.

```csharp
// The geometry property: the project's geometries, keeping the current one.
feed.Assets(AssetRule.For(AssetKind.Geometry), keeping: node.Geometry)
    .Subscribe(assets => this.Rows.Reset(assets));

// A scene reference: cooked content the engine loads with the scene.
AssetRule.ForCooked(AssetKind.InputAction).Apply(assets);
```

A rule never offers an asset whose file is missing or broken, or an
engine-internal one under `/Engine/Internal` (`EngineAssets.Internal`). Engine
and pak assets are offered like the project's; the shared `AssetPicker`
(`Oxygen.Editor.Pickers`) groups them as Engine, Packages and Content.
`AssetRule.Apply(assets, keeping)` orders the accepted assets by name and keeps
the property's current assignments: an assigned asset whose file is missing or
broken is listed as it is, and one no catalog knows is listed as missing
(`AssetEntry.Missing`). It never lists an asset of another kind, or an
engine-internal one.

Facts a picker shows beside an asset are small readers over an entry:
`MaterialPreviewColor.TryRead` in Assets, and the texture facts (`Texture`'s
`IsCube`, `StoresRadiance` and `HasSourceImage`) in Textures, over the
`TextureSourceReader` of Managed.Assets, which owns that format. The status beside a name is `AssetStatusText.ForEntry`.

### 5.5 What the explorer owns

The content explorer (`Oxygen.Editor.ContentExplorer`) owns browsing:
`AssetBrowserRow`, `AssetFileFacts`, `CookedLibraryFolders` (the rows a browsed
library folder shows) and `ProjectCookedFolders`, in its `Panes.Assets`
namespace beside the pane that uses them; the query and layouts; the import
dialogs; the presentation of entry badges (decision 4); and the relocation
and creation dialogs: the rename, delete and New asset dialogs and the outcomes
they show. The rules behind those dialogs are ContentPipeline's:
`IAssetRelocationService` owns a relocation's plan, transaction and history,
the moves a paste of cut items makes and the replay of undo and redo, and
`IAssetAuthoring` creates an asset from its family's template. The explorer
sends no messages: it inspects cooked output through the Shell's
`ICookedInspections`, applies a mount change through ContentPipeline's
`IContentMountChanges` and calls between its own panes. Desktop integration,
such as revealing a file or launching the code editor, is the Shell's
(`IDesktopIntegration`, `ICodeEditorService`). MaterialEditor depends on Assets,
not on the explorer.

### 5.6 The preview port

Assets needs one thing from the engine: which assets the running preview has
mounted, is loading, or failed to apply. It declares that need in its own terms.

```csharp
public interface IAssetRuntimeStatusSource
{
    event EventHandler? Changed;

    AssetRuntimeStatus GetStatus(Guid? projectId);
}

public sealed record AssetRuntimeStatus(
    AssetRuntimeContentState State,       // Unavailable, Unmounted, Updating, Mounted
    IReadOnlyList<FileSystemPath> Roots,  // mounted cooked roots
    IReadOnlyList<AssetRuntimeRequest> Requests, // current run, requested project
    string? Reason = null);

public sealed record AssetRuntimeRequest(AssetPath Asset, bool? Succeeded, string? Reason = null);
```

- The availability rule (kinds, packaged assets, mounted roots, failed and
  pending requests) is in the feed and tested with a fake source.
- The adapter, `AssetRuntimeStatusSource` in LivePreview's `Assets` folder, only
  translates: it filters requests by run and project, takes each request's asset
  path, maps the content state (a run without an id, and failed content, are
  `Unavailable`) and raises `Changed` on the three engine events (content
  status, engine state, asset status).

### 5.7 The publication seam

ContentPipeline exposes the read side of cook publications as two interfaces in
`Oxygen.Editor.ContentPipeline.Publication`:

- `ICookPublicationReader`, implemented by `CookPublicationService`:
  `AcquireReadAsync`, and `AcquireCatalogsAsync`, which returns the publication
  with the catalogs of its cooked roots. When a library's cooked index changed
  after the publication was made, `AcquireCatalogsAsync` publishes the project
  again from its current libraries and returns that publication instead.
- `ICookPublicationReadLease`, implemented by `CookPublicationReadLease`:
  `PublicationId`, `ProjectId`, `ProjectRoot`, `Roots`, `UnavailableRoots`,
  `ProjectRoots`, `CreateCatalogsAsync` (one `IAssetCatalog` per available root,
  never republishing) and `Retain`.

A changed library root raises `CookedLibraryChangedException`, which publishing
again resolves. The project's own cooked output not matching its publication is
damage and raises `InvalidDataException`; it is never republished. Which project
folders are the pipeline's output (`.cooked`, `.imported`, `.build`) is the
project layout's rule, `ProjectOutputFolders` in Oxygen.Editor.Projects; the
catalog indexes every other authoring mount.

`AssetCatalog` reads the project's publication itself through
`AcquireCatalogsAsync`, or shows the one an owner of publication acceptance (the
workspace mount, the mount change service, the publication preview) hands it
through `IAssetPublicationTarget.UsePublicationAsync`. A handed-over publication
is shown exactly: its owner may hold the project's writer, so the catalog never
republishes it.

The lease is an opaque handle. It holds the read lock on the cooked output, so
it stays an object whose lifetime the holder controls with `Retain` and
`Dispose`. Pipeline operations that take a lease back, such as
`IAssetCookStatusReader.ReadAsync`, accept the interface and reject a lease the
pipeline did not issue (`CookPublicationReadLease.Issued`). Callers that only
pass leases through, like the workspace mount code, keep the concrete types. The
composition root maps `ICookPublicationReader` to `CookPublicationService`.

### 5.8 How changes reach the editor

Assets publishes no messages. Every change to asset files reaches the editor
through one of three channels, each with one owner:

| Change                                           | Channel                                                                                                                                                                         | Consumers                                                                                                            |
| ------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| Any file added, changed, removed or renamed      | File watching in the catalogs, surfaced as `IAssetCatalog.Changes`; the feed refreshes from it, and from saves, cooks, cook status and the preview port, then publishes `Items` | Every view of assets observes `IAssetFeed.Items`                                                                     |
| A different cook publication shown               | The catalog reports the publication's cooked records as added and removed through `IAssetCatalog.Changes`                                                                       | The explorer's sources tree, which reloads its cooked folders only when the project's cooked outputs differ          |
| A move, rename, delete or creation in the editor | `IAssetRelocationService.FilesChanged` and `IAssetAuthoring.FilesChanged`, raised on the caller's context with the exact moves, rewritten, recycled and created files           | The feed; the sources tree follows moved folders; the document host closes deleted materials and retitles moved ones |

An operation that creates a file and selects or reveals it at once (a new scene,
script, material or saved copy) awaits `IAssetFeed.RefreshAsync()` first, so the
entry exists before it is shown. Nothing else asks the feed to refresh.

File watching cannot tell a move from a removal followed by an addition, so a
file deleted or moved outside the editor reaches its open document as a missing
file. The open `Document` re-checks its file (`Document.CheckFileAsync`) when
the catalog reports a change to it
([editor documents](editor-documents.md#534-external-changes-to-open-documents)):

- Missing: the document stays open and is marked `IsMissingOnDisk`, which counts
  as unsaved. The document host shows a banner with Save and Close. Saving
  writes the file again at its location; closing discards the document.
- Restored with the same content: the mark clears and the document is in step
  with its file again.
- Restored with other content: the mark clears, and saving reports the ordinary
  external-change conflict.

## 6. Ownership

| Owner                           | Owns                                                                                                          |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| `Oxygen.Editor.Assets`          | The catalog and its file watching, the feed, identity rules, `AssetRule`, `AssetStatusText`, the preview port |
| `Oxygen.Editor.ContentPipeline` | Publication format and read contract, cook status and runs, relocation, creation, texture descriptor facts    |
| `Oxygen.Editor.Projects`        | `ProjectContext`, `ProjectOutputFolders`                                                                      |
| `Oxygen.Managed.Assets`         | `IAssetCatalog`, `IAssetFolderSource`, catalog records, `AssetFileTypes`, `TextureSourceReader`               |
| `Oxygen.Editor.LivePreview`     | `AssetRuntimeStatusSource`, the preview port's implementation                                                 |
| Content explorer                | Browsing, query, layouts, rows, import dialogs, badge presentation                                            |
| `Oxygen.Editor.Pickers`         | The shared `AssetPicker` and its grouping                                                                     |

## 7. Data Contracts

- `AssetEntry`: one logical asset. Its record holds the identity
  (`AssetPath`), display name and path, kind, primary and derived state,
  runtime availability, the source, descriptor and cooked files it has, its
  GUID, diagnostic codes and whether a picker may select it. Its properties add
  the import source of an imported product, the cooked metadata it shows and
  the copies it overrides or that override it, cook status and activity, and
  what the surfaces derive from them: origin (`Engine`, `Package`, `Content`),
  `PrimaryBadge` and its tooltip, `TypeDisplayName`, `CanCook`, `CanReimport`
  and `Information`.
- `AssetKind`: `Folder`, `Material`, `Geometry`, `Scene`, `Texture`, `Image`,
  `ImportSettings`, `ForeignSource` (a model to import), `CookedData`,
  `CookedTable`, `Script`, `InputAction`, `InputMappingContext`, `InputFile`,
  `PhysicsScene` and `Unknown`; `AssetKinds` classifies files and cooked
  records into it.
- `AssetState`: `Packaged` (inside a pak: read-only, nothing to edit or cook),
  `Source`, `Descriptor`, `Cooked`, `Stale` (cooked output older than its
  source), `Missing` (a reference to nothing) and `Broken` (present but
  unreadable or invalid).
- `AssetRuntimeAvailability`, the preview's overlay on an entry:
  `NotApplicable`, `Unknown`, `NotMounted`, `Mounted`, `Unavailable` (the
  preview is not running or is incompatible), `Updating` and `Failed` (a
  current load or application failed).
- `AssetRuntimeStatus`, `AssetRuntimeRequest`, `AssetRuntimeContentState`: the
  preview port's values (5.6).
- `ICookPublicationReader`, `ICookPublicationReadLease`: the publication read
  contract (5.7).
- `AssetFilesChangedEventArgs`: the exact moves, rewritten, recycled and created
  files of an editor operation (5.8).

## 8. Commands, Services, Or Adapters

- `IAssetFeed` (`AssetFeed`) and `AssetCatalog` (as `IAssetCatalog`,
  `IAssetFolderSource`, `IAssetPublicationTarget`), one per open project,
  registered by the Assets registration module (decision 9).
- `IAssetRuntimeStatusSource`, implemented by LivePreview's
  `AssetRuntimeStatusSource` (5.6).
- No commands.

## 9. UI Surfaces

Assets has no UI. Every surface that shows an asset shows `AssetStatusText` for
it: the explorer, the pickers (`AssetPicker`), the inspectors, the material
editor and the problems panel.

## 10. Persistence And Round Trip

Assets writes no files. It reads authoring files through file watching and
cooked content through the publication seam only.

## 11. Live Sync / Cook / Runtime Behavior

Preview availability comes from the preview port (5.6); cook freshness and
activity from ContentPipeline. A handed-over publication is shown exactly and
never republished (5.7).

## 12. Operation Results And Diagnostics

A reference no catalog knows resolves to `AssetEntry.Missing` with the
`AssetIdentityDiagnosticCodes.ResolveMissing` code. A preview failure carries
the preview's reason (`AssetRuntimeRequest.Reason`). Collisions are the cook's
diagnostics ([asset identity](asset-identity.md#56-collisions)).

## 13. Dependency Rules

```text
Oxygen.Editor.Assets
  -> Oxygen.Editor.ContentPipeline   publication read side, cook status, cook runs, relocation and authoring events, engine content pak
  -> Oxygen.Editor.Projects          ProjectContext, IProjectSession, ProjectOutputFolders
  -> Oxygen.Managed.Assets           catalogs, records, file types, material authoring
  -> Oxygen.Managed.Core             AssetPath, FileSystemPath, diagnostics
  -> Storage, System.Reactive, logging
```

No WinUI, Runtime, Interop, Routing, Aura or World types. Dependents: the
content explorer, MaterialEditor, Pickers, LivePreview, WorldEditor, and their
tests.

## 14. Validation Gates

Assets follows the execution tiers of ContentPipeline's tests, as separate
projects under `tests/`. The fixtures they share live in `tests/TestSupport/`,
and no project outside Assets compiles them:

| Project                                  | Holds                                                                                                             | Dependencies                                                               |
| ---------------------------------------- | ----------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| `Oxygen.Editor.Assets.Unit.Tests`        | Identity rules, the feed's composition and availability, catalog composition over a fake publication, asset rules | Managed code, small temporary fixtures, fakes of the ContentPipeline seams |
| `Oxygen.Editor.Assets.Integration.Tests` | The feed over a real native import (`NativeImportTests`)                                                          | Matching installed SDK                                                     |
| `Oxygen.Editor.Assets.Benchmarks.Tests`  | Publishing a thousand-material project through the feed without native work (`AssetFeedWorkloadBenchmarks`)       | Managed code, temporary workspace                                          |

The unit and integration projects are the only assemblies granted its internals;
the benchmarks use the public API. No test writes the publication format:
the ContentPipeline test kit's `FakeCookPublicationReader` serves in-memory
cooked indexes through the read contract,
and ContentPipeline's own integration tests cover the format and the
stale-library diagnosis.

Elsewhere:

- The explorer's tests cover the query, layout, navigation and folder rows,
  including `CookedLibraryFoldersTests`.
- Tests in other modules that need an asset feed or a preview status stub the
  public ports (`IAssetFeed`, `IAssetRuntimeStatusSource`) with their own fakes;
  they never compile Assets' fixtures.
- Import behaviour is tested where it is owned. The explorer's tests cover the
  import review (cancel, accept, validation, collisions, replacement review).
  ContentPipeline's tests cover retention, replacement and retry for glTF and
  FBX, and what a run records. The cooking panel's tests cover Retry. One
  workspace test, `ImportReplacementSyncTests`, checks that a replaced import
  updates the open scene's consumer without an edit. No test outside the
  explorer uses its internals.
- `AssetRuntimeStatusSourceTests` live in LivePreview's unit tests, beside the
  adapter.

## 15. Open Issues

None.

# Assets module

Status: **implemented, awaiting review** (uncommitted)

Scope: `Oxygen.Editor.Assets`, extracted from `Oxygen.Editor.ContentBrowser`
within the architecture of
[editor-module-architecture.md](editor-module-architecture.md): what it owns,
what it exposes, what it depends on and how it is tested. Every boundary below
was decided before code moved, not discovered from compile errors.

## 1. Responsibility

Assets is the editor's live view of the active project's assets. It answers
"what assets can this project use, what state is each one in, and which asset
does a given reference denote", for every feature that shows or picks an asset:
the content explorer, the scene inspector, the material editor, the cooked
inspector and the problems panel.

It composes four inputs into one entry per logical asset:

1. the project, library and engine content catalogs, read through the content
   pipeline's published cooked content;
2. cook freshness and cook activity, from the content pipeline;
3. live authoring state of open documents, from the cook document registry;
4. the preview's acknowledged state, through the `IAssetRuntimeStatusSource`
   port, which LivePreview implements.

It applies the identity rules once (cooked companions, aliases, library
overrides), so no consumer re-derives them.

It does not own how assets are browsed (folders, query, sort, layouts, rows),
how they are created, moved or deleted (ContentPipeline), how they are opened
(Shell commands) or how the engine loads them (LivePreview).

## 2. Why the boundaries are where they are

- The feed needs one value from the engine per entry, its preview availability.
  The engine is reached through a port the module owns (section 5), so neither
  Assets nor the explorer references `Oxygen.Editor.Runtime`.
- The catalog reads cook publications, whose format ContentPipeline owns. It
  depends on a read contract (section 6); its tests fake that contract instead
  of writing the pipeline's private files.
- The browser filter the feed and reducer took was a fixed inclusion policy
  spelled as a parameter: every caller passed the default and the feed ignored
  it. It is replaced by that policy. Searching and filtering belong to the
  explorer's query.
- Display text that four features show (badges, status wording) is the module's
  shared vocabulary and lives in Assets as plain strings (`AssetStatusText`).
- Script facts are on World's `Script`, computed by ContentPipeline with the
  input file checks; templates are written by ContentPipeline and the code
  editor is launched by the Shell. None of it is in Assets.
- The pipeline's file types (each source suffix, what it is, and the cooked
  descriptor it cooks into) are shared with the native engine and the tools
  outside the editor, so they live in Oxygen.Managed.Assets as `AssetFileTypes`,
  mirroring the engine's `AssetType` values and `LooseCookedLayout` extensions.
  ContentPipeline, Assets, the explorer and the scene editor all read them from
  there; Assets only maps them to its `AssetKind` (`AssetKinds.FromFileName`).
- A property that references an asset says what it accepts with an `AssetRule`
  (section 3); no asset kind has a picker service of its own.

## 3. Layout and public surface

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

| Type                           | From                                                          | Notes                                                                                       |
| ------------------------------ | ------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| `IAssetFeed`, `AssetFeed`      | `IContentBrowserAssetProvider`, `ContentBrowserAssetProvider` | `Items`, `RefreshAsync(CancellationToken)`, `ResolveAsync(Uri, CancellationToken)`          |
| `AssetEntry`                   | `ContentBrowserAssetItem`                                     | One logical asset; still carries display properties (section 10)                            |
| `AssetIdentity`                | `AssetIdentityGrouping`, `CookedLibraryProjection.Create`     | `FromCookedSource`: every identity rule in one place                                        |
| `AssetKinds`                   | `AssetIdentityReducer.GetCookedKind`                          | `FromCookedType`; the explorer and the scripting inspector classify cooked types with it    |
| `AssetCatalog`                 | `ProjectAssetCatalog`                                         | The asset catalog (section 6); readers depend on `IAssetCatalog` from Oxygen.Managed.Assets |
| `IAssetPublicationTarget`      | new                                                           | `UsePublicationAsync`: publication owners hand the catalog the publication they accepted    |
| `AssetRule`                    | `IMaterialPickerService` and each picker's own filter         | What a property that references an asset accepts (section 3)                                |
| `MaterialPreviewColor.TryRead` | `MaterialPreviewReader` (internal)                            | Reading a descriptor's base colour is domain logic; the explorer's tile only paints it      |
| `AssetStatusText`              | `AssetStatusPresentation`                                     | The status wording every surface uses                                                       |
| `IAssetRuntimeStatusSource`    | new                                                           | Section 5                                                                                   |

The reducer is internal: the feed creates it, and no consumer needs another. So
are the catalog's parts: `AssetCatalogSnapshot` (records together with the
publication they were read from) and `IAssetCatalogReader` (what the feed reads
from the catalog, which its unit tests fake). The feed and catalog
implementations are public so that the composition root, and tests at that
level, can construct them, as ContentPipeline's services are today. Registration
stays in the workspace until Phase 3 gives every subsystem its registration
module; an Assets-specific registration shape now would pre-empt it.

Names say what the feed holds. It includes the engine content and mounted
libraries, not only the project's own assets, so it is `IAssetFeed` of
`AssetEntry` and the catalog is `AssetCatalog`; `Project` names only what is
relative to the project. `AssetEntry` shares its simple name with the cooked
index record `Oxygen.Managed.Assets.Persistence.LooseCooked.AssetEntry`; only
persistence code imports that namespace.

### What a property accepts

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
engine-internal one under `/Engine/Internal`. Engine and pak assets are offered
like the project's; the shared `AssetPicker` groups them as Engine, Packages and
Content. `AssetRule.Apply(assets, keeping)` orders the accepted assets by name
and keeps the property's current assignments: an assigned asset whose file is
missing or broken is listed as it is, and one no catalog knows is listed as
missing (`AssetEntry.Missing`). It never lists an asset of another kind, or an
engine-internal one. Facts a picker shows beside an asset are small readers over
an entry: `MaterialPreviewColor.TryRead` in Assets, and the texture descriptor
readers (`TextureSourceAssetImporter.ReadCubeDescriptor`, `HasSourceImage`) in
ContentPipeline, which owns that format. The status beside a name is
`AssetStatusText.ForEntry`.

## 4. What stays in the explorer

`ContentBrowser` keeps browsing: `AssetBrowserRow`, `AssetFileFacts`,
`CookedLibraryFolders` (the rows a browsed library folder shows, formerly
`CookedLibraryProjection.ForFolders`) and `ProjectCookedFolders` now live in
`Oxygen.Editor.ContentBrowser.Panes.Assets`, beside the pane that uses them. It
also keeps the query and layouts, the import dialogs, the navigation and
document request messages (until Shell commands exist), relocation and script
creation (until they move to ContentPipeline), and `IAssetShell` and code-editor
launch (until Shell desktop integration exists). `MaterialEditor` now depends on
Assets instead of the explorer.

## 5. The preview port

Assets needs one thing from the engine: which assets the running preview has
mounted, is loading, or failed to apply. It declares that need in its own terms.

```csharp
public interface IAssetRuntimeStatusSource
{
    event EventHandler? Changed;

    AssetRuntimeStatus GetStatus(Guid? projectId);
}

public sealed record AssetRuntimeStatus(
    AssetRuntimeContentState State,   // Unavailable, Unmounted, Updating, Mounted
    IReadOnlyList<string> Roots,      // mounted cooked roots
    IReadOnlyList<AssetRuntimeRequest> Requests, // current run, requested project
    string? Reason = null);

public sealed record AssetRuntimeRequest(string AssetPath, bool? Succeeded, string? Reason = null);
```

- The availability rule (kinds, packaged assets, mounted roots, failed and
  pending requests) is in the feed and tested with a fake source.
- The adapter, `AssetRuntimeStatusSource`, only translates: it filters requests
  by run and project, extracts each request's asset path, maps the content state
  (a run without an id, and failed content, are `Unavailable`) and raises
  `Changed` on the three engine events. It lives in `WorldEditor/Services`, the
  folder LivePreview is cut from, and the workspace registers it beside the
  feed.

## 6. The publication seam

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
again resolves. The project's own cooked output no longer matching its
publication is damage and raises `InvalidDataException`; it is never
republished. Which project folders are the pipeline's output (`.cooked`,
`.imported`, `.build`) is ContentPipeline's rule, `ProjectOutputFolders`; the
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

## 7. How changes reach the editor

Assets publishes no messages. Every change to asset files reaches the editor
through one of three channels, each with one owner:

| Change                                      | Channel                                                                                                                                                                         | Consumers                                                                                                   |
| ------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| Any file added, changed, removed or renamed | File watching in the catalogs, surfaced as `IAssetCatalog.Changes`; the feed refreshes from it, and from saves, cooks, cook status and the preview port, then publishes `Items` | Every view of assets observes `IAssetFeed.Items`                                                            |
| A different cook publication shown          | The catalog reports the publication's cooked records as added and removed through `IAssetCatalog.Changes`                                                                       | The explorer's sources tree, which reloads its cooked folders only when the project's cooked outputs differ |
| A move, rename or delete made in the editor | `IAssetRelocationService.FilesChanged`, raised on the caller's context with the exact moves, rewritten files and recycled paths                                                 | The sources tree follows moved folders; the document host closes deleted materials and retitles moved ones  |

An operation that creates a file and selects or reveals it at once (a new scene,
script, material or saved copy) awaits `IAssetFeed.RefreshAsync()` first, so the
entry exists before it is shown. Nothing else asks the feed to refresh.

File watching cannot tell a move from a removal followed by an addition, so a
file deleted or moved outside the editor reaches its open document as a missing
file. Each document's own service re-checks its file when the feed or the
catalog reports a change to it (`IMaterialDocumentService` and
`IProjectManagerService` with `ISceneDocumentCommandService`):

- Missing: the document stays open and is marked `IsMissingOnDisk`, which counts
  as unsaved. The document host shows a banner with Save and Close. Saving
  writes the file again at its location; closing discards the document.
- Restored with the same content: the mark clears and the document is in step
  with its file again.
- Restored with other content: the mark clears, and saving reports the ordinary
  external-change conflict.

## 8. Dependencies

```text
Oxygen.Editor.Assets
  -> Oxygen.Editor.ContentPipeline   publication read side, cook status, cook runs, cook documents, engine content pak
  -> Oxygen.Editor.Projects          ProjectContext, IProjectContextService (until Phase 4)
  -> Oxygen.Editor.World             project and scene model types
  -> Oxygen.Managed.Assets           catalogs, records, material authoring
  -> Oxygen.Managed.Core             diagnostics
  -> Storage, System.Reactive, logging
```

No WinUI, Runtime, Interop, Routing or Aura. Dependents: ContentBrowser,
MaterialEditor, WorldEditor, and their tests.

## 9. Tests

Assets follows the execution tiers of ContentPipeline's tests, as separate
projects under `tests/`. The fixtures they share live in `tests/TestSupport/`,
and no project outside Assets compiles them:

| Project                                  | Holds                                                                                                                  | Dependencies                                                               |
| ---------------------------------------- | ---------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| `Oxygen.Editor.Assets.Unit.Tests`        | Identity rules, the feed's composition and availability, catalog composition over a fake publication, material choices | Managed code, small temporary fixtures, fakes of the ContentPipeline seams |
| `Oxygen.Editor.Assets.Integration.Tests` | The feed over a real native import (`NativeImportTests`)                                                               | Matching installed SDK                                                     |
| `Oxygen.Editor.Assets.Benchmarks.Tests`  | Publishing a thousand-material project through the feed without native work (`AssetFeedWorkloadBenchmarks`)            | Managed code, temporary workspace                                          |

The unit and integration projects are the only assemblies granted its internals;
the benchmarks use the public API. No test writes the publication format:
`FakeCookPublication` serves in-memory cooked indexes through the read contract,
and ContentPipeline's own integration tests cover the format and the
stale-library diagnosis.

Elsewhere:

- The explorer's tests keep the query, layout, navigation and folder tests,
  including `CookedLibraryFoldersTests` and the query checks that the moved
  tests used to end with.
- Tests in other modules that need an asset feed or a preview status stub the
  public ports (`IAssetFeed`, `IAssetRuntimeStatusSource`) with their own fakes;
  they never compile Assets' fixtures.
- Import behaviour is tested where it is owned, not through one end-to-end
  journey. The explorer's tests cover the review (cancel, accept, validation,
  collisions, replacement review). ContentPipeline's tests cover retention,
  replacement and retry for glTF and FBX, and what a run records. The cooking
  panel's tests cover Retry. One workspace test, `ImportReplacementSyncTests`,
  checks that a replaced import updates the open scene's consumer without an
  edit. No test outside the explorer uses its internals.
- `AssetRuntimeStatusSource` tests live in WorldEditor's unit tests and move to
  LivePreview with the adapter.

## 10. Remaining work

- Move the display properties off `AssetEntry` into `AssetStatusText` and the
  explorer. It touches every surface that shows a badge.
- The ContentExplorer rename, and the relocation and creation cuts.
- Phase 3: the Assets registration module, replacing the workspace lines.

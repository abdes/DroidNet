# LivePreview module

Status: **agreed**; implementation follows section 8.

Scope: slice S1.7 of
[issue-069-di-topology-and-decomposition.md](../plan/issue-069-di-topology-and-decomposition.md):
`Oxygen.Editor.LivePreview`, the domain module that applies authoring state to
the embedded engine and gives the editor its control of the game player (the
player is a separate process, not part of LivePreview). It works within
[editor-module-architecture.md](editor-module-architecture.md) section 7.3, the
[runtime integration](../lld/runtime-integration.md) and
[live engine sync](../lld/live-engine-sync.md) LLDs, and settles P7 of the
[scenes register](scenes-module.md).

## 1. Scenarios

Actors:

- the user;
- the scene's views: the scene editor (viewports, gizmos, play bar), the Scene
  Explorer and the Inspector;
- `OpenDocuments`, which holds the open documents, and the document host, which
  shows them;
- the Content Browser and the asset feed, which show each asset's preview state;
- the cook and its publication, which replace the project's cooked content;
- the embedded engine: one per process, shared by every window;
- the player: an external process that runs the project;
- the Problems panel.

1. **Open a project.** The editor starts the engine if it is not running, with
   its startup settings, mounts the project's cooked content and opens the start
   scene; the scene editor restores the project's preview settings (frame rate,
   VSync, ground grid). If the engine cannot start or the content cannot be
   mounted, the user sees why and can retry; authoring still works.
2. **See a scene.** One scene document is open at a time, as in Unreal: opening
   another scene replaces it. The engine renders that one scene; the assets it
   loads stay in the native asset loader's cache within its memory budget, so
   reopening a scene reuses what is still loaded.
3. **Edit and see it follow.** Every edit, undo and redo shows in the viewports.
   A drag previews each step. An edit the engine rejects stays in the scene and
   is reported; the Inspector shows fields still waiting for the engine.
4. **Use an asset that is not cooked yet.** The user assigns a mesh or a
   material that needs cooking. It is cooked for preview and appears once
   mounted, with no further action.
5. **Content changes underneath.** A cook publishes new output, or the user
   changes the project's mounts. The engine swaps its content and keeps showing
   the current scene.
6. **Reload or close.** A scene reloaded from disk shows the reloaded content; a
   closed scene leaves the engine and releases its viewports.
7. **See preview state of assets.** The Content Browser shows whether each asset
   is available to the engine, loading or failed.
8. **Play.** The user presses Play. The editor saves, cooks the scene, launches
   the player (or tells a running one to play the scene), and lists the player's
   problems in the Problems panel; clicking one selects its node.
9. **Close the project.** The project's content leaves the engine; the engine
   keeps running for the next project.

## 2. What happens today

### 2.1 Where the code is

All of it is in the World Editor; the engine service and the player process are
in `Oxygen.Editor.Runtime`.

| Code                                                                                                                       | Size       | Lives in                                           | Scope                         |
| -------------------------------------------------------------------------------------------------------------------------- | ---------- | -------------------------------------------------- | ----------------------------- |
| `SceneEngineSync` (11 partials) and its coalescer, pending queue and path mappers                                          | 4.4k lines | `Services`                                         | Workspace container           |
| `ISceneEngineSync` and its value types                                                                                     | 0.6k lines | Scenes `Engine/`                                   |                               |
| `SceneContentDemandService`                                                                                                | 0.4k lines | `Services`                                         | Workspace container           |
| `AssetRuntimeStatusSource` (Assets port)                                                                                   | 81 lines   | `Services`                                         | Workspace container           |
| `WorkspacePublicationPreview` (ContentPipeline port `ICookPublicationPreview`)                                             | 97 lines   | `Workspace`                                        | One per publication           |
| `ContentMountChangeService`                                                                                                | 109 lines  | `Services`                                         | One per change                |
| Session start, mounts, content failure, initial scene (`WorkspaceViewModel` partials)                                      | ~0.4k      | `Workspace`                                        | Workspace view model          |
| `PreviewSettingsService`                                                                                                   | 437 lines  | `Workspace`                                        | App singleton                 |
| `PlaySessionService`, `PlayerConnection`, `PlaySessionSettingsService`, `PlayerSettingsSync`, `RuntimeProblemReader`, etc. | ~1.6k      | `Play`                                             | Workspace container, per run  |
| `EngineInteropExceptionPolicy`, `RuntimeOperationResults`                                                                  | 148 lines  | `Services`, `Diagnostics` (also used by viewports) |                               |
| Viewports: surfaces, views, input, gizmo, picking, selection outline                                                       | ~3.5k      | `SceneEditor` (calls `IEngineService`)             | Per scene editor and viewport |

### 2.2 How it behaves

| Concern                | Today                                                                                                                                                                                                                                                                                                                                                                                     |
| ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Who decides what shows | The Scene Explorer. When the document manager installs the opened scene, the explorer registers the document's `SceneAuthoring`, sends `SceneAuthoringLoadedMessage` and calls `SyncSceneWhenReadyAsync`. The scene editor builds its viewports when `SceneSynchronized` arrives, and the Inspector discovers the scene from `SceneAuthoringLoadedMessage` and `SceneLoadedMessage`.      |
| Opening another scene  | Replaces the open scene document, but `DocumentManager` opens the new scene in `OpenDocuments` before prompting to close the old one, closing the new one if the prompt is cancelled. The native `CreateScene` destroys the engine scene and resets its asset requests; the sync builds the new one from a full snapshot. Loaded assets stay in the native asset loader's cache.          |
| Waiting for the engine | `SyncSceneWhenReadyAsync` polls the engine state every 100 ms. A separate `StateChanged` handler resumes the requested scene when the engine reaches Running.                                                                                                                                                                                                                             |
| Keys                   | Registration and close by `DocumentId`; open scenes, pending counts and the coalescer by `SceneId`; readiness, sync and sky light by `Scene` object; the runtime target by a lifetime `Guid` that is not the document id; surfaces by `DocumentId.Value` as a raw `Guid`.                                                                                                                 |
| Session start          | `WorkspaceViewModel.OnNavigatedToAsync`: start the engine, restore preview settings, acquire and prepare the publication, mount it, hand it to the asset catalog, register the publication preview, then open the start scene. If the engine fails to start, navigation returns before opening the start scene; content failure is view model properties.                                 |
| Content changes        | ContentPipeline calls the registered `ICookPublicationPreview`, which pauses the engine content, mounts the new roots and resumes. A mount change arrives as a window message `(Expected, Candidate)`, handled by the workspace view model.                                                                                                                                               |
| Content demand         | On scene load, cooks every `OverrideSlots` geometry and material reference that is `NeedsCooking` or `OutOfDate` (ignoring `TargetedOverrides`). An Inspector assignment demands that asset only while its target nodes are exactly the selection: selecting another node cancels the cook, and nothing demands it again until the scene reloads. Driven by window messages and watchers. |
| Asset preview state    | `AssetRuntimeStatusSource` maps the engine's content snapshot and asset requests and raises `Changed` on engine threads; `AssetFeed` locks and publishes itself.                                                                                                                                                                                                                          |
| Viewports              | Talk to `IEngineService` directly: surfaces, views, input, picking, framing, selection outline, gizmo and scene helpers.                                                                                                                                                                                                                                                                  |
| Play                   | Does not use the embedded engine. It saves, cooks the scene, writes `.build/play/launch.json` over a held mount set, and launches or signals the player through `Runtime.Player`. Player logs are written directly to `OutputLogBuffer` (`Controls.OutputConsole`), and diagnostics are rendered with `Oxygen.Interop` scripting into `ScriptProblem` (`ContentBrowser`) for Problems.    |

### 2.3 What makes it stiff

1. **A panel drives the engine.** The Scene Explorer decides when the engine
   shows the scene, and the editor's own viewports and Inspector wait for
   messages the explorer caused.
2. **The port is shaped by its consumers' history.** Scenes declares
   `ISceneEngineSync`, but nothing in Scenes calls it since scene edits; its
   callers are the views. `GetDocumentScene` duplicates `OpenDocuments`, which
   document identity made the only lookup. Four key kinds remain (P7).
3. **Session work lives in a view model.** Engine start, mounts, content failure
   and the publication preview are workspace view model code (C11), and an
   engine startup failure aborts opening the start scene.
4. **Selection decides what is cooked.** An assigned asset loses its preview
   cook when the user clicks away, and undo or paste never demands one, though
   `SceneAuthoring.Changed` announces every geometry, material and component
   change.
5. **Threads and UI types leak into domain services.** `SceneEngineSync`,
   `WorkspacePublicationPreview`, `ContentMountChangeService`,
   `PlaySessionService` and `PlayerConnection` depend on WinUI's
   `HostingContext` and `DispatcherQueue`; `PlaySessionService` writes to
   WinUI's `OutputLogBuffer` and uses `ContentBrowser`'s `ScriptProblem`; and
   engine readiness is polled every 100 ms.
6. **Close is called twice and never forgotten.** The scene editor closes its
   document on close and again on dispose; closed registrations are never
   removed.

## 3. What LivePreview must not do

From the LLDs and the module architecture, unchanged:

- it never rolls back authoring: a preview failure is reported, not undone;
- it never cooks, saves or mounts as a side effect of an edit: it asks
  ContentPipeline (demand) and mounts only validated publications;
- it does not duplicate what the native engine does: asset caching and eviction
  belong to the native asset loader;
- feature UI never reaches `Oxygen.Interop`; native types stay behind Runtime;
- engine view ids and native handles are never persisted.

## 4. Decisions

1. **The engine shows the open scene document.** LivePreview follows the scene
   document in `OpenDocuments`: a domain module never references the Shell, and
   one scene document is open at a time. The document manager (the Shell in
   S1.10) replaces the open scene in three steps: prepare its close (the unsaved
   edits prompt, `IDocumentArea.PrepareCloseAsync`), open the next scene in
   `OpenDocuments`, then commit the close. A cancelled prompt opens nothing, and
   a scene that fails to open (a decode problem) keeps the current one open and
   shown. LivePreview shows the scene document opened last; the old one is
   retired on its `Closed`. The Scene Explorer, the scene editor and the
   Inspector follow the scene document through `OpenDocuments` (`Opened`,
   `Closed`) and `Document.ContentReplaced`, and only observe engine readiness
   on `IScenePreview`; they stop registering or syncing anything, and the
   `InstallSceneRequestMessage` / `SceneAuthoringLoadedMessage` /
   `SceneLoadedMessage` / `SceneReloadedMessage` chain goes. Opening another
   scene rebuilds the engine scene from a snapshot; the native asset loader
   keeps loaded assets.
2. **One key: `DocumentId` (settles P7).** Everything a view asks LivePreview is
   keyed by `DocumentId`: readiness, pending field count, sky light observation,
   outcomes and `PlaySession`. `RuntimeSceneTarget` stays inside LivePreview and
   Runtime; `SceneId` only addresses content in the engine; no `Scene` object
   crosses the public surface. Runtime's viewport surface leases
   (`ViewportSurfaceKey`, `ViewportSurfaceRequest`,
   `ReleaseDocumentSurfacesAsync`) use a Runtime-owned
   `SurfaceOwnerId(Guid Value)` (converted from `DocumentId` at the view
   boundary), keeping Runtime free of `DroidNet.Documents`
   ([scene-edits.md](scene-edits.md) section 3.9) and usable by non-document
   previews.
3. **Scenes knows no engine.** `ISceneEngineSync` and its value types leave
   Scenes, and so does the engine wire mapping (decision 9). LivePreview
   publishes the small surface the views read. Assets'
   `IAssetRuntimeStatusSource` and ContentPipeline's `ICookPublicationPreview`
   stay ports of their owners, implemented in LivePreview. This replaces "Scenes
   owns `ISceneEngineSync`", which held while the command service awaited sync
   outcomes, and item 8 of [scene-edits.md](scene-edits.md) section 7
   ("descriptor engine mappings").
4. **A preview session per open project.** LivePreview starts the engine with
   its `EngineStartupSettings` (decision 10), mounts the publication, implements
   the publication preview and applies mount changes. Engine or content failure
   is observable state (`PreviewSessionState`, `PreviewFailure`) with
   `RetryAsync`, which the workspace displays. The start scene opens through the
   document manager (the Shell in S1.10) even when the engine or content is
   unavailable, so authoring still works. The session lives in the workspace
   container until Phase 4 introduces the Project Session.
5. **The scene editor keeps Runtime for viewports.** Surfaces, views, input and
   picking need the `SwapChainPanel` and are UI by the runtime LLD. LivePreview
   uses no WinUI type, though it targets the Windows framework through Runtime,
   and it remains the only domain module that references Runtime.
   `EngineInteropExceptionPolicy` and `RuntimeOperationResults` move to Runtime,
   which owns the exceptions it classifies and the runtime/surface operation
   results that both LivePreview and the viewports publish.
6. **Play is in LivePreview.** It covers both runtimes, the embedded engine and
   the player, with `IRuntimeProblemSource` as the domain contract Problems
   reads. `ScriptProblem` and `ScriptProblemSeverity` move from the Content
   Browser to the new `Oxygen.Editor.Scripting` module (pulled forward from
   S1.8) so LivePreview and Problems use them without referencing UI. Player log
   output is raised on `PlaySession` (`LogReceived`), which the workspace
   bridges to `OutputLogBuffer` until the `Output` module lands in S1.12.
7. **Content demand follows the scene change stream.** It demands every
   reference (`SceneAssetReferences.Runtime`, covering both `OverrideSlots` and
   `TargetedOverrides`) on open and on `Document.ContentReplaced`, then follows
   `SceneAuthoring.Changed` (`NodesAdded`, `NodesRemoved`, `ComponentAdded`,
   `ComponentRemoved`, `GeometryChanged`, `MaterialSlotsChanged`), and cancels
   only when the scene stops referencing the asset or the document closes. The
   Inspector's `RequestAssignment`, the window messages and the component
   notification watchers go.
8. **Events on the owner's thread, without WinUI dispatchers.** LivePreview
   raises every public event on the owner thread
   (`SynchronizationContext.Current` captured when the scene document opens or
   when `PreviewSession` and `PlaySession` are constructed), as `SceneAuthoring`
   and `Document` do; engine-thread, cook-publication and player-process
   callbacks are marshalled inside it. LivePreview takes no `HostingContext` or
   `DispatcherQueue`. The 100 ms poll becomes a wait on
   `IEngineService.StateChanged`.
9. **The wire vocabulary is published by Interop.** The field ids are the
   command protocol of the editor's native module
   (`oxygen::interop::module::*Field`, in the Interop `*PropertyApplier.h`
   headers), not engine API. Interop publishes them as managed enums whose
   members are defined from the native constants, so no hand-written copy exists
   and a native renumbering flows through at compile time. LivePreview maps each
   `PropertyId` to its component, fields and encoding. The same holds for the
   command key strings: `PropertyDescriptor.EngineCommandKey` has no reader; the
   strings exist only as the lookup key of `SceneEngineFields` and are stored
   unread everywhere else (environment, geometry, material descriptors). Both go
   from Schemas, Scenes and the Material Editor: `PropertyDescriptor.Engine`,
   `EngineFieldMapping`, `EngineCommandKey`, the 25 `Engine` initializers and
   every command key argument (84 sites).
10. **Engine startup settings are their own concern; preview settings stay with
    the scene editor.** `EngineStartupSettings` holds what the engine must know
    before it starts: graphics, renderer, platform, timing, engine core and
    logging verbosity. It is the Runtime's `IEngineSettings`, `EngineSettings`
    and `EngineSettingsService` renamed `IEngineStartupSettings`,
    `EngineStartupSettings` and `EngineStartupSettingsService`, with logging
    verbosity moved in from the preview settings.
    `NativeEngineSession.Initialize` reads it instead of the hard-coded
    `EngineConstants.DefaultLoggingVerbosity`, and `EngineService` applies a
    change live while the engine runs, so the scene editor's verbosity control
    binds to it. It is app-wide, since one engine serves the process, and
    `EngineService.InitializeAsync` applies it, so `PreviewSession` only starts
    the engine. The preview settings (frame rate cap, VSync, always-render
    panes, ground grid) are the scene editor's view preferences
    ([scenes-module.md](scenes-module.md) decision 8): they stay in the World
    Editor and move with `SceneEditor` in S1.12. `PreviewSettingsService` loads
    the stored preferences when the scene editor opens, applies them only while
    the engine is `Ready` or `Running` (its setters throw otherwise), and
    re-applies them on `IEngineService.StateChanged` whenever the engine enters
    `Ready` or `Running`, which covers a start after `RetryAsync`. The startup
    `GraphicsSettings.EnableVSync` is the initial state; the scene editor's
    VSync toggle changes it live.

## 5. The module

`Oxygen.Editor.LivePreview` is a class library in the domain layer. It
references Scenes, Assets, ContentPipeline, Projects, Documents, World, Schemas,
Data, Managed.Core, Managed.Assets, Runtime and Interop. Nothing but the
composition root, the World Editor's workspace and the features that show
preview state references it; Scenes, Assets and ContentPipeline never do. It
grants internals to its own test projects only.

### 5.1 Folders

| Folder     | Holds                                                                                                                                                                                                                    | From                                                                                                                                           |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `Session/` | `PreviewSession`, `PreviewSessionState`, `PreviewFailure`, the publication preview, mount changes                                                                                                                        | `WorkspaceViewModel` session partials, `WorkspacePublicationPreview`, `ContentMountChangeService`                                              |
| `Scenes/`  | `IScenePreview` and its event arguments, `SkyLightObservation`; internal: the projector, document lifetimes, pending field queue, coalescer, path mappers, deferred and pending replay, asset load failures, `SceneWire` | `SceneEngineSync` and its partials and helpers; `Scenes/src/Engine` value types; `SceneEngineFields`                                           |
| `Content/` | internal: content demand                                                                                                                                                                                                 | `SceneContentDemandService`                                                                                                                    |
| `Assets/`  | `AssetRuntimeStatusSource` (implements `IAssetRuntimeStatusSource`)                                                                                                                                                      | `Services/AssetRuntimeStatusSource`                                                                                                            |
| `Play/`    | `PlaySession`, `PlaySettings`, `PlayerWindowSizeOption`, `PlayStatus`, `PlayActivity`, `PlayFailure`, `PlayStage`, `PlayerLogEventArgs`, `IRuntimeProblemSource`; internal: player connection, settings sync, reader     | `PlaySessionService`, `PlaySessionSettingsService`, `PlayerWindowSizeOption`, `PlayerConnection`, `PlayerSettingsSync`, `RuntimeProblemReader` |

What stays in the World Editor until its own slice: the viewports, viewport
state, transform tool settings and preview and ground grid settings
(`PreviewSettingsService`, `GroundGrid*`) of the scene editor, the play bar, its
view model and its presentation enums (`PlayPanel`, `PlayStatusTarget`,
`PlayStatusTone`, `PlayPrimaryAccent`), and `ScriptAnalysisService`, whose port
is S1.9.

### 5.2 Public surface

**Scene preview.** One per workspace, over the workspace's `OpenDocuments`.

```csharp
public interface IScenePreview
{
    // On the document's owner thread.
    event EventHandler<ScenePreviewEventArgs>? ReadyChanged;
    event EventHandler<ScenePreviewEventArgs>? PendingFieldsChanged;

    bool IsReady(DocumentId document);
    int PendingFieldCount(DocumentId document);
    Task<SkyLightObservation?> ObserveSkyLightAsync(DocumentId document, CancellationToken cancellationToken = default);
}

public sealed class ScenePreviewEventArgs(DocumentId document) : EventArgs
{
    public DocumentId Document { get; } = document;
}
```

- **Registration** is internal. On `OpenDocuments.Opened` for a scene document,
  LivePreview captures `SynchronizationContext.Current` as the document's owner,
  snapshots the scene and subscribes to `SceneAuthoring.Changed` and
  `Document.ContentReplaced` (registration rule of
  [scene-edits.md](scene-edits.md) section 3.7). On `ContentReplaced` it
  registers the new content; on `Closed` it retires the engine scene and forgets
  the document. During a replacement the next scene opens before the prior one
  closes (decision 1): the newly opened document becomes the shown one at once,
  and the prior one is retired when it closes. Nothing is called twice, and no
  closed entry is kept.
- **Readiness** is true once the engine accepted the full projection of the
  document's current content. A view that appears later reads `IsReady`; the
  event covers changes. `ReadyChanged` replaces `SceneSynchronized`,
  `IsSceneSynchronized` and `SceneLoadedMessage`.
- **Removed:** `RegisterDocument`, `CloseDocument`, `GetDocumentScene` (callers
  read `SceneContent.Of(document).Scene`), `SyncSceneAsync`,
  `SyncSceneWhenReadyAsync`, `GetSynchronizedTarget`.
- **Outcomes** stay operation results on `IOperationResultPublisher`, with the
  existing `SceneOperationKinds` and `LiveSyncDiagnosticCodes`; affected scope
  names the document's file.

**Preview session.** One per workspace.

```csharp
public sealed partial class PreviewSession : ObservableObject, IAsyncDisposable
{
    public PreviewSessionState State { get; }   // NotStarted, Starting, Ready, EngineUnavailable, ContentUnavailable
    public PreviewFailure? Failure { get; }     // code, title, message

    public Task<bool> StartAsync(ProjectContext project, CancellationToken cancellationToken = default);
    public Task<bool> RetryAsync(CancellationToken cancellationToken = default);
    public Task<bool> ChangeMountsAsync(ProjectContext expected, ProjectInfo candidate, CancellationToken cancellationToken = default);
}
```

- `StartAsync` runs today's order: register the publication preview, start the
  engine if needed (it applies `EngineStartupSettings`), acquire and prepare the
  publication, mount it, hand it to the asset catalog. A start that fails at the
  engine sets `EngineUnavailable`; one that fails mounting sets
  `ContentUnavailable` and suspends cooked content in the engine. Either sets
  `Failure`, publishes the operation result with today's codes, and returns
  `false` while leaving authoring working.
- `RetryAsync` reruns engine startup and mounting when `EngineUnavailable`, or
  cooked-root mounting when `ContentUnavailable`. A committed publication on
  `ICookPublicationPreview.CommittedAsync` clears `ContentUnavailable` back to
  `Ready`.
- `ChangeMountsAsync` runs inside
  `IContentCookCoordinator.RunProjectChangeAsync` on the session's
  `SynchronizationContext`: it checks that `expected` is still active, validates
  `candidate`, runs `CookPublicationService.ChangeMountsAsync`, updates the
  loaded project's mounts, re-registers the publication preview for the new
  `ProjectContext`, activates it, and updates the catalog.
- Disposal disposes the publication preview registration, and releases the
  project's cooked roots; the engine keeps running.
- The workspace keeps the window message for mount changes and calls
  `ChangeMountsAsync(message.Expected, message.Candidate)`; it shows `State` and
  `Failure` and binds Retry.

**Play.**

```csharp
public sealed partial class PlaySession : ObservableObject, IRuntimeProblemSource, IDisposable
{
    public event EventHandler<PlayerLogEventArgs>? LogReceived;

    public PlaySettings Settings { get; }
    public PlayStatus Status { get; }
    public string PlayerExecutable { get; }
    public bool IsPlayerInstalled { get; }

    public Task PlayAsync(DocumentId scene);
    public Task StopAsync();
    public Task ShutdownAsync();
    public bool BringPlayerToFront();
}

public interface IRuntimeProblemSource
{
    event EventHandler? RuntimeProblemsChanged;
    IReadOnlyList<ScriptProblem> RuntimeProblems { get; }
    (DocumentId Document, NodeId Node)? NodeOf(ScriptProblem problem);
}
```

- `IPlayWorkspace` goes. The play bar saves open documents through a save
  delegate from `DocumentHostViewModel` before calling `PlayAsync`; `PlayAsync`
  refuses with `PlayDiagnosticCodes.SaveFailed` if a document is still dirty.
  The failure names its stage (`PlayStage`: save, cook, launch, scene, or run
  for a player that fails while running) instead of a panel; the play bar maps
  stage and activity to the panel it reveals (`RevealStatus` and `StatusTarget`
  move there) and calls `BringPlayerToFront()` when the target is the player
  window.
- `LogReceived` carries player log lines, setting-refusal warnings, unexpected
  exit output, and the session-start clear (`Clear: true`), replacing the direct
  `OutputLogBuffer` dependency.
- `NodeOf` replaces `SelectProblemNodeAsync`: it resolves the problem's node in
  the played scene when the name is unique. Problems shows the document and
  selects the node through `ISceneSelectionService` (with
  `SceneExplorerViewModel` observing `ISceneSelectionService.SelectionChanged`),
  and reveals the Problems panel when `RuntimeProblemsChanged` adds an error.

**Adapters.** `AssetRuntimeStatusSource` keeps its contract. The publication
preview is internal to `PreviewSession`.

### 5.3 Interop wire vocabulary

A C++/CLI header in Interop declares one managed enum per native field enum
(`TransformField`, `NodeField`, `PerspectiveCameraField`,
`OrthographicCameraField`, `DirectionalLightField`, `LocalLightField`,
`LocalFogVolumeField`) and `EngineComponentId`, each member initialized from the
native constant. `RuntimePropertyValue` takes them typed, so
`EnginePropertyWire` loses its casts. LivePreview's internal `SceneWire` maps
`PropertyId` to component, fields and encoding (the field of view in radians)
and drives component-existence checks, detach superseding and operation-kind
mapping across all eight `EngineComponentId` values. It is built from
`SceneProperties` so it names only real properties.

## 6. Changes outside LivePreview

| Project          | Change                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| ---------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Scripting (new)  | `ScriptProblem` and `ScriptProblemSeverity` moved from the Content Browser (pulled forward from S1.8)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| Content Browser  | Uses `ScriptProblem` and `ScriptProblemSeverity` from `Scripting`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| Scenes           | `Engine/` deleted; `SceneEngineFields`, the `Engine` initializers and every command key argument removed                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| Schemas          | `PropertyDescriptor.Engine`, `EngineCommandKey` and `EngineFieldMapping` removed; the descriptor and `ValidationResult` constructors lose the key                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| Material Editor  | Command key arguments removed from `MaterialDescriptors`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| Interop          | Managed wire enums (5.3)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| Runtime          | `EngineInteropExceptionPolicy` and `RuntimeOperationResults`; typed `RuntimePropertyValue`; surface APIs keyed by `SurfaceOwnerId`; `IEngineSettings` renamed `IEngineStartupSettings` (and its class and service), gaining logging verbosity that `NativeEngineSession.Initialize` reads (decision 10)                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| World Editor     | `DocumentManager` prepares the open scene's close, opens the replacement, then commits the close; scene editor, explorer and Inspector follow `OpenDocuments`/`ContentReplaced` and `IScenePreview`; `InstallSceneRequestMessage`, `SceneAuthoringLoadedMessage`, `SceneLoadedMessage` and `SceneReloadedMessage` go; explorer observes `ISceneSelectionService.SelectionChanged`; workspace delegates to `PreviewSession` and bridges `PlaySession.LogReceived`; play bar owns save, reveal and panel mapping; `PreviewSettingsService` loses logging verbosity, applies only while the engine is `Ready` or `Running` and re-applies on `StateChanged`; `SceneEditorView.xaml` binds its verbosity control to `IEngineStartupSettings` |
| Composition root | `WithSettings<IEngineStartupSettings, EngineStartupSettingsService>` replaces `WithSettings<IEngineSettings, EngineSettingsService>` (`Program.cs`); `PreviewSession` and `PlaySession` are registered in the workspace scope                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| LLDs             | Ownership tables of [runtime-integration.md](../lld/runtime-integration.md) and [live-engine-sync.md](../lld/live-engine-sync.md) name LivePreview instead of the World Editor workspace and `SceneEngineSync`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |

## 7. Tests

`Oxygen.Editor.LivePreview.Tests` (unit, no WinUI host) and
`Oxygen.Editor.LivePreview.Integration.UI.Tests` (shared real engine, which
needs the WinUI test host's dispatcher). Shared fixtures live in
`tests/TestSupport`. Tests assert codes and ids, never message text.

**Unit.** A hand-written fake of `IEngineService` and `IRuntimeWorldCommands` in
`tests/TestSupport` records every command and lets a test complete, reject or
fail it, and drive engine states; scene documents come from the real scene kind
over a temporary project, as Scenes' tests do.

| Area           | Cases                                                                                                                                                                                                                                                                                                                                                                                                       |
| -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Registration   | Opening a scene projects it; reload (`ContentReplaced`) projects the new content; close retires it; changes raised after the snapshot are replayed, none before                                                                                                                                                                                                                                             |
| Readiness      | `IsReady` and `ReadyChanged` per document, raised on the owner context; a view created after readiness reads it                                                                                                                                                                                                                                                                                             |
| Projection     | Every `SceneChange` kind sends its commands; gesture phases preview, commit and cancel; the directional light role order; relocated assets wait for their mount                                                                                                                                                                                                                                             |
| Wire           | Every descriptor of a projected kind has a `SceneWire` entry and every entry names a descriptor; encodings (field of view radians)                                                                                                                                                                                                                                                                          |
| Engine states  | Not running: pending fields across all eight component kinds counted, superseded on component detach, and replayed on Running, without polling; faulted: outcome codes                                                                                                                                                                                                                                      |
| Content demand | Open and reload demand every stale reference (`OverrideSlots` and `TargetedOverrides`); an assignment, component add/undo or paste demands its asset; a selection change cancels nothing; removing the component, last reference or closing cancels                                                                                                                                                         |
| Session        | Start order, with `EngineStartupSettings` applied before the engine starts; engine failure sets `EngineUnavailable` and content failure sets `ContentUnavailable`, with failure code and operation result; `RetryAsync` from both states; `ChangeMountsAsync` validates candidate and updates project; publication preview pauses, mounts, resumes and clears `ContentUnavailable`; disposal releases roots |
| Assets adapter | Today's `AssetRuntimeStatusSource` cases                                                                                                                                                                                                                                                                                                                                                                    |
| Play           | Dirty document refused; stage of each failure; `LogReceived` on player output and session-start clear; `BringPlayerToFront`; `NodeOf` for a unique, duplicate and missing name; settings and settings sync (today's tests)                                                                                                                                                                                  |

Moved with their code and rewritten on the fake where they used Moq: the seven
`SceneEngineSyncTests` files, `PendingPropertySyncQueueTests`,
`AssetRuntimeStatusSourceTests`, `PlaySessionSettingsTests`,
`PlayerSettingsSyncTests` and the content demand tests. The callers' tests mock
`IScenePreview`. `MountTransactionTests` stay in the World Editor's Unit.UI
suite on the public `PreviewSession`: they compose ContentPipeline's
publication, whose internals LivePreview's tests do not see.

**Integration.** One shared engine for the suite: a scene opens and becomes
ready; an edit reaches the engine (observed node state); a publication swap
keeps the scene ready; a cook stopped while its publication preview is being
captured, by closing the project or cancelling the cook, keeps the prior
publication selected and mounted until the next cook. The cross-module workflows
of the World Editor's Integration.UI suite stay there, on the new API; they
observe the engine through a probe of the public `IEngineService`.

## 8. Sequencing

Each step builds the touched projects and runs their tests, filtered to the
changed code; steps that change what the editor shows end with the reviewer's
editor build.

1. **Prerequisites in Scripting and Runtime.** Move `ScriptProblem` and
   `ScriptProblemSeverity` from the Content Browser to
   `Oxygen.Editor.Scripting`; move `EngineInteropExceptionPolicy` and
   `RuntimeOperationResults` to `Runtime`; key Runtime's surface APIs by
   `SurfaceOwnerId`; rename `IEngineSettings` to `IEngineStartupSettings`, move
   logging verbosity into it, read it in `NativeEngineSession.Initialize`, and
   rebind the scene editor's verbosity control.
2. **Create and move core.** Create `Oxygen.Editor.LivePreview` and its test
   projects; `git mv` of `Scenes/src/Engine`, `SceneEngineSync*` and its
   helpers, `AssetRuntimeStatusSource`, `WorkspacePublicationPreview`, and
   `ContentMountChangeService`, switching internal dispatch from
   `HostingContext` to `SynchronizationContext` and keeping type names so the
   compiler checks the move; move their tests.
3. **Wire vocabulary.** Interop enums, `SceneWire` (covering all eight component
   kinds in pending and deferred replay), typed `RuntimePropertyValue`; removal
   from Schemas, Scenes and the Material Editor (decision 9).
4. **Follow the open document.** `DocumentManager` prepares the open scene's
   close, opens the next scene in `OpenDocuments`, then commits the close;
   `IScenePreview`, registration from `OpenDocuments` and
   `Document.ContentReplaced`, `DocumentId` keys, owner-thread events, no
   polling; the explorer, scene editor and Inspector follow
   `OpenDocuments`/`ContentReplaced` and `IScenePreview`, and the message chain
   goes. Editor check.
5. **Content demand on the change stream.** Rewrite `SceneContentDemandService`
   onto `OpenDocuments`, `Document.ContentReplaced`, `SceneAuthoring.Changed`
   (`NodesAdded`, `NodesRemoved`, `ComponentAdded`, `ComponentRemoved`,
   `GeometryChanged`, `MaterialSlotsChanged`) and
   `SceneAssetReferences.Runtime`, dropping `IDocumentArea`, `IMessenger` and
   `HostingContext`, and `git mv` it and its tests into `LivePreview/Content`.
6. **Preview session** out of the workspace view model; `EngineUnavailable` and
   `ContentUnavailable` with `RetryAsync`;
   `ChangeMountsAsync(expected, candidate)`; the start scene opens through the
   document manager even if preview fails; `PreviewSettingsService` applies its
   preferences only while the engine is `Ready` or `Running` and re-applies them
   on `StateChanged`. Editor check.
7. **Play.** Cut `IPlayWorkspace`, `PlayPanel`, `PlayStatusTarget`,
   `OutputLogBuffer` and `HostingContext`/`DispatcherQueue` from Play and
   `git mv` Play and its tests into `LivePreview/Play`; add `PlayStage`,
   `LogReceived`, `BringPlayerToFront()` and `NodeOf`; `SceneExplorerViewModel`
   observes `ISceneSelectionService.SelectionChanged`, the workspace bridges
   `LogReceived` and runtime-error panel reveal, and the play bar takes save,
   reveal and the panel mapping. Editor check.
8. **Renames and documents.** `SceneEngineSync` becomes the internal
   `ScenePreview`, `PlaySessionService` `PlaySession`,
   `PlaySessionSettingsService` `PlaySettings`; namespaces follow the folders.
   Update the LLD ownership tables, [scenes-module.md](scenes-module.md) P7 and
   the #69 plan entry.

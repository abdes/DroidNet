# LivePreview Module LLD

Status: `agreed`

Last Updated: 2026-10-20 01:32

## 1. Purpose

Define `Oxygen.Editor.LivePreview`, the domain module that applies authoring
state to the embedded engine and gives the editor its control of the game
player: the preview session of the open project, the preview of the open scene
document, content demand, the assets' preview state, and Play. The player is a
separate process, not part of LivePreview.

The engine lifecycle, surfaces and views are in the
[runtime integration](runtime-integration.md) LLD, and the projection protocol
(payloads, ordering, coalescing, outcome classification) is in the
[live engine sync](live-engine-sync.md) LLD. This LLD is the module design: what
LivePreview owns, its public surface, the ports it implements, its sessions and
Play.

## 2. PRD Traceability

| ID         | Coverage                                                                                                   |
| ---------- | ---------------------------------------------------------------------------------------------------------- |
| `GOAL-003` | The engine shows the open scene document and follows every edit, undo and redo.                            |
| `REQ-008`  | The scene preview follows each scene document's change stream while the engine runs, and replays on start. |
| `REQ-019`  | The preview session mounts the project's publication and swaps it when a cook publishes.                   |
| `REQ-022`  | Engine start, mount, sync and Play failures publish operation results and show as session or Play state.   |
| `REQ-024`  | Failures carry the code of their domain: runtime start, asset mount, live sync, or the Play stage.         |
| `REQ-026`  | Content demand cooks the assets the open scene references, so they appear once mounted.                    |
| `REQ-039`  | The publication preview pauses the engine's content, mounts the new roots and resumes.                     |
| `REQ-044`  | Play cooks the saved scene, runs it in the player, streams its logs and reports its problems with nodes.   |

## 3. Architecture Links

- [editor-module-architecture.md](editor-module-architecture.md#73-livepreview):
  the layers, LivePreview's place among the domain modules and its ownership
  row.
- [runtime-integration.md](runtime-integration.md#7-ownership): engine
  lifecycle, startup settings, surface leases and views (Runtime).
- [live-engine-sync.md](live-engine-sync.md#6-ownership): the projection
  protocol the scene preview implements.
- [editor-documents.md](editor-documents.md): open documents, their histories
  and the Shell's document service that opens and replaces scenes.
- [scenes-module.md](scenes-module.md), [scene-edits.md](scene-edits.md):
  `SceneAuthoring`, its change stream, the typed ids and the scene editor's view
  preferences.
- [assets-module.md](assets-module.md): the `IAssetRuntimeStatusSource` port.
- [content-pipeline.md](content-pipeline.md),
  [content-cooking-workflows.md](content-cooking-workflows.md): cook,
  publication, `ICookPublicationPreview` and mount changes.
- [asset-family-modules.md](asset-family-modules.md): Scripting, which owns
  `ScriptProblem` and the `IScriptAnalyzer` port.
- [diagnostics-operation-results.md](diagnostics-operation-results.md):
  operation results and codes.

## 4. Current Baseline

The remaining work that brings the code to this design is tracked in the
[issue-069 plan](../plan/issue-069-di-topology-and-decomposition.md).

## 5. Target Design

### 5.1 Scenarios

Actors:

- the user;
- the scene's views: the scene editor (viewports, gizmos, play bar), the Scene
  Explorer and the Inspector;
- `OpenDocuments`, which holds the open documents, and the Shell's document
  service, which opens and shows them;
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

### 5.2 What LivePreview must not do

- It never rolls back authoring: a preview failure is reported, not undone.
- It never cooks, saves or mounts as a side effect of an edit: it asks
  ContentPipeline (demand) and mounts only validated publications.
- It does not duplicate what the native engine does: asset caching and eviction
  belong to the native asset loader.
- Feature UI never reaches `Oxygen.Interop`; native types stay behind Runtime.
- Engine view ids and native handles are never persisted.

### 5.3 Decisions

1. **The engine shows the open scene document.** LivePreview follows the scene
   document in `OpenDocuments`: a domain module never references the Shell, and
   one scene document is open at a time. The Shell's document service replaces
   the open scene in three steps: prepare its close (the unsaved edits prompt,
   `IDocumentArea.PrepareCloseAsync`), open the next scene in `OpenDocuments`,
   then commit the close. A cancelled prompt opens nothing, and a scene that
   fails to open (a decode problem) keeps the current one open and shown.
   LivePreview shows the scene document opened last; the old one is retired on
   its `Closed`. The Scene Explorer, the scene editor and the Inspector follow
   the scene document through `OpenDocuments` (`Opened`, `Closed`) and
   `Document.ContentReplaced`, and only observe engine readiness on
   `IScenePreview`; they register and sync nothing. Opening another scene
   rebuilds the engine scene from a snapshot; the native asset loader keeps
   loaded assets.
2. **One key: `DocumentId`.** Everything a view asks LivePreview is keyed by
   `DocumentId`: readiness, pending field count, sky light observation, outcomes
   and `PlaySession`. `RuntimeSceneTarget` stays inside LivePreview and Runtime;
   `SceneId` only addresses content in the engine; no `Scene` object crosses the
   public surface. Runtime's viewport surface leases (`ViewportSurfaceKey`,
   `ViewportSurfaceRequest`, `ReleaseDocumentSurfacesAsync`) use a Runtime-owned
   `SurfaceOwnerId(Guid Value)`, converted from `DocumentId` at the view
   boundary, which keeps Runtime free of `DroidNet.Documents` and usable by
   non-document previews.
3. **Scenes knows no engine.** Scenes declares no engine port and holds no
   engine wire mapping (decision 9). LivePreview publishes the small surface the
   views read (`IScenePreview`). Assets' `IAssetRuntimeStatusSource`,
   ContentPipeline's `ICookPublicationPreview` and Scripting's `IScriptAnalyzer`
   are ports of their owners, implemented in LivePreview. Scene edit commands
   never await engine outcomes.
4. **A preview session per open project.** `PreviewSession` starts the engine
   (Runtime applies the engine startup settings, decision 10), mounts the
   publication, implements the publication preview and applies mount changes.
   Engine or content failure is observable state (`PreviewSessionState`,
   `PreviewFailure`) with `RetryAsync`, which the workspace displays. The start
   scene opens through the Shell's document service even when the engine or
   content is unavailable, so authoring still works. The session lives in the
   Project Session scope, one per open project.
5. **The scene editor keeps Runtime for viewports.** Surfaces, views, input and
   picking need the `SwapChainPanel` and are UI by the runtime LLD. LivePreview
   uses no WinUI type, though it targets the Windows framework through Runtime,
   and it is the only domain module that references Runtime. Runtime owns
   `EngineInteropExceptionPolicy`, which classifies the exceptions Runtime
   raises, and `RuntimeOperationResults`, the runtime and surface operation
   results that both LivePreview and the viewports publish.
6. **Play is in LivePreview.** It covers both runtimes, the embedded engine and
   the player, with `IRuntimeProblemSource` as the domain contract Problems
   reads. `ScriptProblem` and `ScriptProblemSeverity` belong to
   `Oxygen.Editor.Scripting`, so LivePreview and Problems use them without
   referencing UI. Player log output is raised on `PlaySession` as
   `LogReceived`, which the `Output` module shows.
7. **Content demand follows the scene change stream.** It demands every
   reference (`SceneAssetReferences.Runtime`, covering both `OverrideSlots` and
   `TargetedOverrides`) on open and on `Document.ContentReplaced`, then follows
   `SceneAuthoring.Changed` (`NodesAdded`, `NodesRemoved`, `ComponentAdded`,
   `ComponentRemoved`, `GeometryChanged`, `MaterialSlotsChanged`), and cancels
   only when the scene stops referencing the asset or the document closes.
   Selection never demands or cancels anything, and no view requests demand.
8. **Events on the owner's thread, without WinUI dispatchers.** LivePreview
   raises every public event on the owner thread, as `SceneAuthoring` and
   `Document` do. The internal `OwnerThread` captures
   `SynchronizationContext.Current` and the managed thread id when a scene
   document opens or when `PreviewSession` and `PlaySession` are constructed;
   engine-thread, cook-publication and player-process callbacks are marshalled
   through it. A capture without a context, as in a unit test, runs inline.
   LivePreview takes no `HostingContext` or `DispatcherQueue`, and waits for the
   engine on `IEngineService.StateChanged`, never by polling.
9. **The wire vocabulary is published by Interop.** The field ids are the
   command protocol of the editor's native module
   (`oxygen::interop::module::*Field`, in the Interop `*PropertyApplier.h`
   headers), not engine API. Interop publishes them as managed enums whose
   members are defined from the native constants, so a native renumbering flows
   through at compile time (section 5.8). LivePreview maps each `PropertyId` to
   its component, fields and encoding. Schemas, Scenes and the Material Editor
   carry no engine mapping and no command key: property descriptors describe
   authoring only.
10. **Engine startup settings are their own concern; preview settings belong to
    the scene editor.** `IEngineStartupSettings` (Runtime, persisted by
    `EngineStartupSettingsService`) holds what the engine must know before it
    starts: graphics, renderer, platform, timing, engine core and logging
    verbosity. `NativeEngineSession.Initialize` reads it, and `EngineService`
    applies a verbosity change live while the engine runs, so the scene editor's
    verbosity control binds to it. It is app-wide, since one engine serves the
    process, and `EngineService.InitializeAsync` applies it, so `PreviewSession`
    only starts the engine. The preview settings (frame rate cap, VSync,
    always-render panes, ground grid) are the scene editor's view preferences
    ([scenes-module.md](scenes-module.md)) and belong to the SceneEditor module.
    `PreviewSettingsService` loads the stored preferences when the scene editor
    opens, applies them only while the engine is `Ready` or `Running` (its
    setters throw otherwise), and re-applies them on
    `IEngineService.StateChanged` whenever the engine enters `Ready` or
    `Running`, which covers a start after `RetryAsync`. The startup
    `GraphicsSettings.EnableVSync` is the initial state; the scene editor's
    VSync toggle changes it live.

### 5.4 The module

`Oxygen.Editor.LivePreview` is a class library in the domain layer. It
references Scenes, Assets, ContentPipeline, Projects, Documents
(`Oxygen.Editor.Documents` and `DroidNet.Documents`), World, Schemas, Scripting,
Data, Managed.Core, Managed.Assets, Runtime and Interop. Scripting provides
`ScriptProblem` and `ScriptProblemSeverity` and the `IScriptAnalyzer` port
LivePreview implements. Nothing but the composition of the project's scope and
the features that show preview state references LivePreview; Scenes, Assets and
ContentPipeline never do. It grants internals to its own test projects only.

| Folder     | Holds                                                                                                                                                                                                                                                             |
| ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| root       | internal `OwnerThread`                                                                                                                                                                                                                                            |
| `Session/` | `PreviewSession`, `PreviewSessionState`, `PreviewFailure`; internal: the publication preview, mount changes, runtime bindings                                                                                                                                     |
| `Scenes/`  | `IScenePreview`, `ScenePreviews` (factory), `ScenePreviewEventArgs`, `SkyLightObservation`; internal: `ScenePreview` (projector, document lifetimes, pending field queue, coalescer, path mappers, deferred and pending replay, asset load failures), `SceneWire` |
| `Content/` | `ContentDemands` (starts the demand); internal: `SceneContentDemand`                                                                                                                                                                                              |
| `Assets/`  | `AssetRuntimeStatusSource` (implements `IAssetRuntimeStatusSource`)                                                                                                                                                                                               |
| `Play/`    | `PlaySession`, `PlaySettings`, `PlayerWindowSizeOption`, `PlayStatus`, `PlayActivity`, `PlayFailure`, `PlayStage`, `PlayerLogEventArgs`, `IRuntimeProblemSource`; internal: player connection, settings sync, runtime problem reader                              |

Outside LivePreview: the viewports, viewport state, transform tool settings,
preview and ground grid settings (`PreviewSettingsService`, `GroundGrid*`), the
play bar (`ScenePlayToolbarViewModel`) and its presentation enums (`PlayPanel`,
`PlayStatusTarget`, `PlayStatusTone`, `PlayPrimaryAccent`) belong to the
SceneEditor module; script analysis is Scripting's `IScriptAnalyzer` port, whose
native call LivePreview implements.

### 5.5 Scene preview

One per project scope, over its `OpenDocuments`, created by
`ScenePreviews.Create(IEngineService, OpenDocuments, ...)`.

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
  LivePreview captures the document's owner thread, snapshots the scene and
  subscribes to `SceneAuthoring.Changed` and `Document.ContentReplaced`
  (registration rule of the change stream in [scene-edits.md](scene-edits.md)).
  On `ContentReplaced` it registers the new content; on `Closed` it retires the
  engine scene and forgets the document. During a replacement the next scene
  opens before the prior one closes (decision 1): the newly opened document
  becomes the shown one at once, and the prior one is retired when it closes.
  Nothing is called twice, and no closed entry is kept.
- **Readiness** is true once the engine accepted the full projection of the
  document's current content. A view that appears later reads `IsReady`; the
  event covers changes.
- **Pending fields** are the fields of the shown document still waiting for the
  engine (not running, or deferred until a component or asset exists); the
  Inspector reads `PendingFieldCount` and follows `PendingFieldsChanged`.
- **Outcomes** are operation results on `IOperationResultPublisher`, with
  `SceneOperationKinds` and `LiveSyncDiagnosticCodes` (`Oxygen.Managed.Core`);
  the affected scope names the document's file.

### 5.6 Preview session

One per open project.

```csharp
public sealed partial class PreviewSession : ObservableObject, IAsyncDisposable
{
    public PreviewSessionState State { get; }   // NotStarted, Starting, Ready, EngineUnavailable, ContentUnavailable
    public PreviewFailure? Failure { get; }     // Code, Title, Message

    public Task<bool> StartAsync(ProjectContext project, CancellationToken cancellationToken = default);
    public Task<bool> RetryAsync(CancellationToken cancellationToken = default);
    public Task<bool> ChangeMountsAsync(ProjectContext expected, ProjectInfo candidate, CancellationToken cancellationToken = default);
}
```

- `StartAsync` registers the publication preview with `CookPublicationService`,
  starts the engine if it does not run (Runtime applies the startup settings),
  acquires and prepares the publication, mounts it in the engine, and hands it
  to the asset catalog (`IAssetPublicationTarget`). A start that fails at the
  engine sets `EngineUnavailable`; one that fails mounting sets
  `ContentUnavailable` and suspends cooked content in the engine. Either sets
  `Failure`, publishes an operation result (engine: `OXE.RUNTIME.START_FAILED`;
  content: `AssetMountDiagnosticCodes.RefreshFailed`), and returns `false` while
  leaving authoring working.
- `RetryAsync` reruns engine startup and mounting when `EngineUnavailable`, or
  cooked-root mounting when `ContentUnavailable`. A committed publication on
  `ICookPublicationPreview.CommittedAsync` clears `ContentUnavailable` back to
  `Ready` (`OnContentRecovered`).
- `ChangeMountsAsync` runs inside
  `IContentCookCoordinator.RunProjectChangeAsync` on the session's owner thread:
  it checks that `expected` is still active, builds the candidate context, runs
  `CookPublicationService.ChangeMountsAsync`, updates the loaded project's
  mounts, re-registers the publication preview for the new `ProjectContext`,
  activates it, and updates the catalog. It returns `false` when another project
  became active first.
- Disposal disposes the publication preview registration and releases the
  project's cooked roots; the engine keeps running.
- The workspace forwards a confirmed mount change as
  `ChangeMountsAsync(expected, candidate)`, shows `State` and `Failure`, and
  binds Retry.

### 5.7 Play

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

- **Saving is the caller's.** The play bar saves the open documents through the
  ordinary Save (a save delegate from the document service) before calling
  `PlayAsync`; `PlayAsync` refuses with `PlayDiagnosticCodes.SaveFailed` if a
  document is still dirty, because Play cooks what is saved.
- **Run.** `PlayAsync` cooks the scene (`CookCurrentSceneAsync`), writes
  `.build/play/launch.json` over a held mount set, and launches the player or
  tells the running one to play the scene. A running session keeps running until
  the new scene is cooked; a Play that fails ends the player.
- **Failures name their stage** (`PlayStage`: `Save`, `Cook`, `Launch`, `Scene`,
  or `Run` for a player that fails while running) and their code
  (`PlayDiagnosticCodes`), never a panel. The play bar maps stage and activity
  to the panel it reveals (`RevealStatus`, `StatusTarget`) and calls
  `BringPlayerToFront()` when the target is the player window.
- **`LogReceived`** carries player log lines, setting-refusal warnings,
  unexpected exit output, and the session-start clear (`Clear: true`); the
  `Output` module appends them to its log.
- **Runtime problems.** `NodeOf` resolves a runtime problem's node in the played
  scene when its name is unique there. Problems shows the document and selects
  the node through `ISceneSelectionService` (the Scene Explorer follows
  `ISceneSelectionService.SelectionChanged`), and reveals itself when
  `RuntimeProblemsChanged` adds an error.
- **Preferences.** `PlaySettings` holds the per-user player preferences (window
  size, frame rate limit), stored app-wide through `IEditorSettingsManager`; the
  internal settings sync applies changes to a running player.

### 5.8 Interop wire vocabulary

`Oxygen.Editor.Interop/src/World/WireVocabulary.h` declares one managed enum per
native field enum (`TransformField`, `NodeField`, `PerspectiveCameraField`,
`OrthographicCameraField`, `DirectionalLightField`, `LocalLightField`,
`LocalFogVolumeField`) and `EngineComponentId`, each member initialized from the
native constant.

Runtime keeps one managed copy of the component ids, `RuntimeComponentId`, so
its public commands load without Interop; the native transport converts between
the two, and a Runtime test keeps their names and values identical.
`RuntimePropertyValue(RuntimeComponentId ComponentId, ushort FieldId, float Value)`
carries a field id as the Interop field enum's value.

LivePreview's internal `SceneWire` maps each `PropertyId` to a
`RuntimeComponentId`, its field ids (from the Interop field enums) and their
encoding (the field of view in radians). It drives component-existence checks,
detach superseding and operation-kind mapping across all eight component ids,
and is built from `SceneProperties` so it names only real properties. Geometry
and the scene environment have no entry: the engine takes a geometry asset with
its component and the environment as a whole.

## 6. Ownership

| Concern                                                          | Owner                                                    |
| ---------------------------------------------------------------- | -------------------------------------------------------- |
| Preview session: engine start, mounts, publication preview       | LivePreview `PreviewSession`                             |
| Projection of the open scene document, readiness, pending fields | LivePreview `IScenePreview` (protocol: live engine sync) |
| Content demand for preview                                       | LivePreview `Content/`                                   |
| Assets' preview state                                            | LivePreview `AssetRuntimeStatusSource` (port: Assets)    |
| Native script analysis                                           | LivePreview adapter (port: Scripting `IScriptAnalyzer`)  |
| Play, player connection, runtime problems                        | LivePreview `Play/`                                      |
| Engine lifecycle, startup settings, surfaces, views, wire copy   | Runtime                                                  |
| Wire vocabulary                                                  | Interop                                                  |
| Viewports, preview settings, play bar                            | SceneEditor                                              |
| Opening, replacing and closing scene documents                   | Shell document service                                   |
| Player log view                                                  | Output                                                   |
| Runtime problem list and node selection                          | Problems                                                 |
| `PreviewSession` and `PlaySession` lifetime                      | Project Session scope                                    |

## 7. Data Contracts

- `ScenePreviewEventArgs(DocumentId)`;
  `SkyLightObservation(bool Observed, bool Enabled, SkyLightUnavailableReason UnavailableReason, bool EmptyCapture)`.
- `PreviewSessionState`;
  `PreviewFailure(string Code, string Title, string Message)`.
- `PlayStatus` (activity, scene document, scene name, failure), `PlayActivity`,
  `PlayFailure`, `PlayStage`,
  `PlayerLogEventArgs(Timestamp, Level, Channel, Message, Clear)`.
- `ScriptProblem`, `ScriptProblemSeverity` (Scripting).
- `RuntimeComponentId`, `RuntimePropertyValue`, `SurfaceOwnerId(Guid Value)`
  (Runtime).

## 8. Commands, Services, Or Adapters

| Item                       | Kind                                                |
| -------------------------- | --------------------------------------------------- |
| `ScenePreviews.Create`     | Factory of the project scope's `IScenePreview`      |
| `ContentDemands.Start`     | Starts content demand; disposing it cancels demands |
| `PreviewSession`           | Session service                                     |
| `PlaySession`              | Play service; implements `IRuntimeProblemSource`    |
| `AssetRuntimeStatusSource` | Adapter of Assets' `IAssetRuntimeStatusSource`      |
| Publication preview        | Internal adapter of `ICookPublicationPreview`       |
| Script analyzer            | Adapter of Scripting's `IScriptAnalyzer`            |

## 9. UI Surfaces

None in LivePreview. The workspace shows `PreviewSession.State` and `Failure`
with Retry; the scene editor, Scene Explorer and Inspector read `IScenePreview`;
the play bar binds `PlaySession`; Output and Problems read `LogReceived` and
`IRuntimeProblemSource`.

## 10. Persistence And Round Trip

LivePreview persists no authoring data and never persists engine view ids or
native handles. `PlaySettings` is a per-user editor preference; the player's
launch file `.build/play/launch.json` is build output rewritten on every Play.

## 11. Live Sync / Cook / Runtime Behavior

The projection protocol is in [live-engine-sync.md](live-engine-sync.md) and the
engine lifecycle in [runtime-integration.md](runtime-integration.md). Within
them: a preview failure never changes authoring; content demand asks
ContentPipeline and never mounts; mounts change only through validated
publications (`ICookPublicationPreview`) and `ChangeMountsAsync`; Play uses the
player, never the embedded engine.

## 12. Operation Results And Diagnostics

- Scene preview: `SceneOperationKinds` with `LiveSyncDiagnosticCodes`, scoped to
  the document's file.
- Preview session: `RuntimeOperationResults` with `OXE.RUNTIME.START_FAILED` or
  `AssetMountDiagnosticCodes.RefreshFailed`; the same code is in
  `PreviewFailure.Code`.
- Play: `PlayDiagnosticCodes` (`SaveFailed`, `CookFailed`, `PlayerMissing`,
  `LaunchFailed`, `SceneFailed`, `SceneIssue`, `SessionEndedUnexpectedly`).
- Tests assert codes and ids, never message text.

## 13. Dependency Rules

- LivePreview is the only domain module that references Runtime and Interop.
- Scenes, Assets and ContentPipeline never reference LivePreview; LivePreview
  implements their ports.
- LivePreview references no WinUI or UI module, no `HostingContext` and no
  `DispatcherQueue`.
- Runtime never references `DroidNet.Documents`; surfaces are keyed by
  `SurfaceOwnerId`.
- No view registers, syncs or polls; views read `IScenePreview`.

## 14. Validation Gates

`Oxygen.Editor.LivePreview.Tests` (unit, no WinUI host) and
`Oxygen.Editor.LivePreview.Integration.UI.Tests` (one shared real engine, which
needs the WinUI test host's dispatcher). Shared fixtures live in
`tests/TestSupport`: a hand-written fake of `IEngineService` and
`IRuntimeWorldCommands` records every command, lets a test complete, reject or
fail it, and drives engine states; scene documents come from the real scene kind
over a temporary project. Callers' tests mock `IScenePreview`. Tests that
compose ContentPipeline's publication with the session use the public
`PreviewSession`.

| Area           | Cases                                                                                                                                                                                                                                                                                                                                                                                                            |
| -------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Registration   | Opening a scene projects it; reload (`ContentReplaced`) projects the new content; close retires it; changes raised after the snapshot are replayed, none before                                                                                                                                                                                                                                                  |
| Readiness      | `IsReady` and `ReadyChanged` per document, raised on the owner context; a view created after readiness reads it                                                                                                                                                                                                                                                                                                  |
| Projection     | Every `SceneChange` kind sends its commands; gesture phases preview, commit and cancel; the directional light role order; relocated assets wait for their mount                                                                                                                                                                                                                                                  |
| Wire           | Every descriptor of a projected kind has a `SceneWire` entry and every entry names a descriptor; encodings (field of view radians); `RuntimeComponentId` matches `EngineComponentId`                                                                                                                                                                                                                             |
| Engine states  | Not running: pending fields across all eight component kinds counted, superseded on component detach, and replayed on Running, without polling; faulted: outcome codes                                                                                                                                                                                                                                           |
| Content demand | Open and reload demand every stale reference (`OverrideSlots` and `TargetedOverrides`); an assignment, component add or undo, or paste demands its asset; a selection change cancels nothing; removing the component, the last reference or closing cancels                                                                                                                                                      |
| Session        | Start order, with startup settings applied before the engine starts; engine failure sets `EngineUnavailable` and content failure sets `ContentUnavailable`, with failure code and operation result; `RetryAsync` from both states; `ChangeMountsAsync` validates the candidate and updates the project; the publication preview pauses, mounts, resumes and clears `ContentUnavailable`; disposal releases roots |
| Assets adapter | Status from the engine's content snapshot and asset requests; `Changed` on engine state changes                                                                                                                                                                                                                                                                                                                  |
| Play           | Dirty document refused; stage of each failure; `LogReceived` on player output and session-start clear; `BringPlayerToFront`; `NodeOf` for a unique, duplicate and missing name; settings and settings sync                                                                                                                                                                                                       |

**Integration.** One shared engine for the suite: a scene opens and becomes
ready; an edit reaches the engine (observed node state); a publication swap
keeps the scene ready; a cook stopped while its publication preview is being
captured, by closing the project or cancelling the cook, keeps the prior
publication selected and mounted until the next cook. Cross-module workflows
observe the engine through a probe of the public `IEngineService`.

## 15. Open Issues

None.

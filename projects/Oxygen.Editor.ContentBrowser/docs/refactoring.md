# Content Browser Refactoring Plan

This document captures a refactoring direction for `Oxygen.Editor.ContentBrowser` so it can evolve into the UI side of the Oxygen asset/content pipeline.

## Goals

- Align project structure with architecture (shell/panes/application/infrastructure), not with file types.
- Prepare for `Oxygen.Assets` integration (VirtualPath/AssetKey, catalog queries, cook/validate/diagnostics) without forcing the UI to know pipeline mechanics.
- Reduce stringly-typed coupling (paths, selection, routing query params) to a small boundary.
- Improve correctness and scalability for large projects (moves/renames, incremental updates, virtualization readiness).

## Current implementation (what we observed)

- The shell `ContentBrowserViewModel` currently bundles multiple responsibilities: routing setup, DI child-container, navigation history, breadcrumbs, and starting indexing.
  - See `ContentBrowserViewModel` in [projects/Oxygen.Editor.ContentBrowser/src/Shell/ContentBrowserViewModel.cs](../src/Shell/ContentBrowserViewModel.cs).
- The assets feature uses `IAssetIndexingService` as the main data source.
  - Indexing + live file watching + query are all implemented in one class (`AssetsIndexingService`).
  - The storage backing is a `ConcurrentBag<GameAsset>` which is not suitable for deterministic removals/moves.
  - See [projects/Oxygen.Editor.ContentBrowser/src/Infrastructure/Assets/AssetsIndexingService.cs](../src/Infrastructure/Assets/AssetsIndexingService.cs).
- Asset list subscription/merge logic lives in `AssetsLayoutViewModel`, which is shared by list/tiles layouts.
  - This will become a hotspot as query/filter/sort/diagnostics and pipeline status are added.
  - See [projects/Oxygen.Editor.ContentBrowser/src/Panes/Assets/Layouts/AssetsLayoutViewModel.cs](../src/Panes/Assets/Layouts/AssetsLayoutViewModel.cs).
- Shared state is currently `ContentBrowserState` with `SelectedFolders: ISet<string>`.
  - This is convenient now, but it will conflict with `Oxygen.Assets.Model.VirtualPath` / `AssetKey` once those exist.
  - See [projects/Oxygen.Editor.ContentBrowser/src/State/ContentBrowserState.cs](../src/State/ContentBrowserState.cs).

## Refactoring direction (architecture)

### 1) Separate Shell vs Features

**Intent:** The shell owns routing + chrome/navigation state; features own domain-specific behavior.

- Shell owns: route graph, pane composition, breadcrumbs/history.
- Left pane (Project Explorer) owns: folder tree, selection UI.
- Right pane (Assets) owns: asset querying, results presentation, asset invocation.

### 2) Introduce an Application layer (actions)

**Intent:** Put editor operations in stable action services so ViewModels stay thin and pipeline integration lands in one place.

Examples:

- `OpenAssetAction` (AssetKey → open appropriate editor/document)
- `NavigateToFolderAction` (VirtualPath → update state and route)
- `CreateSceneAction`
- (later) `ImportAssetAction`, `ReimportAssetAction`, `CookSelectionAction`, `ValidateSelectionAction`

### 2.1) Dependency rules (keep layering enforceable)

These rules are the main reason for the proposed folder structure.

- `Shell/*` may depend on `State/*`, and on `Application/*`. Avoid `Infrastructure/*` references from the Shell.
- `Panes/*` may depend on `State/*` and `Application/*`.
- `Application/*` should not depend on WinUI/XAML types; keep it testable.
- `Infrastructure/*` may depend on storage/filesystem and (later) `Oxygen.Assets.*`, but should not depend on views.
- Cross-feature types should be rare; prefer feature-local types unless they are truly shared.

Note: after the Phase A folder moves, some code still crosses these boundaries (e.g., shell and pane viewmodels directly referencing indexing infrastructure). The intent is to enforce these rules after introducing the Application layer and the catalog abstraction.

### 2.2) Namespaces (current state + target)

The project has been reorganized on disk (Phase A), but namespaces are not fully aligned yet.

Current (post Phase A):

- Shell types follow `Oxygen.Editor.ContentBrowser.Shell.*` under `src/Shell/`.
- Other types are still a mix (for example `Oxygen.Editor.ContentBrowser`, `Oxygen.Editor.ContentBrowser.ProjectExplorer`, and some `Oxygen.Editor.ContentBrowser.Panes.Assets.*`), even if the files live under `src/Panes/` or `src/State/`.
- Shared DTOs/messages live in `src/Models/` and `src/Messages/` with `Oxygen.Editor.ContentBrowser.Models.*` and `Oxygen.Editor.ContentBrowser.Messages.*`.

Target (once the layering is enforced):

- `Oxygen.Editor.ContentBrowser.Shell.*`
- `Oxygen.Editor.ContentBrowser.State.*`
- `Oxygen.Editor.ContentBrowser.Panes.ProjectExplorer.*`
- `Oxygen.Editor.ContentBrowser.Panes.Assets.*`
- `Oxygen.Editor.ContentBrowser.Models.*`
- `Oxygen.Editor.ContentBrowser.Messages.*`
- `Oxygen.Editor.ContentBrowser.Application.*`
- `Oxygen.Editor.ContentBrowser.Infrastructure.*`

### 3) Treat asset indexing/query as Infrastructure and expose a UI-facing Catalog

**Intent:** The UI should depend on a stable concept like “catalog/query service”, not on “indexing mechanics”.

- Introduce `IAssetCatalog` (or similar) with:
  - Snapshot query: `QueryAsync(query, ct)`
  - Change stream: `IObservable<AssetChange>` / event stream
  - Optional progress/diagnostics hooks
- Provide a temporary adapter implementation backed by the current indexer, but keep the interface aligned with future `Oxygen.Assets` APIs.

### 4) Adopt pipeline identity early (VirtualPath / AssetKey)

**Intent:** Once `Oxygen.Assets.Model.VirtualPath` and `AssetKey` exist, the content browser should use them internally and keep strings only at the routing boundary.

- `ContentBrowserState` evolves to:
  - `SelectedFolders: ISet<VirtualPath>`
  - `SelectedAssets: ISet<AssetKey>`
  - `ActiveFolder: VirtualPath?`
  - `ViewMode` (tiles/list)
- A dedicated route/state adapter translates URL query params ↔ typed state.

### 5) Separate pipeline model from UI list items

**Intent:** The asset pipeline model will grow (source/imported/intermediate/cooked/containerized). The UI needs display fields (thumbnail, badges, states) that should not pollute pipeline DTOs.

- Keep pipeline identity and metadata in `Oxygen.Assets.*`.
- Add UI-specific `AssetListItem` / `AssetItemViewModel` for presentation.

### 6) Centralize asset result subscription; keep layouts presentation-only

**Intent:** As soon as filtering/sorting/diagnostics are added, duplicating “snapshot + stream merge” logic per layout becomes fragile.

- Move the “snapshot + stream merge” logic into a single controller in the Assets feature.
- Keep `TilesLayoutViewModel` / `ListLayoutViewModel` focused on interaction (invoke, selection, view-only behaviors).

### 7) Settings + UI state persistence (Oxygen.Editor.Data)

The Content Browser needs two different kinds of persistence, and they should be treated differently in the architecture.

#### A) User settings / preferences (Settings UI)

- Persisted via `Oxygen.Editor.Data.Services.IEditorSettingsManager` using typed `SettingKey<T>`.
- Used for user-editable preferences like default view mode, thumbnail size, columns, sort defaults, etc.
- Scoping:
  - Prefer **Application scope** for “this is how I like my editor” preferences.
  - Allow **Project scope overrides** for settings that genuinely vary per project.

#### B) Session/UI state (restore what the user was doing)

- Persist a compact “last session” snapshot per project.
- `Oxygen.Editor.Data.Services.IProjectUsageService` already supports `UpdateContentBrowserStateAsync(...)` and the `ProjectsUsage.ContentBrowserState` field exists for this purpose.
- Treat this as a *session restoration payload*: last active folder, last selected asset (optional), view mode, and key layout splits.

#### Important constraints and patterns

- Persist state through a dedicated store/service (not directly from ViewModels) and debounce writes.
  - UI selection can change very frequently; saving on every change will create unnecessary churn.
- Version the serialized payload (e.g. `{ "v": 1, ... }`) and keep it small (DB fields are currently size-limited).
- Do **not** route session persistence through undo/redo (navigation and panel geometry should not pollute the undo stack).

### 8) Undo/Redo integration (TimeMachine)

The content browser will eventually perform asset operations that must be undoable (rename/move/delete/import/reimport/cook/validate in editor workflows).
TimeMachine is a good fit, but it impacts *where* mutations live and *how* they are expressed.

#### What should be undoable

- Asset graph / project mutations: create folder, rename, move, delete, import/reimport, scene creation, metadata edits.

#### What should NOT be undoable

- Navigation state: selection, breadcrumbs/history, view mode toggles, panel sizes.
  - These belong to session/UI persistence, not to the undo stack.

#### Where undo/redo belongs in the architecture

- Undo/redo integration should live in the **Application layer**, not in Views.
- Actions that mutate state should:
  1) perform the mutation now,
  2) immediately register the inverse with a `HistoryKeeper`.
- Multi-item operations should be wrapped in a TimeMachine transaction so they appear as a single undo step.

#### Choosing an undo root

- Use one undo history per “editing root” (typically per project or per document).
- TimeMachine supports `UndoRedo.Default[rootObject]` or `UndoRedo.Default[guid]`.
  - Prefer a stable project/document identity when available; otherwise use the current `IProject` instance as the root.

## Integration priorities for the Oxygen asset pipeline

1) **Catalog & identity:** move UI to `AssetKey`/`VirtualPath` as soon as `Oxygen.Assets.Model` has them.
2) **Operations:** add action entry points for import/reimport/cook/validate; UI wires to those commands.
3) **Diagnostics-first:** surface validation/cook errors as first-class badges + a details panel/log stream.
4) **Scalability:** virtualized asset lists, throttled change streams, incremental queries.

## Migration strategy (phased)

- Phase A: Folder moves + namespaces, no behavioral changes.
- Phase B: Introduce Application layer services, move commands out of viewmodels.
- Phase C: Replace indexer interface with catalog interface, keep adapter to existing implementation.
- Phase D: Switch state from strings to `VirtualPath`/`AssetKey` once `Oxygen.Assets` types exist.
- Phase E: Add pipeline operations (cook/validate/import) and diagnostics UX.

## Trackable tasks

1. [X] Define a target folder layout and move files into `Shell/`, `Panes/`, `Infrastructure/Assets/`, `State/`, `Models/`, `Messages/` without behavior changes.
2. [X] Extract URL/query-param parsing into a single route/state adapter (keep it in an existing namespace, e.g. `Oxygen.Editor.ContentBrowser.Shell`) so string paths stay at one boundary.
3. [ ] Introduce `Application/Assets/IAssetCatalog` (snapshot query + change stream) and update assets VMs to depend on it.
4. [ ] Create an adapter `Infrastructure/Assets/IndexerBackedAssetCatalog` wrapping the current `IAssetIndexingService`.
5. [ ] Replace `ConcurrentBag<GameAsset>` in the indexing implementation with a key-indexed store that supports remove/move/rename deterministically.
6. [ ] Introduce `AssetQuery` (folder scope/search/filter/sort) and drive results from query state.
7. [ ] Centralize snapshot+stream merging in an `AssetResultsController` so layouts remain presentation-only.
8. [ ] Add `OpenAssetAction` and route all item invocation through it (no asset-type switch in viewmodels).
9. [ ] Add `NavigateToFolderAction` and route all folder navigation through it (single place updates state/router).
10. [ ] Add `CreateSceneAction` and route scene creation through it.
11. [ ] Extend `ContentBrowserState` to include selected assets (in addition to selected folders).
12. [ ] When `Oxygen.Assets.Model.VirtualPath` and `AssetKey` exist, replace string-based paths/identity in state and UI models.
13. [ ] Add pipeline operation stubs: import, reimport, cook selection, validate selection (application services first).
14. [ ] Add diagnostics surfacing: per-asset badges (errors/warnings) + a details view/log stream.
15. [ ] Add thumbnail pipeline abstraction (async + caching + cancellation) and update layouts to use it.
16. [ ] Add scalability safeguards: throttle/debounce change streams and enable UI virtualization for large asset sets.
17. [ ] Add unit tests for URL↔state mapping, folder-scope matching, and snapshot+stream deduplication.
18. [ ] Define Content Browser settings keys and descriptors using `IEditorSettingsManager` (application vs project scope).
19. [ ] Implement `IContentBrowserPreferencesStore` backed by `Oxygen.Editor.Data` settings and wire it to default view/query/layout preferences.
20. [ ] Implement `IContentBrowserSessionStateStore` backed by `IProjectUsageService.UpdateContentBrowserStateAsync` with versioned, compact JSON payload.
21. [ ] Load session state on project open and apply it to `ContentBrowserState` + route (debounced persistence of subsequent changes).
22. [ ] Introduce `IUndoHistory`/provider using TimeMachine `HistoryKeeper` per project/document root.
23. [ ] Implement first undoable asset operation action (e.g., rename or move) using TimeMachine inverse-registration pattern.
24. [ ] Wrap multi-asset operations in TimeMachine transactions so they undo as a single step.

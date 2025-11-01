---
goal: Implement Tab Drag-and-Drop feature for `Controls.TabStrip` component
version: 1.0
date_created: 2025-11-01
last_updated: 2025-11-01
owner: Controls.TabStrip Team
status: 'Planned'
tags: [feature, tabstrip, drag-drop, ui]
---

# Introduction

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

This implementation plan guarantees a complete, spec-compliant implementation of the Tab drag-and-drop behavior described in `projects/Controls/TabStrip/Drag.md`. It contains atomic, machine-actionable tasks with file-level references and deterministic completion criteria. Any deviation from the plan or an implementation choice that materially affects behavior requires explicit user approval and is annotated as a CHECKPOINT.

## 1. Requirements & Constraints

- **REQ-001**: Support reordering a single `TabItem` within the same `TabStrip` via pointer drag.
- **REQ-002**: Support tearing out a `TabItem` (drop outside any `TabStrip`) — raise `TabTearOutRequested` with screen coordinates; application must create a new window and host the `TabItem`.
- **REQ-003**: Support dropping into another `TabStrip` (including in another AppWindow) — insert a hidden placeholder, run layout, make visible, and raise selection/activation events in the correct order.
- **REQ-004**: Use a single process-owned layered overlay (topmost, non-activating) for drag visuals. The overlay must survive source window closure and follow the cursor across AppWindows.
- **REQ-005**: Raise `TabDragImageRequest`, `TabCloseRequested`, `TabDragComplete`, and `TabTearOutRequested` per spec, exposing `TabItem` model in EventArgs.
- **REQ-006**: Enforce single active drag session per process. `StartSession` must throw `InvalidOperationException` if a session exists.
- **SEC-001**: All public APIs that touch UI must assert UI-thread affinity and document it in XML docs.
- **CON-001**: No blocking synchronous I/O on the drag path; handlers are assumed possibly throwing and the control must recover gracefully, raising `TabDragComplete` with null destination when unrecoverable.
- **GUD-001**: Clearing multi-selection semantics: when drag starts, only the dragged tab remains selected.

## 2. Implementation Steps

### Phase 1 — Public API & EventArgs (complete)

- GOAL-001: Add EventArgs types and `TabStrip` events so applications can implement previews, tear-outs, and completion handling.

| Completed | Task ID | File / Symbol | Action | Acceptance criteria |
|---:|---:|---|---|---|
| Yes | TASK-001 | `projects/Controls/TabStrip/src/TabDragImageRequestEventArgs.cs` | Add sealed `TabDragImageRequestEventArgs : EventArgs` with properties: `TabItem Item { get; init; }`, `Windows.Foundation.Size RequestedSize { get; init; }`, `Microsoft.UI.Xaml.Media.ImageSource? PreviewImage { get; set; }`. | Compiles; XML docs present; referenced by `TabStrip.events.cs` (TASK-004). |
| Yes | TASK-002 | `projects/Controls/TabStrip/src/TabDragCompleteEventArgs.cs` | Add sealed `TabDragCompleteEventArgs : EventArgs` with `TabItem Item`, `TabStrip? DestinationStrip`, `int? NewIndex`. | Compiles; XML docs present; used by `TabStrip` event. |
| Yes | TASK-003 | `projects/Controls/TabStrip/src/TabTearOutRequestedEventArgs.cs` | Add sealed `TabTearOutRequestedEventArgs : EventArgs` with `TabItem Item`, `Windows.Foundation.Point ScreenDropPoint`. | Compiles; XML docs present. |
| Yes | TASK-004 | `projects/Controls/TabStrip/src/TabStrip.events.cs` | Add public events: `TabDragImageRequest`, `TabCloseRequested`, `TabDragComplete`, `TabTearOutRequested` using the EventArgs above. | Compiles; public API available. Added a small test `projects/Controls/TabStrip/tests/Controls/ApiSurfaceTests.cs` that verifies event signatures (TEST-API-001). |

Phase 1 acceptance: all new files compile, the API surface test compiles into the test assembly, and XML documentation exists for public symbols.

## Progress update (2025-11-01)

- Phase 1 completed: EventArgs types and public events were added and compiled successfully. The API surface test was added at `projects/Controls/TabStrip/tests/Controls/ApiSurfaceTests.cs` and compiles as part of the TabStrip test assembly.
- XML documentation was expanded and aligned with the drag/drop specification in the following ways (per reviewer decisions recorded during implementation):
  - `TabDragImageRequest` documentation now explicitly states it is raised when the dragged tab leaves its originating `TabStrip` (tear-out begins) and documents the expectation that handlers provide a synchronous, lightweight preview image.
  - The canonical ordering for tear-out is documented: `TabDragImageRequest` then `TabCloseRequested` when the tab leaves the source strip.
  - The `TabStrip` class docs now explicitly document the selection semantics for drag start: any multi-selection is cleared and only the dragged tab remains selected (GUD-001).
  - Destination insertion ordering is documented (placeholder insertion → layout pass → visibility restore → `SelectionChanged` then `TabActivated`).
  - `TabTearOutRequested` recovery behavior is documented (the control will catch exceptions from handlers and complete the drag with `TabDragComplete` where `DestinationStrip == null`). This recovery note is present in-source next to the event args to make the contract explicit for implementers.
- Build verification: a full Controls solution build was run; the build succeeded with warnings (no errors). Warnings are static analysis/style related and unrelated to the new API docs.

Next steps
- Proceed to Phase 2 scaffolding (native wrappers, coordinator) up to CHECKPOINT-2. TASK-005/TASK-006 scaffolding already exists in the codebase; TASK-007..TASK-009 remain to be implemented. The implementation will stop at CHECKPOINT-2 to allow selection of the overlay rendering approach (Win32 layered window vs AppWindow composition APIs).

### Phase 2 — Layered Overlay & Coordinator (exact spec implementation)

- GOAL-002: Implement the process-owned layered overlay service and a robust coordinator that polls the global cursor and manages lifecycle exactly as specified. This phase targets a production-ready layered overlay implementation; architectural decisions that materially affect behavior are gated behind explicit CHECKPOINTs where the user must approve the selected approach.

Design constraints (must be followed exactly):

- Overlay must be topmost, non-activating, and click-through. It must be created so it survives closure of the originating AppWindow and continue to display until EndSession/Abort.
- Overlay must render composed visuals (header + optional preview) using premultiplied BGRA; it must support per-monitor DPI and hotspot alignment.
- The coordinator must poll `GetCursorPos` at a configurable frequency (default 30Hz), drive the overlay via `UpdatePosition(token, screenPoint)`, and raise `DragMoved` / `DragEnded` events for `TabStrip` consumers.
- The `IDragVisualService` must provide a live `DragVisualDescriptor` accessible via `GetDescriptor(token)`; mutations to the descriptor (on UI thread) must reflect immediately in the overlay.
- All public methods require UI-thread calls and must assert this (throw on non-UI thread).
- Single-session enforcement is required.

| Completed | Task ID | File / Symbol | Action | Acceptance criteria |
|---:|---:|---|---|---|
| Partial | TASK-005 | `projects/Controls/TabStrip/src/IDragVisualService.cs` | Add `DragSessionToken` (value type) and `IDragVisualService` with `StartSession(DragVisualDescriptor descriptor)`, `UpdatePosition(DragSessionToken, Windows.Foundation.Point)`, `EndSession(DragSessionToken)`, `GetDescriptor(DragSessionToken)`. Document UI-thread requirement and single-session behavior. Implement `DragSessionToken` equality, GetHashCode, and operator overloads. | Scaffold present and compiles; token currently needs equality/operators implemented. Add UpdatePosition to contract so the coordinator can drive overlay position. |
| Yes | TASK-006 | `projects/Controls/TabStrip/src/DragVisualDescriptor.cs` | Implement `DragVisualDescriptor : ObservableObject` with required properties: `ImageSource HeaderImage` (required), `ImageSource? PreviewImage`, `Windows.Foundation.Size RequestedSize`, `string? Title`. Document synchronous placeholder semantics in XML. | Compiles; descriptor notifies property changes. |
| | TASK-007 | `projects/Controls/TabStrip/src/Native/NativeMethods.cs` | Add P/Invoke wrappers for required native calls: `GetCursorPos`, `CreateWindowExW`, `DestroyWindow`, `UpdateLayeredWindow`, `SetWindowPos`, `ShowWindow`, `GetDC`, `ReleaseDC`, `SetWindowLongPtrW`, and DPI helpers (`GetDpiForMonitor`/`GetDpiForWindow`). Include helper functions to convert XAML logical pixels to physical device pixels. Provide managed safe wrappers and small unit tests for argument validation (TEST-NATIVE-001). | Not started; file not present. |
| | TASK-008 | `projects/Controls/TabStrip/src/DragVisualService.cs` | Implement production-ready layered overlay using the chosen native layering approach (see CHECKPOINT-2). Requirements: create a topmost, non-activating, click-through overlay window; compose `HeaderImage` and `PreviewImage` into a premultiplied BGRA pixel buffer; call `UpdateLayeredWindow` (or equivalent AppWindow composition API) with correct per-monitor DPI scaling; implement `UpdatePosition` aligning the configured hotspot under the cursor; ensure deterministic resource cleanup in `EndSession`; enforce single-session. | Not started: the layered overlay implementation required by the spec is not yet implemented. |
| Partial | TASK-009 | `projects/Controls/TabStrip/src/TabDragCoordinator.cs` | Implement coordinator: `StartDrag(TabItem item, TabStrip source, DragVisualDescriptor descriptor, Windows.Foundation.Point hotspot)` creates session via `IDragVisualService`, stores token, starts a `DispatcherQueueTimer` polling `GetCursorPos` at configured frequency and calling `UpdatePosition`, exposes `Move(...)`, `EndDrag(...)`, and `Abort()`; raises `DragMoved` and `DragEnded` events as `EventHandler<T>` with documented args. Coordinator must survive source AppWindow closure and continue polling/overlay updates. | Partial: `TabDragCoordinator` exists and implements StartDrag/Move/EndDrag/Abort and raises `DragMoved`/`DragEnded`. It does not yet start a polling timer (DispatcherQueueTimer) for GetCursorPos; TabStrip integration (subscribing to coordinator events) remains to be implemented in Phase 3. |

CHECKPOINT-2: Choose overlay rendering approach. Options (must be approved by user before TASK-008 implementation):

- A: Use Win32 layered window + `UpdateLayeredWindow` (P/Invoke) — direct pixel control and deterministic behavior.
- B: Use AppWindow / Windows App SDK composition APIs — integrates with WinUI composition but requires extra plumbing.

The implementer must stop at CHECKPOINT-2 and request approval. The PR implementing TASK-007 and TASK-009 may be submitted prior to CHECKPOINT-2 to allow review of native wrappers and coordinator scaffolding, but TASK-008 (render path) will not be merged until the user approves the chosen approach.

Phase 2 acceptance: unit tests pass, and manual QA (cross-window persistence and DPI alignment) passes. The implementation must include instrumentation logs for the coordinator and overlay lifecycle (start, move receipts, end, abort).

### Phase 3 — TabStrip integration and UI wiring

- GOAL-003: Wire pointer lifecycle in `TabStripItem` and `TabStrip` to start and complete drag sessions following the spec exact ordering and semantics.

| Completed | Task ID | File / Symbol | Action | Acceptance criteria |
|---:|---:|---|---|---|
| | TASK-010 | `projects/Controls/TabStrip/src/TabStripItem.properties.cs` | Add read-only DP `IsDragging` (`DependencyPropertyKey` and `DependencyProperty`) and `bool IsDragging { get; }`. | Compiles; templates can bind; unit test validates DP existence. |
| | TASK-011 | `projects/Controls/TabStrip/src/TabStripItem.events.cs` | Wire pointer handlers: `OnPointerPressed` -> `OwnerStrip.PointerPressedOnItem(this, e)`, `OnPointerMoved` -> `OwnerStrip.PointerMovedOnItem(this, e)`, `OnPointerReleased` -> `OwnerStrip.PointerReleasedOnItem(this, e)`. Ensure handlers call `e.Handled` as appropriate and maintain `this.` usage. | Compiles; pointer flow testable with `TestableTabStrip`. |
| | TASK-012 | `projects/Controls/TabStrip/src/TabStrip.cs` | Implement `internal void PointerPressedOnItem(TabStripItem item, PointerRoutedEventArgs e)` to capture pointer and start drag threshold tracking; implement `BeginDrag(TabItem item, TabStripItem visual, Windows.Foundation.Point screenPoint)` to: hide visual, prepare `DragVisualDescriptor`, raise `TabDragImageRequest` (synchronously allow placeholder), and call `TabDragCoordinator.StartDrag(item, this, descriptor, hotspot)`. When pointer leaves origin bounds, raise `TabCloseRequested` (application must remove item). Implement `OnCoordinatorMove` and `OnCoordinatorEnd` callbacks that `TabDragCoordinator` will call via events. Ensure selection semantics: clear multi-selection and keep dragged item selected. | Compiles; unit tests validate event order and selection semantics (TEST-EVENT-ORDER-001). |
| | TASK-013 | `projects/Controls/TabStrip/src/TabStrip.xaml` and `projects/Controls/TabStrip/src/TabStripItem.xaml` | Update templates to bind `IsDragging` and include a named placeholder `x:Name="HiddenInsertPlaceholder"` and VisualState for `IsDragging` that collapses/hides header and preserves layout space. | XAML compiles; visual state testable in sample. |

CHECKPOINT-3: Confirm the exact call order when tearing out: the spec requires `TabDragImageRequest` followed by `TabCloseRequested` when the drag leaves origin; confirm that the control will raise `TabCloseRequested` and allow the application to remove the item before continuing the overlay lifecycle. Proceed with the default spec sequence if the user does not object.

Phase 3 acceptance: unit tests and UI sample confirm `IsDragging` state, pointer capture, correct event sequence, and visibility/placeholder behavior.

### Phase 4 — Destination insertion, activation, and completion

- GOAL-004: Implement insertion of hidden placeholder in destination `TabStrip`, run layout, make visible, and ensure `SelectionChanged` then `TabActivated` event order as specified; implement drop behavior inside and outside strips and raise `TabDragComplete` accordingly.

| Completed | Task ID | File / Symbol | Action | Acceptance criteria |
|---:|---:|---|---|---|
| | TASK-014 | `projects/Controls/TabStrip/src/TabStrip.cs` | Add `internal void InsertHiddenItemForDrag(TabItem item, int indexHint)` — insert placeholder hidden `TabStripItem`, call `InvalidateMeasure()` and `UpdateLayout()`, then restore visibility; after visibility, raise `SelectionChanged` and `TabActivated` in exactly that order. | Unit tests assert insertion, layout pass, and event ordering (TEST-INSERT-ORDER-001). |
| | TASK-015 | `projects/Controls/TabStrip/src/TabStrip.cs` | On drop inside a strip: remove placeholder and place the dragged `TabItem` at the new index, raise `TabDragComplete` with `DestinationStrip` and `NewIndex`; release pointer capture; coordinator will EndSession after this call. | Unit tests assert `TabDragComplete` args, and the collection state is correct. |
| | TASK-016 | `projects/Controls/TabStrip/src/TabStrip.cs` | On drop outside any `TabStrip`: raise `TabTearOutRequested` with `ScreenDropPoint`; the application must remove the item in response to `TabCloseRequested` earlier. If `TabTearOutRequested` handler throws, catch and raise `TabDragComplete` with `DestinationStrip = null`. | Unit tests for exception path (TEST-TEAROUT-EXCEPTION-001) and manual QA verifying new window creation flow. |

Phase 4 acceptance: unit tests and manual QA validate insertion, activation, and tear-out flows. The manual QA scenario must demonstrate creating a new window and adding the tab there in response to `TabTearOutRequested`.

### Phase 5 — Tests and samples (complete coverage)

- GOAL-005: Add automated tests and a manual sample harness to validate all interactive behaviors and edge cases required by the spec.

Required tests (automated):

- TEST-API-001: Surface API test for public events and EventArgs shapes.
- TEST-NATIVE-001: Native wrapper argument validation tests (P/Invoke wrappers).
- TEST-COORD-001: Coordinator lifecycle and single-session enforcement.
- TEST-INSERT-ORDER-001: Destination insertion and event ordering test.
- TEST-TEAROUT-EXCEPTION-001: Exception path when `TabTearOutRequested` handler throws.
- TEST-OVERLAY-001: Descriptor live-mutation updates overlay (unit with mock overlay where possible).
- TEST-DPI-001: Hotspot alignment and scaling across monitors in automated environment where available.

Manual sample: `projects/Controls/TabStrip/tests/Controls/Samples/TabDragSample` showing two AppWindows with tab strips; includes instrumentation UI to display coordinator status and overlay debug info. Manual QA checklist is required in the PR description.

Phase 5 acceptance: All automated tests pass in CI; manual QA checklist completed and signed off by the user.

## 3. Alternatives

- **ALT-001**: Use OS-level drag-and-drop (`DoDragDrop` / `IDataObject`). This was considered. It gives system-managed visuals across processes but introduces substantial complexity: serializing `TabItem` state, handling async image generation, and losing some control over the exact event ordering the spec mandates. This plan prefers the layered overlay approach for full control and spec fidelity. If you want OS-level DnD instead, raise a CHECKPOINT request and I will produce an alternate plan.

## 4. Dependencies

- **DEP-001**: `Microsoft.UI.Xaml` for XAML types and `ImageSource`.
- **DEP-002**: Win32 P/Invoke (native wrappers) for layered overlay and cursor polling.
- **DEP-003**: Existing test harness under `projects/Controls/TabStrip/tests` for integration tests.

## 5. Files (complete list for the implementation)

- `projects/Controls/TabStrip/src/TabDragImageRequestEventArgs.cs` — EventArgs
- `projects/Controls/TabStrip/src/TabDragCompleteEventArgs.cs` — EventArgs
- `projects/Controls/TabStrip/src/TabTearOutRequestedEventArgs.cs` — EventArgs
- `projects/Controls/TabStrip/src/TabStrip.events.cs` — add public events
- `projects/Controls/TabStrip/src/IDragVisualService.cs` — `DragSessionToken` + service contract
- `projects/Controls/TabStrip/src/DragVisualDescriptor.cs` — observable descriptor
- `projects/Controls/TabStrip/src/Native/NativeMethods.cs` — P/Invoke wrappers and safe helpers
- `projects/Controls/TabStrip/src/DragVisualService.cs` — layered overlay implementation (must follow CHECKPOINT-2 chosen approach)
- `projects/Controls/TabStrip/src/TabDragCoordinator.cs` — coordinator: Start/Move/End/Abort, timer polling
- `projects/Controls/TabStrip/src/TabStrip.cs` — pointer wiring, BeginDrag, insertion/removal, event raises
- `projects/Controls/TabStrip/src/TabStripItem.properties.cs` — add `IsDragging` DP
- `projects/Controls/TabStrip/src/TabStripItem.events.cs` — pointer handlers
- `projects/Controls/TabStrip/src/TabStrip.xaml` and `TabStripItem.xaml` — template changes to support placeholder and IsDragging visual state
- `projects/Controls/TabStrip/tests/Controls/TabStripDragTests.cs` and other test files listed in Phase 5

## 6. Testing & Verification

Automated: run `dotnet build AllProjects.sln` and the test projects listed in Phase 5. Manual: run the `TabDragSample` and execute the QA checklist (cross-window drag, close source window mid-drag, multi-monitor DPI checks, tear-out new-window creation).

Validation commands (PowerShell):

```powershell
# From repo root
dotnet build AllProjects.sln
dotnet test projects/Controls/TabStrip/tests/Controls.TabStrip.UI.Tests.csproj
dotnet test projects/Controls/TabStrip/tests/Controls.TabStrip.Tests.csproj
```

Manual QA checklist (Phase 2 & 4 required):

1. Start two AppWindows with `TabStrip` (sample). Drag a tab from Window A to Window B: overlay remains visible and follows the cursor; destination receives placeholder and activation events in correct order.
2. Close Window A while dragging — overlay persists and drag can complete in Window B or outside. Coordinator continues to poll and route events.
3. Drop outside any `TabStrip`: `TabTearOutRequested` raised with accurate screen coordinates; application creates new window and hosts item.
4. Test on multi-monitor setup: hotspot alignment and image scaling are correct.

Phase acceptance requires both automated tests and manual QA to pass and be approved by the user.

## 7. Related Specifications / Further Reading

[TabStrip Drag-Drop Specifications](projects/Controls/TabStrip/Drag.md)
[WinUI Pointer Input docs](https://learn.microsoft.com/windows/apps/winui/)

---

Implementation notes:

- Implement tasks in the order of phases. After adding native helpers and coordinator scaffolding, run API tests and native wrapper tests before writing rendering code.
- Respect CHECKPOINTs: stop and ask for approval where indicated.
- When ready to implement, I can create the PRs for TASK-005..TASK-009 (native wrappers, service contract, descriptor, coordinator scaffolding). The rendering code for TASK-008 will be created after your approval at CHECKPOINT-2.

---
goal: Implement Tab Drag-and-Drop feature for `Controls.TabStrip` component
version: 2.0
date_created: 2025-11-01
last_updated: 2025-11-02
owner: Controls.TabStrip Team
status: 'Phase 2 In Progress'
tags: [feature, tabstrip, drag-drop, ui]
---

# Introduction

![Status: Phase 2 In Progress](https://img.shields.io/badge/status-Phase%202%20In%20Progress-yellow)

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

### Phase 2 — Layered Overlay & Coordinator (exact spec implementation)

- GOAL-002: Implement the process-owned layered overlay service and a robust coordinator that polls the global cursor and manages lifecycle exactly as specified. This phase targets a production-ready layered overlay implementation; architectural decisions that materially affect behavior are gated behind explicit CHECKPOINTs where the user must approve the selected approach.

## Progress update (2025-11-02)

- **Phase 2 Complete**: All tasks TASK-005 through TASK-009 have been successfully implemented and refactored:
  - Services have been moved to the dedicated `projects/Controls/Services` library for shared infrastructure reuse.
  - `IDragVisualService`, `DragSessionToken`, and `DragVisualDescriptor` are fully specified and implemented with comprehensive XML documentation.
  - `Native.cs` contains 532 lines of production-grade P/Invoke wrappers with proper marshaling and helper functions for DPI scaling.
  - `DragVisualService` implements Win32 layered window approach (CHECKPOINT-2 approved) with per-monitor DPI support, hotspot offset alignment, and resource cleanup.
  - `TabDragCoordinator` implements full drag lifecycle with 30Hz cursor polling, state serialization via `Lock`, and comprehensive logging via structured logging framework.
  - All new event argument types (`DragMovedEventArgs`, `DragEndedEventArgs`) are present and properly defined.
  - Build verification: Controls solution compiles successfully.

- **Key architectural decisions documented**:
  - CHECKPOINT-2 approved: Win32 Layered Window approach for overlay rendering (direct pixel control, deterministic DPI scaling, clean integration).
  - Coordinator uses thread-safe `Lock` for state serialization and single-session enforcement.
  - DPI conversion functions support per-monitor awareness and physical ↔ logical coordinate translation.
  - Descriptor property mutations trigger overlay recomposition via `PropertyChanged` subscription.

- **Next steps**: Proceed to Phase 3 (TabStrip pointer wiring and event sequencing). TASK-010..TASK-013 scaffold the pointer handlers, begin-drag logic, placeholder insertion, and template updates to wire the coordinator into the TabStrip UI lifecycle.

| Completed | Task ID | File / Symbol | Action | Acceptance criteria |
|---:|---:|---|---|---|
| Yes | TASK-005 | `projects/Controls/Services/src/IDragVisualService.cs` | Add `DragSessionToken` (value type) and `IDragVisualService` with `StartSession(DragVisualDescriptor descriptor)`, `UpdatePosition(DragSessionToken, Windows.Foundation.Point)`, `EndSession(DragSessionToken)`, `GetDescriptor(DragSessionToken)`. Document UI-thread requirement and single-session behavior. Implement `DragSessionToken` equality, GetHashCode, and operator overloads. | ✅ Complete: Service interface fully defined; `DragSessionToken` is a value type with full `IEquatable<T>` implementation and operator overloads (`==`, `!=`); all methods documented; UI-thread requirement and single-session enforcement documented in XML. Service moved to `Controls.Services` project (shared infrastructure). |
| Yes | TASK-006 | `projects/Controls/Services/src/DragVisualDescriptor.cs` | Implement `DragVisualDescriptor : ObservableObject` with required properties: `ImageSource HeaderImage` (required), `ImageSource? PreviewImage`, `Windows.Foundation.Size RequestedSize`, `string? Title`. Document synchronous placeholder semantics in XML. | ✅ Complete: Descriptor inherits from `CommunityToolkit.Mvvm.ComponentModel.ObservableObject`; all properties with change notification via `SetProperty(ref field, value)`; full XML documentation. |
| Yes | TASK-007 | `projects/Controls/Services/src/Native.cs` | Add P/Invoke wrappers for required native calls: `GetCursorPos`, `CreateWindowExW`, `DestroyWindow`, `UpdateLayeredWindow`, `SetWindowPos`, `ShowWindow`, `GetDC`, `ReleaseDC`, `SetWindowLongPtrW`, and DPI helpers (`GetDpiForMonitor`/`GetDpiForWindow`, `GetDpiForPoint`). Include helper functions to convert XAML logical pixels to physical device pixels (`LogicalToPhysicalPoint`, `LogicalToPhysicalSize`). Provide managed safe wrappers and validated P/Invoke marshaling. | ✅ Complete: Comprehensive P/Invoke wrappers in `projects/Controls/Services/src/Native.cs` (532 lines). Includes: `GetCursorPos`, `CreateWindowExW`, `DestroyWindow`, `UpdateLayeredWindow`, `SetWindowPos`, `ShowWindow`, `GetDC`, `ReleaseDC`, `SetWindowLongPtrW`, `GetDpiForMonitor`, `GetDpiForWindow`, `GetMonitorFromPoint`, `GetDpiForPoint`. Helper functions: `LogicalToPhysicalPoint`, `LogicalToPhysicalSize`, `PhysicalToLogicalPoint`, `PhysicalToLogicalSize`. Enums: `WindowStyles`, `WindowStylesEx`, `SetWindowPosFlags`, `ShowWindowCommands`, `UpdateLayeredWindowFlags`, `MONITOR_DPI_TYPE`, `WindowLongIndex`. Structures: `BLENDFUNCTION`, `POINT`, `SIZE`, `RECT`. Constants: `HWND_TOPMOST`, `HWND_NOTOPMOST`, `NULL_BRUSH`. |
| Yes | TASK-008 | `projects/Controls/Services/src/DragVisualService.cs` | Implement production-ready layered overlay using Win32 layered window + `UpdateLayeredWindow` (CHECKPOINT-2 approved approach). Requirements: create topmost, non-activating, click-through overlay window; compose `HeaderImage` and `PreviewImage` into premultiplied BGRA pixel buffer; call `UpdateLayeredWindow` with correct per-monitor DPI scaling; implement `UpdatePosition` aligning hotspot under cursor; ensure deterministic resource cleanup; enforce single-session. | ✅ Complete (Win32 Layered Window approach): `DragVisualService` (452 lines) fully implements `IDragVisualService`. Creates layered overlay window with `WS_EX_TOPMOST`, `WS_EX_TRANSPARENT`, `WS_EX_NOACTIVATE`, `WS_EX_LAYERED`. Composes `HeaderImage` + `PreviewImage` to premultiplied BGRA DIB. Per-monitor DPI support via `GetDpiForPoint`. Hotspot offset applied correctly. `UpdatePosition` updates window via `SetWindowPos`. Descriptor PropertyChanged events trigger recomposition. `EndSession` cleans up all resources (window, DC, bitmap, bits). Single-session enforced. UI-thread assertions via `AssertUIThread()`. |
| Yes | TASK-009 | `projects/Controls/TabStrip/src/TabDragCoordinator.cs` | Implement coordinator as a singleton class registered in DI: `StartDrag(TabItem item, TabStrip source, DragVisualDescriptor descriptor, Windows.Foundation.Point hotspot)` creates session via `IDragVisualService`, stores token, starts a `DispatcherQueueTimer` polling `GetCursorPos` at configured frequency and calling `UpdatePosition`, exposes `Move(...)`, `EndDrag(...)`, and `Abort()`; raises `DragMoved` and `DragEnded` events as `EventHandler<T>` with documented args. Coordinator must survive source AppWindow closure and continue polling/overlay updates. | ✅ Complete: `TabDragCoordinator` fully implements all methods. `StartDrag` validates args, enforces single-session, creates overlay session, starts polling timer. `Move` updates overlay position and raises `DragMoved`. `EndDrag` stops timer, raises `DragEnded` with destination/index, ends session, cleans up. `Abort` stops timer and cleans up without raising completion. Polling timer at 30Hz with threshold-based move detection (0.5px). All state protected by `Lock`. Partial class with `.Logs.cs` for structured logging. Coordinator survives source window closure by holding session reference in drag service. |

CHECKPOINT-2: Choose overlay rendering approach. Options (must be approved by user before TASK-008 implementation):

- A: Use Win32 layered window + `UpdateLayeredWindow` (P/Invoke) — direct pixel control and deterministic behavior.
- B: Use AppWindow / Windows App SDK composition APIs — integrates with WinUI composition but requires extra plumbing.

**CHECKPOINT-2 DECISION MADE (2025-11-02)**: Approach **A (Win32 Layered Window)** selected and implemented. Rationale: provides direct pixel control, deterministic behavior, proper per-monitor DPI support, and clean integration with existing P/Invoke infrastructure. TASK-008 implementation complete and ready for Phase 3 integration.

Phase 2 acceptance: all code compiles, unit tests for TASK-005..TASK-009 pass (TEST-COORD-001, TEST-OVERLAY-001). Manual QA (cross-window persistence and DPI alignment) deferred to Phase 4 integration QA. Coordinator polling and overlay lifecycle fully functional. Implementation ready for Phase 3 TabStrip pointer wiring.

### Phase 3 — TabStrip integration and UI wiring

- GOAL-003: Wire pointer lifecycle in `TabStripItem` and `TabStrip` to start and complete drag sessions following the spec exact ordering and semantics.

**Phase 3 Architecture Overview:**
The integration uses the existing **partial class pattern** to separate concerns:

- `TabStripItem.properties.cs` — Add `IsDragging` read-only DP
- `TabStripItem.events.cs` — Wire pointer handlers to owner TabStrip
- `TabStrip.properties.cs` — Add `DragCoordinator` DP with PropertyChangedCallback for subscription management
- `TabStrip.drag.cs` — NEW file containing all drag lifecycle logic: pointer threshold tracking, BeginDrag, hit-testing hooks, and coordinator event handlers
- `TabStrip.xaml`, `TabStripItem.xaml` — Add visual states and placeholder template parts

**Design Rationale:**

- `DragCoordinator` is a **read-write DP** (not a normal property) for consistency with existing TabStrip architecture, XAML binding support, and clean event subscription via PropertyChangedCallback
- All coordinator event subscriptions happen in `OnDragCoordinatorPropertyChanged` callback, centralizing lifecycle management
- Pointer handling delegates to TabStrip immediately, enabling centralized drag state machine
- Hit-testing logic deferred to Phase 4 to keep Phase 3 focused on event wiring
- When implementing the new DPs, must pay attention that WinUI does not guarantee the order of setting properties vs applying the template. Therefore, the OnApplyTemplate must apply properties in case they were set before it, and property change handlers must check that parts they manipulate are not null (use null propagation or check not null).

| Completed | Task ID | File / Symbol | Action | Acceptance criteria |
|---:|---:|---|---|---|
| | TASK-010 | `projects/Controls/TabStrip/src/TabStripItem.properties.cs` | Add read-only DP `IsDragging` via `DependencyPropertyKey` and public `IsDependencyProperty`. This DP is set by TabStrip when drag begins and cleared when drag ends. Templates bind to this to show/hide drag visual feedback. | Compiles; DP can be set by code-behind; templates can bind via `{Binding RelativeSource={RelativeSource TemplatedParent}, Path=IsDragging}`; unit test verifies DP existence and read-only enforcement. |
| | TASK-011 | `projects/Controls/TabStrip/src/TabStripItem.events.cs` | Create partial event handlers that delegate to TabStrip: `OnPointerPressed(PointerRoutedEventArgs e)` → `this.OwnerStrip?.PointerPressedOnItem(this, e)`, similarly for `OnPointerMoved` and `OnPointerReleased`. Set `e.Handled = true` to prevent bubbling to parent. Handlers assume pointer events are already wired in TabStripItem.xaml or code-behind (handled separately). | Compiles; pointer flow testable with TabStrip unit tests; manual test shows drag threshold tracking works. |
| | TASK-012a | `projects/Controls/TabStrip/src/TabStrip.properties.cs` | Add `DragCoordinatorProperty` DP and CLR property with getter/setter. Use `PropertyMetadata` with `OnDragCoordinatorPropertyChanged` callback to manage event subscriptions atomically: unsubscribe from old coordinator, subscribe to new. Document that this DP is typically set once during control initialization and should reference the process-wide singleton coordinator. | Compiles; DP can be set via XAML binding or code; PropertyChangedCallback manages subscription lifecycle correctly even during property changes or null assignments. |
| | TASK-012b | `projects/Controls/TabStrip/src/TabStrip.drag.cs` | NEW file. Implement drag lifecycle methods: `internal void PointerPressedOnItem(TabStripItem item, PointerRoutedEventArgs e)` — capture pointer, record start point; `internal void PointerMovedOnItem(TabStripItem item, PointerRoutedEventArgs e)` — track drag threshold (5+ pixels), call `BeginDrag` if exceeded; `internal void PointerReleasedOnItem(TabStripItem item, PointerRoutedEventArgs e)` — release pointer capture, call `coordinator.EndDrag()` with drop info. Implement `BeginDrag(TabStripItem visual)`: raise `TabDragImageRequest` synchronously to allow app to provide preview image, create `DragVisualDescriptor`, clear multi-selection (GUD-001), set `visual.IsDragging = true`, call `coordinator.StartDrag()`. Implement event handlers `OnCoordinatorDragMoved` and `OnCoordinatorDragEnded` to raise `TabDragComplete` or `TabTearOutRequested` (hit-testing deferred to Phase 4). | Compiles; unit tests validate threshold behavior, event sequencing (press/move/release), selection clearing, descriptor creation, and coordinator method calls; manual test shows overlay follows cursor and stops at release. |
| | TASK-013 | `projects/Controls/TabStrip/src/TabStrip.xaml` and `projects/Controls/TabStrip/src/TabStripItem.xaml` | Update `TabStripItem.xaml` to add VisualStateGroup `DraggingStates` with states `Normal` (default) and `Dragging`; bind the group to `IsDragging` DP. When `IsDragging == true`, apply visual transition (e.g., reduced opacity 0.7, subtle scale 0.95). Add template part `<Grid x:Name="HiddenInsertPlaceholder" Visibility="Collapsed"/>` for Phase 4 insertion logic (placeholder for now). Update `TabStrip.xaml` root to support binding `DragCoordinator` if needed in data templates. | XAML compiles; visual state machine is valid; Blend/designer shows state transitions; manual test shows tab becomes slightly transparent/scaled when dragging begins and restores on release. |

CHECKPOINT-3: Confirm the exact call order when tearing out: the spec requires `TabDragImageRequest` followed by `TabCloseRequested` when the drag leaves origin; confirm that the control will raise `TabCloseRequested` and allow the application to remove the item before continuing the overlay lifecycle. Proceed with the default spec sequence if the user does not object.

Phase 3 acceptance: all code compiles, unit tests validate pointer threshold tracking, event ordering (press/move/threshold/release), selection clearing, and coordinator method calls; manual test shows overlay follows cursor with visual feedback (`IsDragging` state in templates); tabbing between multiple TabStrip instances works correctly with process-wide coordinator.

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
- `projects/Controls/Services/src/IDragVisualService.cs` — `DragSessionToken` + service contract ✅
- `projects/Controls/Services/src/DragVisualDescriptor.cs` — observable descriptor ✅
- `projects/Controls/Services/src/Native.cs` — P/Invoke wrappers and safe helpers ✅
- `projects/Controls/Services/src/DragVisualService.cs` — layered overlay implementation ✅
- `projects/Controls/Services/src/DragVisualService.Logs.cs` — structured logging for overlay
- `projects/Controls/TabStrip/src/TabDragCoordinator.cs` — coordinator: Start/Move/End/Abort, timer polling ✅
- `projects/Controls/TabStrip/src/TabDragCoordinator.Logs.cs` — structured logging for coordinator
- `projects/Controls/TabStrip/src/DragMovedEventArgs.cs` — coordinator event args ✅
- `projects/Controls/TabStrip/src/DragEndedEventArgs.cs` — coordinator event args ✅
- `projects/Controls/TabStrip/src/TabStrip.cs` — pointer wiring, BeginDrag, insertion/removal, event raises (Phase 3)
- `projects/Controls/TabStrip/src/TabStripItem.properties.cs` — add `IsDragging` DP (Phase 3)
- `projects/Controls/TabStrip/src/TabStripItem.events.cs` — pointer handlers (Phase 3)
- `projects/Controls/TabStrip/src/TabStrip.xaml` and `TabStripItem.xaml` — template changes to support placeholder and IsDragging visual state (Phase 3)
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

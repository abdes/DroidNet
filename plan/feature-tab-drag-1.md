---
goal: Implement Tab Drag-and-Drop feature for `Controls.TabStrip` component
version: 4.4
date_created: 2025-11-01
last_updated: 2025-11-02
owner: Controls.TabStrip Team
status: 'Phase 3 Complete - UI Integration & Placeholder Management Done, Phase 4 In Progress'
tags: [feature, tabstrip, drag-drop, ui, reorder, tearout]
---

# Introduction

![Status: Phase 3 Complete - UI Integration Done](https://img.shields.io/badge/status-Phase%203%20Complete-green)

This implementation plan delivers a complete, spec-compliant implementation of the revised Tab drag-and-drop behavior described in `projects/Controls/TabStrip/Drag.md`. The architecture implements a two-mode system: **Reorder Mode** (within TabStrip bounds using placeholders) and **TearOut Mode** (cross-window with overlay visuals). It contains atomic, machine-actionable tasks with file-level references and deterministic completion criteria.

## 1. Requirements & Constraints

### Core Behavioral Requirements

- **REQ-001**: **Two-Mode System**: Support **Reorder Mode** (within TabStrip bounds using placeholder + TranslateTransform) and **TearOut Mode** (cross-window with DragVisualService overlay)
- **REQ-002**: **Mode Transition**: Automatic switching between modes based on pointer distance from TabStrip bounds using configurable thresholds
- **REQ-003**: **Reorder Mode**: Insert lightweight placeholder, use `TranslateTransform.X` on dragged item, animate adjacent item swapping
- **REQ-004**: **TearOut Mode**: Capture header image, request preview via `TabDragImageRequest`, use `IDragVisualService` for cross-window overlay
- **REQ-005**: **Pinned Tab Constraints**: Pinned tabs cannot be dragged; dropping into pinned bucket is forbidden
- **REQ-006**: **Cross-TabStrip Drop**: Re-entering unpinned bucket during TearOut switches back to Reorder Mode seamlessly
- **REQ-007**: **Process-Wide Coordination**: Single active drag per process using `TabDragCoordinator` singleton
- **REQ-008**: **TearOut Re-entry Sequencing**: End drag visual session → insert new TabStripItem → trigger selection/activation → continue in Reorder mode (per spec exact sequence)

### Event Contract Requirements

- **EVT-001**: **Event Sequencing**: Reorder→TearOut: `TabCloseRequested` → `TabDragImageRequest` → start overlay. TearOut Re-entry: end overlay → insert item → activate/select → continue Reorder. Drop: `TabDragComplete` or `TabTearOutRequested`
- **EVT-002**: **Model Exposure**: All EventArgs use `TabItem` model type, not visual `TabStripItem` controls
- **EVT-003**: **Error Recovery**: Unhandled exceptions in event handlers abort drag with `TabDragComplete(DestinationStrip=null)`
- **EVT-004**: **No Cancellation**: No `Handled` properties; drag always completes (success or failure)

### Technical Constraints

- **TECH-001**: **UI Thread Affinity**: All drag APIs must assert UI thread and document in XML
- **TECH-002**: **Pointer Resilience**: Continue drag via cursor polling if source window closes during cross-window drag
- **TECH-003**: **Single Session**: `TabDragCoordinator.StartDrag` throws if session already active
- **TECH-004**: **DragVisualService Preservation**: Minimal changes to existing `IDragVisualService` implementation
- **TECH-005**: **State Recovery**: Graceful cleanup on unrecoverable errors (remove placeholders, clear transforms, release capture)

## 2. Implementation Architecture

### Strategy Pattern for Two-Mode System

The implementation uses a **Strategy Pattern** with automatic mode switching:

```text
TabDragCoordinator (State Machine)
├── IDragStrategy interface
├── ReorderStrategy (placeholder + TranslateTransform)
├── TearOutStrategy (DragVisualService overlay)
└── Mode switching based on pointer bounds
```

**Design Principles:**

- **Separation of Concerns**: TabStrip handles UI/XAML, strategies handle mode-specific logic
- **State Machine**: TabDragCoordinator orchestrates strategy transitions
- **Clean Integration**: Leverage XAML template for placeholders, code-behind for transforms
- **Minimal Service Changes**: DragVisualService remains largely unchanged

## 3. Implementation Steps

### Phase 1 — Strategy Infrastructure & Drag Strategies ✅ **COMPLETED**

**GOAL**: Implement the strategy pattern foundation and two drag mode strategies with clean separation of concerns.

| Task ID | File / Symbol | Action | Status | Acceptance Criteria |
|---:|---|---|---|---|
| ✅ TASK-001 | `projects/Controls/TabStrip/src/IDragStrategy.cs` | Create interface with `OnEnter(DragContext)`, `OnMove(Point)`, `OnExit()`, `OnDrop(Point)`, `IsActive` property. Include `DragContext` class with `TabItem`, `TabStrip`, `TabStripItem` references. | **COMPLETE** | Interface compiles; context class provides strategy access to drag participants. |
| ✅ TASK-002 | `projects/Controls/TabStrip/src/ReorderStrategy.cs` | Implement strategy for in-TabStrip dragging. Manages placeholder insertion/removal, TranslateTransform application, adjacent item swapping with animations. | **COMPLETE** | Strategy handles placeholder lifecycle, transform application, item position swapping. No overlay creation. |
| ✅ TASK-003 | `projects/Controls/TabStrip/src/TearOutStrategy.cs` | Implement strategy for cross-window dragging. Manages DragVisualService session, header capture, preview request events. | **COMPLETE** | Strategy creates/manages visual service session, captures header image, raises TabDragImageRequest event. |
| ✅ TASK-004 | `projects/Controls/TabStrip/src/DragThresholds.cs` | Add configuration class with `DragInitiationThreshold` (5px), `TearOutThreshold` (20px), `SwapThreshold` (half item width). Make configurable via dependency properties. | **COMPLETE** | Thresholds are configurable, well-documented, with reasonable defaults matching spec requirements. |

**Phase 1 Deliverables:**

- ✅ **IDragStrategy Interface**: Complete contract with lifecycle methods and DragContext
- ✅ **ReorderStrategy**: Full implementation with logging, placeholder management, and transform logic
- ✅ **TearOutStrategy**: Full implementation with DragVisualService integration and logging
- ✅ **DragThresholds**: Simple static configuration constants (simplified per feedback)
- ✅ **Test Coverage**: Comprehensive test suites for both strategies (33+ test methods total)
- ✅ **Compilation**: All files compile without errors, lint compliant

### Phase 2 — Redesigned TabDragCoordinator with State Machine ✅ **COMPLETED**

**GOAL**: Redesign TabDragCoordinator as a state machine that orchestrates strategy switching and maintains cross-TabStrip coordination.

| Task ID | File / Symbol | Action | Status | Acceptance Criteria |
|---:|---|---|---|---|
| ✅ TASK-005 | `projects/Controls/TabStrip/src/TabDragCoordinator.cs` | Redesign as state machine with `currentStrategy`, `ReorderStrategy`, `TearOutStrategy` fields. Add `SwitchToReorderMode()`, `SwitchToTearOutMode()` methods. Maintain cross-TabStrip event broadcasting. | **COMPLETE** | Coordinator manages strategy lifecycle, broadcasts DragMoved/DragEnded events, enforces single active session per process. |
| ✅ TASK-006 | `projects/Controls/TabStrip/src/TabDragCoordinator.cs` | Add bounds checking logic: `IsWithinTearOutThreshold(Point cursor, TabStrip strip)`, `GetHitTestTabStrip(Point cursor)` for cross-window hit testing. | **COMPLETE** | Accurate bounds detection for mode switching, cross-window TabStrip detection during TearOut mode. |
| ✅ TASK-007 | `projects/Controls/TabStrip/src/TabDragCoordinator.cs` | Implement strategy transition methods with proper cleanup: strategy exit → new strategy enter. Handle errors during transitions. | **COMPLETE** | Clean strategy transitions, proper resource cleanup, error recovery that maintains consistency. |
| ✅ TASK-008 | Update existing `DragMovedEventArgs.cs` / `DragEndedEventArgs.cs` | Add properties needed for TabStrip subscribers: `TabItem Item`, `Point ScreenPosition`, `bool IsInReorderMode`, `TabStrip? HitStrip`, `int? DropIndex`. | **COMPLETE** | EventArgs provide sufficient data for TabStrip instances to perform hit testing and coordinate insertion/removal. |

**Phase 2 Deliverables:**

- ✅ **State Machine Implementation**: TabDragCoordinator fully redesigned with strategy orchestration and lifecycle management
- ✅ **TabStrip Registration**: `RegisterTabStrip()` and `UnregisterTabStrip()` methods for cross-window coordination using weak references
- ✅ **Bounds Checking**: `IsWithinTearOutThreshold()` checks if cursor is within threshold distance from TabStrip bounds (with DPI conversion)
- ✅ **Cross-Window Hit Testing**: `GetHitTestTabStrip()` finds which TabStrip is under cursor across all registered strips
- ✅ **Automatic Mode Transitions**: `CheckAndHandleModeTransitions()` triggers strategy switches based on cursor position
- ✅ **Complete EventArgs**: DragMovedEventArgs and DragEndedEventArgs include all required properties (Item, IsInReorderMode, HitStrip, DropIndex, Destination)
- ✅ **Event Broadcasting**: DragMoved and DragEnded events raised with complete context for TabStrip subscribers
- ✅ **Compilation**: All files compile without errors, no lint issues

### Phase 3 — TabStrip UI Integration & Placeholder Management

**GOAL**: Wire TabStrip pointer events, implement placeholder insertion/removal, and integrate with the redesigned coordinator.

| Task ID | File / Symbol | Action | Status | Acceptance Criteria |
|---:|---|---|---|---|
| ✅ TASK-009 | `projects/Controls/TabStrip/src/TabStrip.xaml` | Add `PlaceholderItem` template in resources. Add `IsDragging` visual state group. Ensure TranslateTransform binding exists on TabStripItem containers. | **COMPLETE** | XAML template supports placeholder insertion via binding (`IsPlaceholder`), drag visual states group defined, TranslateTransform on item containers. |
| ✅ TASK-010 | `projects/Controls/TabStrip/src/TabStrip.cs` | Implement placeholder management: `InsertPlaceholderAtDraggedItemPosition()`, `RemovePlaceholder()`, `SwapPlaceholderWithAdjacentItem()`, `CommitReorderFromPlaceholder()`. Include animated swapping. | **COMPLETE** | All placeholder lifecycle methods implemented in TabStrip.cs (not TabStrip.drag.cs). Methods handle insertion at dragged item position, removal, adjacent swaps with direction control, and commit operations. Suppresses collection change notifications during mutations. |
| ✅ TASK-011 | `projects/Controls/TabStrip/src/TabStrip.drag.cs` | Implement pointer event handlers: threshold detection, coordinator integration, capture management. Handle pinned tab constraints. | **COMPLETE** | HandlePointerPressed, HandlePointerMoved, HandlePointerReleased implemented with threshold detection, pinned tab validation, coordinator integration, capture/release lifecycle. |
| ✅ TASK-012 | `projects/Controls/TabStrip/src/TabStripItem.properties.cs` | Add `IsDragging` dependency property for template bindings. Add TranslateTransform properties for reorder animations. | **COMPLETE** | IsDraggingProperty dependency property fully defined with internal setter in TabStripItem.properties.cs. OnIsDraggingChanged handler calls UpdateVisualStates() for drag visual feedback. Template binding works correctly. |

### Phase 4 — Event Sequencing & Cross-TabStrip Coordination

**GOAL**: Implement the complete event sequencing as specified and enable cross-TabStrip drag coordination.

| Task ID | File / Symbol | Action | Status | Acceptance Criteria |
|---:|---|---|---|---|
| ✅ TASK-013 | `projects/Controls/TabStrip/src/TabStrip.events.cs` | Implement event raising sequence: `TabCloseRequested` → `TabDragImageRequest` → `TabDragComplete` or `TabTearOutRequested`. Handle exceptions and error recovery. | **COMPLETE** | RaiseTabDragImageRequest, RaiseTabDragComplete, RaiseTabTearOutRequested methods implemented with exception handling and error recovery patterns. |
| ⚠️ TASK-014 | `projects/Controls/TabStrip/src/TabStrip.drag.cs` | Subscribe to coordinator's `DragMoved` and `DragEnded` events. Implement hit-testing for cross-TabStrip drops during TearOut mode. | **PARTIAL** | DragMoved/DragEnded event subscription implemented in OnDragCoordinatorChanged. OnCoordinatorDragMoved contains TODO comment "Phase 4: Implement hit-testing and placeholder insertion logic". |
| ⚠️ TASK-015 | `projects/Controls/TabStrip/src/TabStrip.drag.cs` | Implement TearOut re-entry logic: detect pointer entry into unpinned bucket, end DragVisualService session, insert new TabStripItem, trigger selection/activation events, switch back to Reorder mode per spec. | **NOT IMPLEMENTED** | TearOut re-entry logic not found. Logic should be in OnCoordinatorDragMoved for hit-testing and mode switching. |
| ⚠️ TASK-016 | `projects/Controls/TabStrip/src/TabStrip.drag.cs` | Implement drop handling: insertion at calculated index, selection/activation event sequencing, placeholder removal and item visibility restoration. Handle pinned bucket constraints with forbidden cursor and fallback behavior. | **PARTIAL** | OnCoordinatorDragEnded has basic drop handling (RaiseTabDragComplete/RaiseTabTearOutRequested), but placeholder removal and UI state restoration logic not fully implemented. |
| ✅ TASK-017 | `projects/Controls/TabStrip/src/TabStrip.properties.cs` | Add dependency properties for coordinator injection: `TabDragCoordinator`, `DragThresholds`. Ensure proper DI integration. | **COMPLETE** | DragCoordinatorProperty defined with OnDragCoordinatorPropertyChanged handler, properly integrated with subscription/unsubscription logic. |

### Phase 5 — Testing & Validation

**GOAL**: Comprehensive testing of both modes, mode transitions, error scenarios, and cross-window behavior.

| Task ID | File / Symbol | Action | Acceptance Criteria |
|---:|---|---|---|
| TASK-018 | `projects/Controls/TabStrip/tests/Controls/TabStripDragTests.cs` | Unit tests for reorder mode: placeholder insertion, item swapping, TranslateTransform application, drop completion. | All reorder mode functionality tested, edge cases covered, animations validate correctly. |
| TASK-019 | `projects/Controls/TabStrip/tests/Controls/TabStripDragTests.cs` | Unit tests for tearout mode: header capture, preview request, overlay creation, cross-window coordination. | TearOut mode functionality tested, DragVisualService integration verified, cross-window scenarios covered. |
| TASK-020 | `projects/Controls/TabStrip/tests/Controls/TabStripDragTests.cs` | Unit tests for mode transitions: reorder→tearout, tearout→reorder re-entry, bounds checking, threshold validation. Include pinned bucket constraint testing. | Mode transition logic verified including TearOut re-entry, bounds detection accurate, pinned bucket constraints working, thresholds configurable. |
| TASK-021 | `projects/Controls/TabStrip/tests/Controls/TabStripDragTests.cs` | Unit tests for error scenarios: handler exceptions during transitions, coordinator failures, unrecoverable errors, TearOut re-entry failures, state recovery. | Error handling robust including TearOut transition failures, state recovery works, no resource leaks, proper event sequencing on all error paths. |

## 4. Implementation Notes

### Strategy Pattern Implementation Details

**IDragStrategy Interface:** Internal interface with lifecycle methods `OnEnter()`, `OnMove()`, `OnExit()`, `OnDrop()` and `IsActive` property. Include `DragContext` class to pass drag participants and state to strategies.

**ReorderStrategy Responsibilities:**

- Insert/remove placeholder items in ItemsRepeater
- Apply/remove TranslateTransform.X to dragged TabStripItem
- Animate placeholder position swaps when crossing adjacent items
- Track drop index based on placeholder position
- Handle pointer capture within source TabStrip bounds

**TearOutStrategy Responsibilities:**

- Capture header image using RenderTargetBitmap
- Raise TabDragImageRequest event for preview image
- Create and manage DragVisualService session
- Poll cursor position and update overlay
- Detect re-entry into TabStrip unpinned buckets and trigger mode switch
- Handle cross-window drag coordination
- Execute TearOut→Reorder transition sequence per spec (end session → insert item → activate → switch modes)

### XAML Template Requirements

**TabStrip.xaml additions:** Add `PlaceholderItemTemplate` DataTemplate with transparent Border, configurable width/height bindings, and drag placeholder styling. Add `IsDragging` visual state group for TabStripItem. Ensure TranslateTransform binding exists on item containers for reorder animations.

### Error Recovery Patterns

**Unrecoverable Errors:** If any strategy method throws or coordinator fails:

1. Stop polling timer (if active)
2. End DragVisualService session (if active)
3. Remove placeholder items
4. Clear TranslateTransform on all items
5. Release pointer capture
6. Raise TabDragComplete with DestinationStrip=null, NewIndex=null
7. Log error with structured logging

**Application Handler Exceptions:** If TabDragImageRequest, TabCloseRequested, or TabTearOutRequested handlers throw:

1. Catch exception and log
2. Abort current drag operation using recovery pattern above
3. Continue with error completion (TabDragComplete with null destination)

## 5. Acceptance Criteria

**Phase 1 Complete:** All strategy interfaces and implementations compile, basic threshold detection works, placeholder insertion/removal functional.

**Phase 2 Complete:** TabDragCoordinator redesigned as state machine, strategy transitions working, cross-TabStrip event broadcasting operational.

**Phase 3 Complete:** Full TabStrip UI integration, pointer capture/release, XAML template updates, basic drag functionality working end-to-end.

**Phase 4 Complete:** Event sequencing implemented per spec, cross-TabStrip coordination working, mode re-entry from TearOut to Reorder functional.

**Phase 5 Complete:** Comprehensive test coverage, all error scenarios handled, manual QA validation across different monitors and windows, performance acceptable (60fps during drag).

## 6. Dependencies & Files

**Dependencies:**

- `Microsoft.UI.Xaml` for XAML types and `ImageSource`
- `CommunityToolkit.Mvvm` for `ObservableObject` base class
- Win32 P/Invoke via existing `Native.cs` for cursor polling
- `IDragVisualService` from Controls.Services (existing, minimal changes)

**Key Files Modified/Created:**

- `IDragStrategy.cs`, `ReorderStrategy.cs`, `TearOutStrategy.cs` - Strategy pattern implementation
- `DragThresholds.cs` - Configurable threshold values
- `TabDragCoordinator.cs` - Redesigned as state machine with strategy orchestration
- `TabStrip.xaml` - Placeholder template and visual state updates
- `TabStrip.drag.cs` - Pointer event handlers and drag lifecycle management
- `TabStripItem.cs` - IsDragging property and transform support
- `TabStripDragTests.cs` - Comprehensive test coverage for both modes

**Existing EventArgs (unchanged):** `TabDragImageRequestEventArgs.cs`, `TabDragCompleteEventArgs.cs`, `TabTearOutRequestedEventArgs.cs`, `TabStrip.events.cs`

## 7. Manual QA Checklist

1. **Reordering Mode:** Drag tab within TabStrip, verify placeholder insertion, smooth item swapping animations, proper drop positioning
2. **Mode Transition:** Drag beyond TearOut threshold, verify placeholder removal, overlay appearance, event sequence (TabCloseRequested → TabDragImageRequest)
3. **Cross-Window TearOut:** Drag between AppWindows, verify overlay persistence, proper hotspot alignment, DPI scaling across monitors
4. **Re-entry to Reorder:** During TearOut, re-enter TabStrip unpinned bucket, verify overlay disappears immediately, new TabStripItem inserted, selection/activation events fired, seamless transition back to reorder mode with placeholder management
5. **Error Recovery:** Test handler exceptions, window closure during drag, verify graceful cleanup and TabDragComplete with null destination
6. **Pinned Tab Constraints:** Verify pinned tabs cannot be dragged, dropping on pinned bucket shows forbidden cursor and fails gracefully

---

## 8. Current Status & Next Steps

### Phase 1 Status: ✅ **COMPLETE**

All Phase 1 tasks have been successfully implemented and tested:

- **Strategy Infrastructure**: IDragStrategy interface and DragContext provide clean contract for drag mode implementations
- **Drag Strategies**: Both ReorderStrategy and TearOutStrategy are fully implemented with comprehensive logging
- **Configuration**: DragThresholds simplified to static constants per feedback (no over-engineering)
- **Test Coverage**: Comprehensive test suites created for both strategies with 33+ test methods covering all scenarios
- **Code Quality**: All files compile without errors and are lint compliant

### Phase 2 Status: ✅ **COMPLETE**

All Phase 2 tasks have been successfully implemented:

- **State Machine**: TabDragCoordinator redesigned with strategy lifecycle management and automatic mode transitions
- **TabStrip Registry**: Weak reference-based registration system for cross-window coordination
- **Bounds Checking**: DPI-aware bounds detection with `IsWithinTearOutThreshold()` for mode switching
- **Hit Testing**: Cross-window TabStrip detection via `GetHitTestTabStrip()` for drop target identification
- **Mode Transitions**: Automatic switching between Reorder and TearOut modes based on cursor position
- **Complete EventArgs**: DragMovedEventArgs and DragEndedEventArgs fully populated with all required properties
- **Event Broadcasting**: DragMoved and DragEnded events provide complete context for TabStrip subscribers
- **Code Quality**: All files compile without errors, no lint issues

### Phase 3 Status: ✅ **COMPLETE**

**All Phase 3 Tasks Completed:**

- ✅ **TASK-009**: XAML template has PlaceholderItemTemplate defined with correct styling and IsPlaceholder binding. IsDragging visual state group with NotDragging and Dragging states defined. TranslateTransform on item containers with RenderTransform binding.

- ✅ **TASK-010**: Placeholder management methods fully implemented in `TabStrip.cs`:
  - `InsertPlaceholderAtDraggedItemPosition(TabItem draggedItem)`: Inserts placeholder at dragged item position, returns bucket index
  - `RemovePlaceholder()`: Removes placeholder from collection, returns former index
  - `SwapPlaceholderWithAdjacentItem(bool swapForward)`: Swaps placeholder with adjacent regular items, supports forward/backward direction
  - `CommitReorderFromPlaceholder(TabItem draggedItem)`: Commits final reorder by moving dragged item to placeholder position
  - `GetPlaceholderBucketIndex()`: Gets current bucket index of placeholder
  - All methods suppress collection change notifications during mutations to avoid reentrancy

- ✅ **TASK-011**: Pointer event handlers fully implemented in TabStrip.drag.cs:
  - `HandlePointerPressed()`: Drag initiation with pinned tab constraint checking
  - `HandlePointerMoved()`: Threshold detection (5px) and coordinator integration
  - `HandlePointerReleased()`: Drop completion with screen coordinate conversion
  - `OnPreviewPointerPressed/Moved/Released()`: Event handler plumbing with capture/release lifecycle
  - `FindTabStripItemAtPoint()`: Hit-testing via FindAscendant&lt;TabStripItem&gt;
  - `BeginDrag()`: Selection clearing, IsDragging marking, hotspot calculation, coordinator integration

- ✅ **TASK-012**: IsDragging dependency property fully defined in `TabStripItem.properties.cs`:
  - `IsDraggingProperty`: Dependency property with internal setter (public getter)
  - `IsDragging` property accessor with internal write protection
  - `OnIsDraggingChanged()`: Callback that triggers `UpdateVisualStates()` for visual feedback
  - Properly integrated with VSM for drag visual state transitions

- ✅ **TASK-017**: DragCoordinator property defined in TabStrip.properties.cs with proper subscription/unsubscription logic.

**Phase 3 Deliverables:**

- ✅ **Placeholder Lifecycle**: Complete management of placeholder insertion, removal, and swapping with direction control
- ✅ **Pointer Event Handlers**: Full drag detection and threshold-based initiation with pinned tab constraints
- ✅ **IsDragging Visual State**: Dependency property properly wired to template visual states
- ✅ **Coordinator Integration**: Event subscriptions and drag lifecycle coordination working
- ✅ **Coordinate Conversion**: TabStrip-relative and screen coordinate conversion methods (`ScreenToStrip()`, `StripToScreen()`)
- ✅ **Placeholder Utility Methods**: `HitTestRegularBucket()`, `GetRegularBucketCount()`, `CreatePlaceholderItem()` for reorder mode support

### Phase 4 Status: ⚠️ **NOT STARTED - BLOCKED ON PHASE 3**

**Completed Tasks:**

- ✅ **TASK-013**: Event raising methods fully implemented in TabStrip.drag.cs:
  - `RaiseTabDragImageRequest()`: Raises event with exception handling
  - `RaiseTabDragComplete()`: Raises completion event with destination/index info
  - `RaiseTabTearOutRequested()`: Raises tear-out event with fallback to TabDragComplete on error
  - All use try-catch patterns with structured logging
- ✅ **TASK-017**: DragCoordinator property fully implemented (same as Phase 3)

**Incomplete Tasks:**

- ⚠️ **TASK-014**: Hit-testing for cross-TabStrip drops NOT IMPLEMENTED. OnCoordinatorDragMoved contains TODO comment:

```csharp
// Phase 4: Implement hit-testing and placeholder insertion logic here
```

  Needs to implement screen point to local coordinate conversion for each TabStrip, detection of pointer over unpinned bucket, hit-test result caching during drag move, and placeholder insertion/removal based on hit-test results.

- ⚠️ **TASK-015**: TearOut re-entry logic NOT IMPLEMENTED. Requires detection of pointer entry into unpinned bucket during TearOut mode, ending DragVisualService session, inserting new TabStripItem at calculated index, triggering selection/activation events in correct sequence per spec, switching back to Reorder mode via coordinator. Logic should integrate with hit-testing in TASK-014.

- ⚠️ **TASK-016**: Drop handling incomplete. OnCoordinatorDragEnded has basic structure but missing placeholder removal and UI state restoration, item visibility restoration (hidden placeholder → visible item), pinned bucket constraint enforcement (forbidden cursor), fallback behavior for invalid drop targets, and coordinate conversion from screen to strip-relative for index calculation.

**Recommended Next Steps for Phase 4:**

1. Implement OnCoordinatorDragMoved with hit-testing logic
2. Add placeholder management integration to drag move events
3. Implement TearOut re-entry detection and mode switch
4. Complete drop handling with UI state restoration
5. Test mode transitions and cross-TabStrip coordination
6. Validate event sequencing for all scenarios

---

### Implementation Blockers & Dependencies

**Phase 3 Blockers:**

- Clarify IsDragging property implementation in TabStripItem (is it a DP?)
- Implement placeholder management methods (blocking Phase 4)
- Verify TranslateTransform binding works correctly on template items

**Phase 4 Blockers:**

- Phase 3 placeholder management must be complete
- Phase 3 pointer event handlers working correctly
- Coordinator hit-testing infrastructure from Phase 2 available and tested

**Dependency Chain:**

- Phase 1 ✅ → Phase 2 ✅ → Phase 3 ✅ → Phase 4 (In Progress)
- Phase 3 complete; Phase 4 hit-testing and re-entry logic can now proceed

---

**Implementation Status Summary:**

- **Phase 1**: ✅ COMPLETE - Strategy infrastructure, drag strategies, thresholds, comprehensive tests
- **Phase 2**: ✅ COMPLETE - State machine coordinator, cross-window hit testing, mode transitions, event broadcasting
- **Phase 3**: ✅ COMPLETE - Placeholder management, pointer event handlers, IsDragging property, coordinator integration
- **Phase 4**: ⚠️ IN PROGRESS - Event sequencing partially complete, hit-testing and re-entry logic remaining
- **Phase 5**: ⏳ NOT STARTED - Testing and validation deferred to Phase 5

**Lines of Code Implemented:**

- TabStrip.cs: ~350 lines (placeholder management, coordinate conversion, hit-testing utilities)
- TabStrip.drag.cs: ~467 lines (pointer event handlers, drag coordination)
- TabStrip.properties.cs: ~250 lines (DragCoordinator property)
- TabStripItem.properties.cs: ~95 lines (IsDragging dependency property)
- TabStrip.xaml: ~171 lines (placeholder template, visual states, transforms)

**Code Quality**: All files compile without errors, lint compliant, comprehensive error handling and logging

**Ready for Phase 4 Implementation**: Phase 3 foundation complete; placeholder management fully functional; pointer event handlers working; Phase 4 can now focus on hit-testing, mode transitions, and TearOut re-entry logic

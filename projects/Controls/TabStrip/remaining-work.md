---
goal: Complete Production-Ready TabStrip Drag-Drop Implementation
version: 1.0
date_created: 2025-11-02
last_updated: 2025-11-02
owner: DroidNet Controls Team
status: 'In Progress'
tags: [feature, drag-drop, tabstrip, production, controls]
---

# TabStrip Drag-Drop: Remaining Work

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

This document identifies all remaining work required to complete the production-ready implementation of TabStrip drag-and-drop functionality according to `Drag.md` specifications.

## Current Status Analysis

### ✅ Completed Features

- TabDragCoordinator singleton with cross-window coordination
- Strategy pattern (ReorderStrategy, TearOutStrategy) with automatic mode switching
- DragVisualService with Win32 layered window overlay (process-global, survives window closure)
- Placeholder-based reordering within strips (insertion, swapping, commit)
- Pointer event handling with drag threshold detection
- DPI-aware coordinate transformations (logical ↔ physical, multi-monitor)
- Polling timer for cursor tracking during TearOut mode (60Hz)
- Basic event infrastructure (TabDragComplete, TabTearOutRequested)
- Comprehensive logging with partial classes for log methods

### ❌ Missing Features (from code analysis)

**Critical TODO items identified:**

1. `TearOutStrategy.cs:139` - Header image capture (DONE — implemented via RenderTargetBitmap; non-blocking capture of full TabStripItem)
2. `DragVisualService.cs:338` - Render header/preview images (currently solid color)
3. `TearOutStrategy.cs:162` - TabDragImageRequest event (DONE — routed via TabStrip; coordinator mediates request)
4. `TabStrip.drag.cs:332` - OnCoordinatorDragMoved handler (empty, no re-entry logic)
5. `TearOutStrategy.cs:89` - Check for re-entry into TabStrip unpinned buckets
6. Event sequencing incomplete: TabDragImageRequest → TabCloseRequested during TearOut
7. Esc key abort handling not implemented
8. Visual states (IsDragging) defined but not fully utilized
9. Forbidden drop zone cursor feedback not implemented
10. Error recovery for handler exceptions inconsistent

## 1. Requirements & Constraints

### Functional Requirements

- **REQ-001**: Header image capture using RenderTargetBitmap from TabStripItem
- **REQ-002**: TabDragImageRequest event raised during TearOut threshold transition
- **REQ-003**: Cross-window re-entry from TearOut to Reorder when cursor enters unpinned bucket
- **REQ-004**: TabCloseRequested event raised when transitioning Reorder → TearOut
- **REQ-005**: Event sequence: TabDragImageRequest → TabCloseRequested → TabDragComplete/TabTearOutRequested
- **REQ-006**: Esc key aborts drag, restores original state
- **REQ-007**: All EventArgs use TabItem (model), not TabStripItem (visual)
- **REQ-008**: Comprehensive error recovery for control errors and handler exceptions
- **REQ-009**: Multi-monitor DPI handling during cross-monitor drags
- **REQ-010**: Visual feedback for forbidden drop zones (pinned bucket)

### Performance Requirements

- **PERF-001**: Header capture < 50ms synchronously
- **PERF-002**: Polling timer maintains 60Hz (16.67ms per tick)
- **PERF-003**: Cross-window hit-test < 5ms
- **PERF-004**: TabDragImageRequest handlers must be synchronous

### Constraints

- **CON-001**: All drag operations on UI thread (AssertUIThread enforced)
- **CON-002**: One drag session per process (TabDragCoordinator singleton)
- **CON-003**: Pinned tabs cannot be dragged
- **CON-004**: Dragging into pinned bucket forbidden
- **CON-005**: Multi-selection cleared on drag start
- **CON-006**: Use Win32 GetCursorPos for cursor position (physical pixels)
- **CON-007**: Overlay survives source window closure
- **CON-008**: Explicit DPI conversions (no implicit scaling)

### Guidelines

- **GUD-001**: Strategy pattern with automatic mode switching
- **GUD-002**: Event handlers non-blocking; catch all exceptions
- **GUD-003**: Partial classes for organization (drag, events, properties, logs)
- **GUD-004**: Comprehensive logging (Debug, Info, Warning, Error levels)
- **GUD-005**: XAML-first for visual feedback (visual states, theme resources)

## 2. Implementation Phases

### Phase 1: Header Image Capture & Visual Enhancement

**Goal**: Complete drag visual overlay with header and preview images

| Done | Task | File(s) | Priority |
|------|------|---------|----------|
| ✅ | TASK-001: Implement CaptureHeaderImage() using RenderTargetBitmap on PartContentRootGrid | `TearOutStrategy.cs` | Critical |
| ⬜ | TASK-002: Render HeaderImage + PreviewImage in DragVisualService overlay | `DragVisualService.cs` (Services project) | Critical |
| ⬜ | TASK-003: Add ConvertImageSourceToBitmap helper for WinUI → Win32 | `DragVisualService.cs` | Critical |
| ✅ | TASK-004: Update RequestedSize default to (300, 150) | `TearOutStrategy.cs` | Low |
| ⬜ | TASK-005: Apply Dragging visual state (opacity 0.5, scale 0.95) | `TabStripItem.xaml` | Medium |
| ✅ | TASK-006: Test header capture with various tab configs | `TearOutStrategyTests.cs` | Medium |

### Phase 2: TabDragImageRequest Event

**Goal**: Enable application-provided preview images

| Done | Task | File(s) | Priority |
|------|------|---------|----------|
| ✅ | TASK-008: Raise event from TearOutStrategy.RequestPreviewImage() | `TearOutStrategy.cs` | Critical |
| ✅ | TASK-009: Wire coordinator event to TabStrip.TabDragImageRequest | `TabStrip.drag.cs` | Critical |
| ✅ | TASK-010: Update descriptor when handler sets PreviewImage | `DragVisualDescriptor.cs` | Medium |
| ✅ | TASK-011: Test event sequencing and image propagation | `TabDragCoordinatorTests.cs` | Medium |

### Phase 3: TabCloseRequested During TearOut

**Goal**: Notify application when drag transitions to TearOut

| Done | Task | File(s) | Priority |
|------|------|---------|----------|
| ⬜ | TASK-012: Add TearOutInitiated event to TabDragCoordinator | `TabDragCoordinator.cs` | Critical |
| ⬜ | TASK-013: Raise TearOutInitiated in SwitchToTearOutMode() | `TabDragCoordinator.cs` | Critical |
| ⬜ | TASK-014: Handle event in TabStrip, raise TabCloseRequested | `TabStrip.drag.cs` | Critical |
| ⬜ | TASK-015: Document TabCloseRequested contract during drag | `TabStrip.events.cs` (XML docs) | Medium |
| ⬜ | TASK-016: Test event raised exactly once at threshold | `TabStripDragTests.cs` | Medium |

### Phase 4: Cross-Window Re-Entry

**Goal**: Transition TearOut → Reorder when entering unpinned bucket

| Done | Task | File(s) | Priority |
|------|------|---------|----------|
| ⬜ | TASK-017: Implement OnCoordinatorDragMoved with hit-testing | `TabStrip.drag.cs` | Critical |
| ⬜ | TASK-018: Add IsPointerOverUnpinnedBucket() method | `TabStrip.cs` | Critical |
| ⬜ | TASK-019: Insert hidden placeholder at drop position | `TabStrip.cs` | Critical |
| ⬜ | TASK-020: Add NotifyReentry() to TabDragCoordinator | `TabDragCoordinator.cs` | Critical |
| ⬜ | TASK-021: Handle re-entry in TearOutStrategy | `TearOutStrategy.cs` | Critical |
| ⬜ | TASK-022: Implement cross-strip item transfer logic | `TabStrip.cs` | Critical |
| ⬜ | TASK-023: Test cross-window re-entry scenarios | `TabStripDragTests.cs` | High |
| ⬜ | TASK-024: Test multi-monitor DPI during re-entry | `TabStripDragTests.cs` | Medium |

### Phase 5: Esc Key Abort

**Goal**: Allow user to cancel drag with Esc key

| Done | Task | File(s) | Priority |
|------|------|---------|----------|
| ⬜ | TASK-025: Add PreviewKeyDown handler to TabStrip | `TabStrip.drag.cs` | High |
| ⬜ | TASK-026: Verify TabDragCoordinator.Abort() handles both modes | `TabDragCoordinator.cs` | High |
| ⬜ | TASK-027: Handle abort in ReorderStrategy (restore state) | `ReorderStrategy.cs` | High |
| ⬜ | TASK-028: Handle abort in TearOutStrategy (end session) | `TearOutStrategy.cs` | High |
| ⬜ | TASK-029: Test Esc abort in Reorder and TearOut modes | `TabStripDragTests.cs` | Medium |

### Phase 6: Error Handling & Recovery

**Goal**: Graceful degradation on errors, no crashes

| Done | Task | File(s) | Priority |
|------|------|---------|----------|
| ⬜ | TASK-030: Wrap all event raises in try-catch | `TabStrip.drag.cs` | Critical |
| ⬜ | TASK-031: Add HandleUnrecoverableError() to coordinator | `TabDragCoordinator.cs` | Critical |
| ⬜ | TASK-032: Handle header capture failures gracefully | `TearOutStrategy.cs` | High |
| ⬜ | TASK-033: Handle TabTearOutRequested exceptions | `TabStrip.drag.cs` | High |
| ⬜ | TASK-034: Ensure CleanupState() handles all error paths | `TabDragCoordinator.cs` | High |
| ⬜ | TASK-035: Add defensive null checks everywhere | All drag files | Medium |
| ⬜ | TASK-036: Test error scenarios (handler throws, capture fails) | `TabStripDragTests.cs` | High |

### Phase 7: Visual Feedback & Forbidden Zones

**Goal**: Clear visual feedback for drag state and drop validity

| Done | Task | File(s) | Priority |
|------|------|---------|----------|
| ⬜ | TASK-037: Add IsPointerOverPinnedBucket() method | `TabStrip.cs` | Medium |
| ⬜ | TASK-038: Set forbidden cursor over pinned bucket | `TabStrip.drag.cs` | Medium |
| ⬜ | TASK-039: Reset cursor to default when leaving forbidden zone | `TabStrip.drag.cs` | Medium |
| ⬜ | TASK-040: Add ForbiddenDrop visual state to TabStrip.xaml | `TabStrip.xaml` | Low |
| ⬜ | TASK-041: Enhance Dragging visual state in TabStripItem | `TabStripItem.xaml` | Low |

### Phase 8: Comprehensive Testing

**Goal**: 85%+ code coverage, all scenarios tested

| Done | Task | File(s) | Priority |
|------|------|---------|----------|
| 🔄 | TASK-042: Expand ReorderStrategyTests (placeholder, swap, drop) | `ReorderStrategyTests.cs` | High |
| 🔄 | TASK-043: Expand TearOutStrategyTests (capture, session, drop) | `TearOutStrategyTests.cs` | High |
| ⬜ | TASK-044: Expand TabDragCoordinatorTests (transitions, errors) | `TabDragCoordinatorTests.cs` | High |
| ⬜ | TASK-045: Add integration tests (end-to-end scenarios) | New: `TabStripDragIntegrationTests.cs` | High |
| ⬜ | TASK-046: Test event sequencing | `TabStripDragTests.cs` | Medium |
| ⬜ | TASK-047: Test multi-monitor DPI scenarios | New: `DragVisualServiceDpiTests.cs` | Medium |
| ⬜ | TASK-048: Test error recovery paths | `TabStripDragTests.cs` | Medium |
| ⬜ | TASK-049: Verify code coverage > 85% | CI/CD pipeline | High |

### Test Coverage Analysis (In Progress)

#### ReorderStrategy - 25 Required Tests

**Completed: 11/25 (44%)**

**✅ Constructor & Initialization (3/3)**

- [x] StrategyCanBeInstantiated_WithDefaultLogger
- [x] InitiateDrag_RequiresNonNullContext
- [x] InitiateDrag_ThrowsWhenAlreadyActive

**✅ Drag Initiation (2/2)**

- [x] InitiateDrag_ActivatesStrategy_WithValidContext
- [x] InitiateDrag_AppliesTransformToSourceVisualItem

**✅ Pointer Movement (1/2)**

- [x] OnDragPhysicalPositionChanged_IsIgnored_WhenStrategyNotActive
- [x] OnDragPhysicalPositionChanged_UpdatesTransform_WhenStrategyActive

**✅ Drag Completion (2/3)**

- [x] CompleteDrag_IsIgnored_WhenStrategyNotActive
- [x] CompleteDrag_HandlesDropOnSameStrip_Successfully
- [x] CompleteDrag_HandlesCrossStripDrop_ByDelegating

**✅ Edge Cases & Reusability (3/3)**

- [x] StrategyCanBeReused_AfterCompleteDrag
- [x] InitiateDrag_HandlesNullSourceVisualItem_Gracefully
- [x] MultipleStrategies_CanBeCreated_Independently

**❌ Missing Critical Tests (14)**

- [ ] InitiateDrag_StoresOriginalItemPosition_ForTransformCalculation
- [ ] InitiateDrag_CallsInsertPlaceholder_OnSourceStrip
- [ ] InitiateDrag_HandlesPlaceholderInsertionFailure_Gracefully
- [ ] OnDragPhysicalPositionChanged_ConvertsScreenToStripCoordinates
- [ ] OnDragPhysicalPositionChanged_UpdatesTransformWithHotspotOffset
- [ ] OnDragPhysicalPositionChanged_TriggersPlaceholderSwap_WhenCrossingMidpoint
- [ ] CheckAndPerformSwap_SwapsPlaceholderForward_WhenTargetIndexGreater
- [ ] CheckAndPerformSwap_SwapsPlaceholderBackward_WhenTargetIndexLess
- [ ] CheckAndPerformSwap_HandlesNoAdjacentItem_Gracefully
- [ ] CompleteDrag_CommitsReorderAtPlaceholderPosition
- [ ] CompleteDrag_RemovesTransform_AfterCommit
- [ ] CompleteDrag_RemovesPlaceholder_OnCrossStripDrop
- [ ] CompleteDrag_ResetsAllState_AfterCompletion
- [ ] CompleteDrag_HandlesAbort_WhenTargetStripIsNull

#### TearOutStrategy - 22 Required Tests

**Completed: 11/22 (50%)**

**✅ Constructor & Initialization (2/2)**

- [x] StrategyCanBeInstantiated_WithDragService
- [x] Constructor_RequiresNonNullDragService

**✅ Drag Initiation (4/4)**

- [x] InitiateDrag_RequiresNonNullContext
- [x] InitiateDrag_ThrowsWhenAlreadyActive
- [x] InitiateDrag_ActivatesStrategy_AndStartsDragSession
- [x] InitiateDrag_CreatesDescriptorWithHeaderAndTitle

**✅ Pointer Movement (2/2)**

- [x] OnDragPhysicalPositionChanged_IsIgnored_WhenStrategyNotActive
- [x] OnDragPhysicalPositionChanged_UpdatesOverlayPosition_WhenStrategyActive

**✅ Drag Completion (2/3)**

- [x] CompleteDrag_IsIgnored_WhenStrategyNotActive
- [x] CompleteDrag_HandlesDropOnTabStrip_Successfully
- [x] CompleteDrag_HandlesDropOutsideTabStrip_ByDelegating

**✅ Edge Cases & Reusability (3/3)**

- [x] StrategyCanBeReused_AfterCompleteDrag
- [x] InitiateDrag_HandlesNullSourceVisualItem_Gracefully
- [x] MultipleStrategies_CanShareSameDragService
- [x] CompleteDrag_AlwaysEndsSession_EvenOnFailure

**❌ Missing Critical Tests (11)**

- [ ] InitiateDrag_CapturesHeaderImage_WhenSourceVisualItemExists
- [ ] InitiateDrag_HandlesHeaderCaptureFailure_Gracefully
- [ ] InitiateDrag_RequestsPreviewImage_FromApplication
- [ ] InitiateDrag_UsesCorrectHotspot_FromContext
- [ ] InitiateDrag_StartsSessionWithPhysicalInitialPoint
- [ ] OnDragPhysicalPositionChanged_RequiresActiveSession
- [ ] OnDragPhysicalPositionChanged_PassesPhysicalScreenPoint_ToDragService
- [ ] CompleteDrag_EndsSession_BeforeResetingState
- [ ] CompleteDrag_ReturnsTrue_WhenDroppedOnTabStrip
- [ ] CompleteDrag_ReturnsFalse_WhenDroppedOutside
- [ ] CompleteDrag_ClearsAllState_AfterCompletion

### Phase 9: Performance Optimization

**Goal**: 60Hz update rate, responsive on low-end hardware

| Done | Task | File(s) | Priority |
|------|------|---------|----------|
| ⬜ | TASK-050: Profile OnPollingTimerTick (target < 10ms) | `TabDragCoordinator.cs` | High |
| ⬜ | TASK-051: Optimize GetHitTestTabStrip with bounds caching | `TabDragCoordinator.cs` | Medium |
| ⬜ | TASK-052: Optimize CaptureHeaderImage (clip to visible) | `TearOutStrategy.cs` | Medium |
| ⬜ | TASK-053: Profile UpdatePosition (target < 5ms) | `DragVisualService.cs` | Medium |
| ⬜ | TASK-054: Add performance metrics logging | `TabDragCoordinator.Logs.cs` | Low |
| ⬜ | TASK-055: Test on low-end hardware (4-core CPU, integrated GPU) | Manual testing | High |

### Phase 10: Documentation

**Goal**: Complete XML docs, developer guide, spec updates

| Done | Task | File(s) | Priority |
|------|------|---------|----------|
| ⬜ | TASK-056: Complete XML docs for all public APIs | All public types | High |
| ⬜ | TASK-057: Add code examples to XML comments | `TabStrip.events.cs`, coordinator | Medium |
| ⬜ | TASK-058: Update Drag.md with implementation notes | `Drag.md` | Medium |
| ⬜ | TASK-059: Create developer guide | New: `docs/tabstrip-drag-drop.md` | Medium |
| ⬜ | TASK-060: Review API surface for consistency | All public APIs | Low |

### Phase 11: Accessibility & UI Testing

**Goal**: WCAG 2.1 AA compliance, screen reader support

| Done | Task | File(s) | Priority |
|------|------|---------|----------|
| ⬜ | TASK-061: Add UI automation tests (WinAppDriver) | New: UI test project | Medium |
| ⬜ | TASK-062: Test screen reader announcements (Narrator, NVDA) | Manual testing | High |
| ⬜ | TASK-063: Verify keyboard accessibility (Space/Enter/Esc) | Manual testing | High |
| ⬜ | TASK-064: Test high contrast themes | Manual testing | Medium |
| ⬜ | TASK-065: Test touch input | Manual testing | Low |
| ⬜ | TASK-066: Set AutomationProperties on TabStripItem | `TabStripItem.xaml` | Medium |

### Phase 12: Lifecycle Integration

**Goal**: Handle suspend/resume, window close during drag

| Done | Task | File(s) | Priority |
|------|------|---------|----------|
| ⬜ | TASK-067: Handle app suspend during drag (abort, clean up) | `TabDragCoordinator.cs` | High |
| ⬜ | TASK-068: Verify overlay survives source window close | `DragVisualService.cs` | High |
| ⬜ | TASK-069: Handle destination window close (abort gracefully) | `TabDragCoordinator.cs` | Medium |
| ⬜ | TASK-070: Implement coordinator disposal | `TabDragCoordinator.cs` | Medium |
| ⬜ | TASK-071: Test window close scenarios | Integration tests | Medium |

## 3. Critical Path

**Phase 1 → Phase 2 → Phase 3** (visual experience + events)
**Phase 4** (re-entry, can start after Phase 3)
**Phase 5 → Phase 6** (abort + error handling)
**Phase 7-12** (can run in parallel after Phase 6)

## 4. Risks & Mitigation

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| RenderTargetBitmap fails on virtualized content | High | Medium | Ensure item loaded/visible; fallback visual |
| Win32 rendering perf issues on low-end HW | Medium | Low | Profile early; frame skip if needed |
| Cross-window hit-test inaccurate (multi-DPI) | High | Medium | Comprehensive DPI testing; GetDpiForPhysicalPoint |
| Pointer capture lost during cross-window | Medium | High | Polling fallback (already implemented) |
| App handler exceptions orphan items | High | Low | Document contract; recovery guidance |
| Memory leaks from drag sessions | Medium | Low | WeakReferences; disposal testing |

## 5. Dependencies

### Internal

- DroidNet.Controls.Services (IDragVisualService, DragVisualDescriptor)
- DroidNet.Collections (DispatcherCollectionProxy)
- DroidNet.Converters (NullToVisibilityConverter)

### External

- CommunityToolkit.WinUI v8.1.0+
- Microsoft.Extensions.Logging v9.0.0+
- CommunityToolkit.Mvvm v8.3.0+
- Windows SDK 10.0.19041.0+ (Win32 APIs)
- MSTest v3.6.0+ (testing)

## 6. Definition of Done

- [ ] All 71 tasks completed and verified
- [ ] Zero TODO/FIXME/Phase comments in code
- [ ] All events in spec implemented and tested
- [ ] Test coverage ≥ 85%
- [ ] No crashes on error scenarios
- [ ] 60Hz maintained on target hardware
- [ ] WCAG 2.1 AA compliant
- [ ] Code review approved
- [ ] Documentation complete
- [ ] Spec updated with implementation notes

## 7. Estimated Effort

- **Phase 1-3** (Critical): 5-7 days
- **Phase 4** (Re-entry): 3-4 days
- **Phase 5-6** (Abort/Errors): 2-3 days
- **Phase 7** (Visual): 1-2 days
- **Phase 8** (Testing): 4-5 days
- **Phase 9** (Performance): 2-3 days
- **Phase 10-12** (Docs/A11y/Lifecycle): 3-4 days

**Total: ~20-28 days** (4-6 weeks with reviews/iterations)

## 8. References

- [Drag.md](./Drag.md) - Complete specification
- [Layout.md](./Layout.md) - Layout system reference
- [WinUI 3 Drag and Drop](https://learn.microsoft.com/en-us/windows/apps/design/input/drag-and-drop)
- [Win32 Layered Windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows)
- [DPI Awareness](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/high-dpi-improvements-winui3)

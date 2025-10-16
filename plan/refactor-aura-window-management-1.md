---
goal: Refactor Aura Window Lifecycle and Theme Synchronization
version: 1.0
date_created: 2025-10-16
owner: Aura Module Maintainers
status: Planned
tags: [refactor, winui, architecture]
---

# Introduction

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

Refactor the Aura window management stack to harden theme synchronization, enforce immutable window context state, prevent dispatcher deadlocks, and eliminate window lifecycle leaks identified during the recent design review.

## 1. Requirements & Constraints

- **REQ-001**: WindowManagerService must reapply theme changes to every tracked `WindowContext.Window` within 100ms of `AppearanceSettingsService.AppThemeMode` updates.
- **REQ-002**: `WindowContext` instances must remain immutable after creation; activation metadata updates must use new record copies only.
- **REQ-003**: All window classes subscribing to `AppearanceSettingsService.PropertyChanged` must unsubscribe during `Window.Closed`.
- **REQ-004**: Provide configurable window decoration presets for each Aura window category (Primary, Document, Tool, Secondary) exposed via strongly typed helpers.
- **REQ-005**: Allow consumers to disable Aura chrome or supply custom menu definitions per window without editing Aura XAML.
- **SEC-001**: Preserve existing security posture; no new privileges, IPC channels, or unmanaged code usage are permitted.
- **PER-001**: Introduced changes must not add more than 2ms average overhead to window activation on reference hardware (Surface Laptop 5, i7, 16GB).
- **CON-001**: Maintain .NET 9, WinUI 3, and C#13 preview compatibility; no additional third-party packages allowed.
- **CON-002**: Do not modify public API signatures in `IWindowManagerService` or `IWindowFactory`.
- **GUD-001**: Follow existing logging conventions (`ILogger<T>`) with structured messages.
- **PAT-001**: Use disposable pattern and relay commands consistent with CommunityToolkit.Mvvm usage.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Implement centralized theme change propagation and align sample windows with production behavior.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Update `src/WindowManagement/WindowManagerService.cs` to subscribe to `AppearanceSettingsService.PropertyChanged` once at construction, call new private `ApplyTheme(WindowContext context)` helper that reuses `IAppThemeModeService` on dispatcher, and reapply theme on change for every tracked window. | ✅ | 2025-10-16 |
| TASK-002 | Modify `src/WindowManagement/WindowManagerService.cs` to expose internal iteration-safe snapshot method for theme updates without holding live dictionary enumerator during dispatcher work. | ✅ | 2025-10-16 |
| TASK-003 | Extend `samples/MultiWindow/ToolWindow.xaml.cs` and `samples/MultiWindow/DocumentWindow.xaml.cs` to subscribe to the manager-driven theme updates via injected `IAppThemeModeService` by registering through DI using `AddWindow<T>` and removing direct property change listeners. | ✅ | 2025-10-16 |

### Implementation Phase 2

- GOAL-002: Enforce `WindowContext` immutability and stabilize activation bookkeeping.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-004 | Refactor `src/WindowManagement/WindowContext.cs` to replace settable properties with `init` accessors, migrate activation timestamps into primary record parameters, and update `WithActivationState` to return a new instance using primary constructor arguments. |  |  |
| TASK-005 | Adjust `src/WindowManagement/WindowManagerService.cs` activation/deactivation logic to rely on immutable contexts, eliminating direct property mutation and ensuring `TryUpdate` compares previous immutable instances. |  |  |
| TASK-006 | Add unit test in `src/WindowManagement/WindowManagerServiceTests.cs` (create if missing) verifying that calling `ActivateWindow` twice yields distinct context instances with updated `LastActivatedAt`. |  |  |

### Implementation Phase 3

- GOAL-003: Resolve dispatcher completion and mass-close ordering issues and eliminate window leaks.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-007 | Update `src/WindowManagement/DispatcherQueueExtensions.cs` to instantiate `TaskCompletionSource` with `TaskCreationOptions.RunContinuationsAsynchronously` and add overload accepting `CancellationToken` for both `Action` and `Func<T>` signatures. |  |  |
| TASK-008 | Modify `src/WindowManagement/WindowManagerService.cs` `CloseAllWindowsAsync` to capture a snapshot of window IDs before issuing close requests, ensuring deterministic closure even as collection mutates. |  |  |
| TASK-009 | Update `src/MainWindow.xaml.cs` and `samples/MultiWindow/MainWindow.xaml.cs` to unsubscribe from `AppearanceSettingsService.PropertyChanged` within `OnClosed` overrides prior to disposing managed resources. |  |  |

### Implementation Phase 4

- GOAL-004: Establish configurable Aura decoration presets and metadata-driven customization flow.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-010 | Introduce `IWindowDecorationOptions` contract in `src/WindowManagement` and implement helper factory methods (`WindowDecorationPresets.CreatePrimary()`, `CreateDocument()`, etc.) returning immutable option sets with title bar template identifiers, button layouts, and menu providers. |  |  |
| TASK-011 | Extend `WindowContext.Create` overloads to accept typed decoration options (or resolve via `IWindowDecorationRegistry` using `windowType`) and persist selection for downstream renderers; replace ad-hoc metadata usage with the selected option strategy. |  |  |
| TASK-012 | Refactor `src/MainShellView.xaml(.cs)` to bind to `IWindowDecorationOptions`, support menu opt-out by collapsing menu regions when `MenuSource` is null, and allow consumer-supplied icon/command sets; update documentation in `projects/Aura/README.md` and `MULTI_WINDOW_IMPLEMENTATION.md`. |  |  |
| TASK-013 | Update sample windows to request presets via the window manager (e.g., tool windows use slim preset) and add scenario demonstrating opt-out of Aura chrome for modal/secondary windows. |  |  |

## 3. Alternatives

- **ALT-001**: Introduce a dedicated theme broadcaster service. Rejected because existing window manager already has required context and communication paths, and adding another service increases complexity.
- **ALT-002**: Replace Rx-based dispatcher utilities with `DispatcherQueueScheduler`. Rejected to avoid additional package dependencies and maintain current helper signature usage.
- **ALT-003**: Keep decoration configuration as loose metadata (`IDictionary<string, object>`) attached to `WindowContext`. Rejected because it is error-prone, lacks discoverability, and cannot guarantee schema validation across window categories.
- **ALT-004 (Selected)**: Define `IWindowDecorationOptions` plus preset factory helpers and optional `IWindowDecorationRegistry` for lookup by window type. Chosen to provide strong typing, predictable defaults, and an intuitive opt-in/out workflow aligned with DI patterns.

## 4. Dependencies

- **DEP-001**: `Microsoft.UI.Xaml` dispatcher API for theme application must remain available in Windows App SDK 1.8.
- **DEP-002**: Existing logging and Rx packages must compile against C#13 preview with new immutable record adjustments.

## 5. Files

- **FILE-001**: `src/WindowManagement/WindowManagerService.cs`
- **FILE-002**: `src/WindowManagement/WindowContext.cs`
- **FILE-003**: `src/WindowManagement/DispatcherQueueExtensions.cs`
- **FILE-004**: `src/MainWindow.xaml.cs`
- **FILE-005**: `samples/MultiWindow/MainWindow.xaml.cs`
- **FILE-006**: `samples/MultiWindow/ToolWindow.xaml.cs`
- **FILE-007**: `samples/MultiWindow/DocumentWindow.xaml.cs`
- **FILE-008**: `src/WindowManagement/IWindowDecorationOptions.cs` (new file)
- **FILE-009**: `src/WindowManagement/WindowDecorationPresets.cs` (new file)
- **FILE-010**: `src/WindowManagement/IWindowDecorationRegistry.cs` (new file)
- **FILE-011**: `tests/Aura.WindowManagement/WindowManagerServiceTests.cs` (new test file)
- **FILE-012**: `projects/Aura/README.md`
- **FILE-013**: `projects/Aura/MULTI_WINDOW_IMPLEMENTATION.md`

## 6. Testing

- **TEST-001**: Add unit tests verifying theme reapplication across all registered windows after simulated `AppThemeMode` change using mocked `IAppThemeModeService` (`tests/Aura.WindowManagement/WindowManagerServiceTests.cs`).
- **TEST-002**: Add integration test in `samples/MultiWindow` automated UI harness (if available) or manual test script validating that closing all windows with concurrent creation completes without exceptions and leaves no open windows.
- **TEST-003**: Add memory leak detection step using `WinUITestRunner` or equivalent to ensure windows unsubscribe from appearance events upon closure.
- **TEST-004**: Add unit test suite for `WindowDecorationPresets` verifying opt-out semantics, custom menu injection, and correct preset retrieval by window type.
- **TEST-005**: Extend sample UI test/manual checklist ensuring tool/document windows reflect selected presets and that disabling menus collapses Aura chrome while keeping default WinUI system controls accessible.

## 7. Risks & Assumptions

- **RISK-001**: Dispatcher queue saturation could delay theme propagation beyond 100ms target; mitigation: throttle updates and log warning when delay exceeds threshold.
- **RISK-002**: Immutable `WindowContext` may require wider refactors if other components rely on mutable state; assumption is those consumers use provided APIs only.
- **ASSUMPTION-001**: Tests project `tests/Aura.WindowManagement` either exists or can be created without impacting CI pipeline configuration.

## 8. Related Specifications / Further Reading

- [MULTI_WINDOW_IMPLEMENTATION.md](../projects/Aura/MULTI_WINDOW_IMPLEMENTATION.md)
- [Aura README](../projects/Aura/README.md)

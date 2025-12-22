# Editor Input Handling and Camera Control Design

## 1. Introduction & Philosophy

This document specifies the design for a **Unified Input Pipeline** in the Oxygen Editor. A "best-in-class" engine architecture does not treat editor input and game input as two separate, disconnected systems. Instead, it uses a single, robust pipeline that is **context-aware** and **priority-driven**.

### 1.1 The Unified Pipeline Philosophy

Our design is based on four core principles:

1. **Thread-Safe Command Relay**: Input events from the WinUI thread are treated as asynchronous commands to the engine. This ensures that input is processed at the correct point in the engine's frame lifecycle, avoiding race conditions.
2. **Contextual Routing**: Every input event is tagged with a `ViewId`. The engine uses this to determine which viewport, camera, or gizmo should receive the input.
3. **Priority-Based Consumption**: Input flows through a stack of handlers. High-priority editor tools (like Gizmos) can "consume" an event, preventing it from reaching lower-priority systems (like game logic).
4. **Stateful Cursor Management**: The system handles cursor visibility and confinement during continuous operations like orbiting or dragging.

### 1.2 Why a Unified System?

By routing editor input through the engine's `InputSystem` rather than bypassing it, we gain several professional-grade features:

* **Chorded Actions**: Easily handle complex combinations like `Alt + Shift + LMB` without manual state tracking.
* **Rebindable Hotkeys**: Users can customize editor shortcuts (e.g., changing the "Translate" tool from `W` to something else) using the same mapping system as the game.
* **Consistent State**: The engine maintains a single source of truth for key/button states (e.g., "Is the Ctrl key currently pressed?").

> **Note**: Undo/Redo transaction management is handled by the editor-level system (out of scope for this document). The input pipeline enables this by providing clear action lifecycle events (Started/Completed/Canceled).

---

## 2. Architecture Overview

The input flow follows a clear path from the UI to the Engine:

1. **WinUI Layer (`Viewport.xaml.cs`)**: Captures raw pointer and keyboard events from the `SwapChainPanel`.
2. **Interop Layer (`EngineRunner`)**: Packages the event into a `platform::InputEvent` and sends it to the `EditorModule` via a thread-safe command queue.
3. **Editor Module (`EditorModule.cpp`)**: Drains the command queue during the `OnFrameStart` phase and injects events into the global **Platform channel**.
4. **Input System**: The global `InputSystem` processes the events against the currently active **Input Mapping Contexts (IMC)**. The `EditorModule` manages which contexts are active based on the current mode (Editor vs. Game).
5. **Action Execution**: The `EditorModule` (or the Game) reacts to the triggered actions (e.g., `Editor.Camera.Orbit` or `Game.Jump`).

---

## 3. Input Mapping Contexts (IMC)

We use the engine's `InputMappingContext` system to manage input priority. The stack is dynamic based on the current tool state:

| Context | Priority | Description |
| :--- | :--- | :--- |
| **IMC_Editor_Gizmos** | 100 (Highest) | Active when a gizmo handle is hovered or captured. Consumes all mouse input during a drag. |
| **IMC_Editor_Viewport** | 50 | Contains mappings for `Orbit`, `Pan`, `Zoom`, and `Fly`. Active when a viewport is focused or hovered (for scroll). |
| **IMC_Editor_Tools** | 20 | Viewport-specific tools (`W`, `E`, `R` for Gizmo modes, `F` to Focus). |
| **IMC_Game** | 0 (Lowest) | Standard game input. Only active during Play-In-Editor (PIE). |

> **Note on Focus vs. Hover**:
>
> * **Hover**: Sufficient for `Zoom` (Scroll Wheel) and `Gizmo Pre-highlighting`. Scroll events are delivered to the **last-hovered viewport** (UE5 convention).
> * **Focus**: Required for `Keyboard` shortcuts and `Orbit/Pan` operations. Clicking a viewport should grant it focus.
> * **Multi-Viewport Scroll**: WinUI tracks which viewport last received `PointerEntered`. Scroll events are tagged with that viewport's `ViewId`.

---

## 4. Editor Input Bindings

Standard 3D editor controls based on UE5/Maya/Blender conventions:

### 4.1 Viewport Navigation

| Action | Input | Description |
| :------- | :------ | :------------ |
| **Orbit** | `Alt + LMB` drag | Rotate camera around pivot point (tumble) |
| **Pan** | `Alt + MMB` drag | Move camera parallel to view plane (track) |
| **Zoom** | `Mouse Wheel` | Dolly camera toward/away from pivot |
| **Fly Mode** | `RMB` hold + mouse look + `WASD` (`Q/E` up/down, `Shift` fast) | Free camera movement (FPS-style). `Alt + RMB` is reserved for dolly. |
| **Zoom Alt** | `Alt + RMB` drag | Alternative zoom via mouse drag |

### 4.2 Selection

| Action | Input | Description |
| :------- | :------ | :------------ |
| **Select** | `LMB` click | Replace selection with clicked object |
| **Add to Selection** | `Shift + LMB` | Add clicked object to current selection |
| **Toggle Selection** | `Ctrl + LMB` | Toggle clicked object in/out of selection |
| **Box Select** | `LMB` drag on empty space | Rectangular selection (future) |
| **Deselect All** | `Escape` or click empty | Clear selection |

### 4.3 Gizmo Modes

| Action | Input | Description |
| :------- | :------ | :------------ |
| **Translate Mode** | `W` | Switch to move gizmo (3-axis arrows) |
| **Rotate Mode** | `E` | Switch to rotation gizmo (3-axis circles) |
| **Scale Mode** | `R` | Switch to scale gizmo (3-axis handles) |
| **Cycle Modes** | `Space` | Cycle through translate/rotate/scale |
| **Toggle Local/World** | `Q` | Switch between local and world space gizmos |

### 4.4 Gizmo Interaction

| Action | Input | Description |
| :------- | :------ | :------------ |
| **Drag Axis** | `LMB` drag on axis handle | Constrained movement along single axis |
| **Drag Plane** | `LMB` drag on plane handle | Constrained movement in plane (2 axes) |
| **Free Drag** | `LMB` drag on center | Unconstrained screen-space movement |
| **Precision Drag** | `Shift` (hold during drag) | Slow down drag speed (0.1x multiplier) |
| **Snap Toggle** | `Ctrl` (hold during drag) | Enable/disable grid/angle snapping |

### 4.5 Viewport Utilities

| Action | Input | Description |
| :------- | :------ | :------------ |
| **Focus Selection** | `F` | Frame selected object(s) in viewport |
| **Reset Camera** | `Home` | Reset to default camera position/rotation |
| **Toggle Wireframe** | `Alt + W` | Switch between shaded/wireframe |
| **Toggle Grid** | `G` | Show/hide grid overlay |

### 4.6 Modifier Key Semantics

* **Shift**: Additive (add to selection, precision mode)
* **Ctrl**: Toggle (toggle selection, toggle snap)
* **Alt**: Camera manipulation (orbit/pan/zoom prefix)

**Design Notes**:

* Bindings are **configurable** via Input Mapping Contexts.
* Default bindings match UE5 conventions (most widely used).
* Maya users can remap to Maya-style (`Alt+Shift+LMB` for pan).
* All bindings route through `InputSystem` - no hardcoded checks in UI layer.

---

## 5. Technical Implementation Details

### 5.1 Direct Input Injection

In a standalone game, the `Platform` layer pulls input directly from the OS (via SDL3). In hosted/editor scenarios, the `Platform` layer accepts input provided by the host application (WinUI).

The `InputEvents` component automatically adjusts its behavior based on `PlatformConfig.headless`: when `headless` is false it continues to pull events from the `EventPump` (standalone mode); when `headless` is true (hosted/embedded) it operates in an injected-mode and accepts events supplied by the host without subscribing to the `EventPump`.

#### 5.1.1 Configurable Input Source

The `InputEvents` component in `Oxygen.Engine` will support both the default pull-from-`EventPump` behavior used in standalone games and an injected-mode suitable for hosted/editor scenarios. Selection between these internal behaviors is **automatic** and driven by `PlatformConfig.headless`: when `headless` is false, `InputEvents` pulls from the `EventPump`; when `headless` is true (hosted/embedded), `InputEvents` allows external producers to send events via its `ForWrite()` interface and does not subscribe to the `EventPump`. This is an internal implementation detail and does not introduce a public mode toggle.

#### 5.1.2 Editor-Side Injection

1. **Configuration**: In host scenarios, the host (for example, the Editor's interop layer) delivers input to the engine. `InputEvents` will automatically operate in injected-mode when `PlatformConfig.headless` is true; hosts should obtain a writer via `InputEvents::ForWrite()` to deliver events instead of relying on the `EventPump`.
2. **Event Bridge**:
    * WinUI events are captured by `Viewport.xaml.cs`. **Critical**: Each `Viewport` control has an assigned `ViewId`.
    * Events are passed to `EditorModule` via `EngineRunner` along with the `ViewId` of the source viewport.
    * `EditorModule` translates them into `InputEvent` objects, **embedding the `ViewId`** in each event.
    * `EditorModule` sends events to the engine via the `InputEvents::ForWrite()` writer.
3. **Seamless Consumption**: The engine's `InputSystem` remains completely unaware of this swap. It continues to read from the `InputEvents` channel, receiving events that originated in WinUI. The `ViewId` tag allows systems like `EditorCameraController` to route input to the correct viewport camera.

#### 5.1.3 Handling High-Frequency Input (Throttling)

Since the host bypasses the OS event queue, the host (e.g., the Editor's interop layer) becomes responsible for flow control.

* **Accumulation**: The host maintains a thread-safe "Input Accumulator" that receives raw events from the WinUI thread. In automated tests and CI, prefer using a direct-injection test harness rather than the full EditorModule to exercise this logic.
* **Coalescing**:
  * **Mouse Moves**: Continuous mouse deltas are summed into a single "Frame Delta".
  * **Scroll Wheel**: Deltas are summed.
  * **Keys/Buttons**: All state changes are preserved in order.
* **Injection**: Once per frame (during the host's frame-start or dispatch phase), the host drains the accumulator, generates optimized `InputEvent`s (e.g., one MouseMove event representing the sum of 100 raw moves), and sends them to the engine via the `InputEvents::ForWrite()` writer.
* **Benefit**: This prevents channel flooding and ensures the engine processes exactly one logical input update per simulation step, matching the frame rate.

### 5.2 Context-Based Routing

Instead of maintaining a separate `InputSystem` for the editor, we leverage the engine's native **Input Mapping Context (IMC)** system to handle mode switching. This ensures a unified architecture where the Editor is just another "player" with high-priority input needs.

1. **Unified Input Stream**: All events (from WinUI or SDL) flow into the single, global `InputSystem`.
2. **Mode Switching**: The `EditorModule` is responsible for activating and deactivating IMCs based on the current engine state.

| Mode | Active Contexts | Behavior |
| :--- | :--- | :--- |
| **Editing** | `IMC_Editor_Gizmos`, `IMC_Editor_Viewport`, `IMC_Editor_Global` | Game contexts are **Deactivated**. The game receives no input. |
| **Play-In-Editor (PIE)** | `IMC_Game`, `IMC_Editor_Global` (limited) | Editor viewport navigation is **Deactivated**. Game logic receives full input. |

#### 5.2.1 Advantages

* **Simplicity**: Single code path for input processing.
* **Consistency**: Editor controls use the exact same Action/Axis system as the game.
* **Configurability**: Users can rebind editor keys using the same UI used for game controls.

#### 5.2.2 Priority and Consumption

We use the standard **IMC priority system** to manage conflicts:

1. **Gizmo Priority**: `IMC_Editor_Gizmos` has the highest priority. If a user clicks a Gizmo handle, the action "consumes" the input.
   * *Cursor Hiding*: When a Gizmo action starts (e.g., dragging an axis), the cursor should be hidden or locked to the axis to prevent it from leaving the viewport.
2. **Navigation Fallback**: If no gizmo or tool consumes the input, it falls through to `IMC_Editor_Viewport` (Orbit/Pan/Zoom).
3. **Clean State**: When switching modes, we simply call `ActivateMappingContext` / `DeactivateMappingContext` on the relevant contexts.

### 5.3 Camera Control & View Matrix Updates

Viewport navigation is implemented as a **feature compositor** in the interop/editor layer.

1. **Per-view state**: Each `EditorView` owns a camera `SceneNode` and a mutable `focus_point` (pivot) used by orbit/dolly/zoom and updated by pan.
1. **Input consumption**: `EditorViewportNavigation` registers and activates `IMC_Editor_Viewport`, then each frame calls `feature->Apply(...)` for each navigation feature (Orbit/Pan/Dolly/WheelZoom/Fly) using the per-frame `InputSnapshot`.
1. **Transform updates (critical constraint)**: Navigation runs during scene mutation, so it must avoid APIs that require up-to-date world transforms. The implementation updates the camera using **local-only** operations (`SetLocalPosition`, `SetLocalRotation`) and computes look rotations locally (no `Transform::LookAt()` / no world queries).
1. **Renderer synchronization**: The renderer computes final world transforms and view/projection data later in the frame. Because navigation only marks local transforms dirty, it integrates cleanly with the renderer's single authoritative world-transform update pass.

### 5.4 Selection via GPU Picking

Selection is triggered by the `Editor.Select` action (usually bound to `LMB`) within the editor input context.

**Implementation** (Detailed in `editor-artifacts-rendering.md` §5.3):

1. **Pick Request Generation**: On `LMB` press, `EditorModule` generates a **Click Pick Request**:
   * Tagged with `request_id`, `timestamp`, `view_id`, and cursor screen position.
   * Queued for rendering in next frame's pick pass.

2. **GPU Pick Pass** (Frame N+1):
   * Render MainScene + GizmoScene with ID shader to 1×1 `R32_UINT` texture at cursor position.
   * Separate passes for opaque (depth write on) and transparent (depth write off) geometry.
   * Readback ID value from persistent CPU-readable texture.

3. **Pick Result Delivery** (Frame N+2):
   * Result delivered to `EditorModule` with picked `EntityId` (0 = miss).
   * Timeout validation applied (see §6.3).
   * If valid, update `SelectionManager`.

4. **Selection Update**:
   * `SelectionManager` updates internal state based on modifier keys:
     * Query current key states from `InputSystem::GetKeyState()`:
       * `Shift` pressed: Add to selection (union).
       * `Ctrl` pressed: Toggle selection (XOR).
       * Neither: Replace selection (clear + add).
     * **Rationale**: Modifier logic belongs in selection handler, not action mapping. Allows runtime behavior changes without remapping.
   * Triggers stencil buffer update for outline rendering.
   * Notifies Properties panel to display selected object properties.

**Advantages**:

* Handles gizmos, scene objects, and transparent geometry uniformly.
* Predictable 1-frame latency (imperceptible in editor).
* No CPU-side raycasting needed.

---

## 6. Advanced Interaction Details

### 6.0 Gizmo Hover Detection

**Purpose**: Determine which gizmo axis is under the cursor before the user clicks, enabling pre-highlighting and conditional IMC activation.

**Implementation** (Leverages existing GPU picking infrastructure from rendering design):

1. **Hover Query Generation**:
   * Every frame during `EditorModule::OnPreRender` phase, for each active viewport:
     * If cursor is over viewport, generate a **Hover Pick Request**.
     * Request uses current cursor position, renders to 1×1 ID texture.
   * Hover picks use a **rolling 2-frame buffer**: Frame N renders hover pick, Frame N+1 reads result.
   * **Phase Rationale**: Pick requests must be generated before rendering begins, not during input processing.

2. **Hover State Management**:
   * `GizmoManager` maintains:
     * `EntityId hovered_gizmo_part_` — ID of currently hovered gizmo axis/handle (0 = none).
     * `EntityId hovered_scene_object_` — ID of currently hovered scene object (0 = none).
   * **Priority**: Gizmo IDs take priority over scene object IDs (checked first in pick result).

3. **IMC Consumption Logic**:
   * `IMC_Editor_Gizmos` is **always active** (registered at editor startup).
   * Gizmo actions check hover state before consuming input:
     * `Editor.Gizmo.BeginDrag`: Consumes LMB only if `hovered_gizmo_part_ != 0`.
     * `Editor.Gizmo.ContinueDrag`: Consumes mouse delta if drag is in progress.

4. **Performance**:
   * Hover picks reuse the same GPU picking pass infrastructure as click-based selection (see `editor-artifacts-rendering.md` §5.3).
   * Cost: ~1 draw call per frame (MainScene + GizmoScene with ID shader, 1×1 viewport). Negligible overhead.

**Note**: Hover detection does NOT require a separate picking system. The existing GPU-based unified picking design (§5.3 in rendering doc) handles both hover and click seamlessly.

### 6.1 Cursor Policy

To provide a professional feel, the cursor behavior must change based on the active operation:

* **Default**: Standard arrow pointer.
* **Hovering Gizmo**: Context-specific icon (e.g., "Move X", "Rotate").
* **Orbiting/Panning**:
  * **Hide**: The cursor is hidden during the operation.
  * **Restore**: On release, the cursor reappears at the original position.
  * **Note**: Cursor position is locked to the start point. Delta accumulation allows unlimited rotation without cursor escaping viewport.

### 6.2 Mouse Constraint vs. Snapping (Separation of Concerns)

**Mouse Constraint** (WinUI Responsibility):

* **Purpose**: Constrain cursor movement to a specific screen-space direction during gizmo drag (e.g., lock cursor to vertical line when dragging Y-axis handle).
* **Implementation** (Per-Request Callback Pattern):
  * **Step 1 - User Clicks**: User presses LMB. WinUI sends pick request with callback:

    ```csharp
    EngineRunner.RequestPick(viewId, cursorPos, PickType.Click, result => {
        if (result.entity_id != 0 && IsGizmoPart(result.entity_id)) {
            // Store constraint from pick result
            active_constraint = result.screen_axis_direction;
        }
    });
    ```

  * **Step 2 - Engine Processes**: Engine does GPU pick (1-2 frames later), constructs `PickResult`:
    * `EntityId entity_id` — picked object/gizmo part (0 = miss).
    * `Vector2 screen_axis_direction` — if gizmo part: axis already projected to screen space.
    * Engine invokes callback with result.
  * **Step 3 - Callback Stores Constraint**: Callback receives result, stores `active_constraint` if gizmo.
  * **Step 4 - Constraint Application** (`PointerMoved` handler):
    * If `active_constraint` exists:
      * `constrained_delta = dot(delta, active_constraint) * active_constraint`.
      * Send constrained delta to accumulator.
    * Else: Send raw delta.
  * **Step 5 - Constraint Release**: On LMB release, clear `active_constraint`.
* **Benefit**: User feels direct manipulation along the axis.
* **Note**: Matches `CreateScene`/`CreateViewAsync` pattern — per-request callback, not global registration.

**Transform Snapping** (Engine Responsibility):

* **Purpose**: Quantize object transforms to grid increments or angle steps.
* **Implementation** (Engine `GizmoManager`):
  * `EditorSettings` defines:
    * `grid_snap_enabled` (bool), `grid_size` (float, e.g., 0.5 units).
    * `angle_snap_enabled` (bool), `angle_step` (float, e.g., 15 degrees).
  * During gizmo drag, `GizmoManager::ApplyDrag(delta)` checks snap settings:
    * Check `InputSystem::GetKeyState(Key::Ctrl)` to determine if snap is active.
    * **Translation**: If snap active: `new_pos = round(new_pos / grid_size) * grid_size`.
    * **Rotation**: If snap active: `new_angle = round(new_angle / angle_step) * angle_step`.
    * **Scale**: If snap active: snap scale to fixed increments (e.g., 0.1x steps).
  * Snapping is applied **after** constraint, so the two systems compose cleanly.
* **Benefit**: Objects align to level geometry, rotations are clean (0°, 90°, etc.).
* **Note**: Snapping is geometric logic involving world-space transforms. WinUI has no visibility into this.

**Summary**:

* **WinUI**: Constrains cursor motion in screen space (UX polish).
* **Engine**: Snaps transforms in world space (level design tool).
* The two features are orthogonal and can be enabled independently.

### 6.3 Pick Request Timeout and Invalidation

**Purpose**: Prevent stale pick results from being applied if the engine is slow or the request is abandoned.

**Implementation**:

* Each pick request (hover or click) is tagged with:
  * `request_id` (unique per request).
  * `timestamp` (request submission time).
  * `view_id` (which viewport issued the request).
  * `request_type` (hover vs. click).
* **Timeout Threshold**: `kPickTimeoutMs = 100ms` (configurable).
* **Invalidation Logic** (in `EditorModule` pick result handler):
  * When a pick result arrives, check:
    * `if (current_time - request.timestamp > kPickTimeoutMs)`: Discard result, log warning.
    * `if (!ViewExists(request.view_id))`: Discard (viewport was closed mid-pick).
    * `if (request_type == Click && !ViewHasFocus(request.view_id))`: Discard (user clicked elsewhere, losing focus).
    * For hover picks: Always deliver if view exists (hover doesn't require focus).
    * Otherwise: Deliver result to `GizmoManager` or `SelectionManager` with `view_id` context.
* **Multi-Viewport Safety**: Each viewport maintains independent hover/selection state. Pick results are routed to the originating viewport only.
* **User Scenario**: User rapidly clicks in different viewports. Stale or cross-viewport pick results are ignored, preventing incorrect selection.

**Note**: This is a simple timeout mechanism. No complex camera-based invalidation is needed—the GPU pick design in `editor-artifacts-rendering.md` is already robust. The 1-frame latency is inherent and well-handled.

### 6.4 Batch Updates for Multi-Object Transforms

**Status**: Future enhancement. Tracked but not implemented in Phase 1.

**Rationale**: Current test scenes have ~10 objects. Premature optimization. Defer until perf profiling shows bottlenecks with large selections (>100 objects).

**Tracking Note**: When implemented, use a transaction-scoped batch update pattern:

* `EditorModule::BeginBatchUpdate()` defers scene graph updates.
* Transform changes are queued.
* `EndBatchUpdate()` applies all changes in one traversal.
* Single undo record for the batch.

---

## 7. Implementation Plan

The plan below is ordered to optimize for **quick verification** of the core pipeline:

> WinUI `Viewport` → interop/EditorModule → injected `InputEvents` → `InputSystem` → `EditorViewportNavigation` mutates per-view camera `SceneNode` transforms.

Picking/selection/gizmos remain part of the design, but are explicitly deferred to a later phase.

### Phase 1 (Fast Path): Camera navigation end-to-end

1. [X] **Refactor `InputEvents` Component**:
   * Modify `Oxygen.Engine/src/Oxygen/Platform/InputEvents.h` to support injected events.
   * Expose `ForWrite()` on `InputEvents` to allow external producers to send events.
   * Ensure `ProcessPlatformEvents` (the SDL poller) is skipped or disabled when `PlatformConfig.headless` is true so the component does not subscribe to the `EventPump` in hosted mode.
   * Make the selection of pull vs injected-mode **automatic** based on `PlatformConfig.headless` (no public mode toggle).

2. [X] **Platform testing**:
   * Add unit tests for the Platform, following OxCo testing patterns for coroutines.

3. [X] **Implement Input Accumulator**:
   * Implemented the host-side `InputAccumulator` as part of the interop/EditorModule layer to buffer and coalesce raw input before injection into the engine.
   * Coalescing logic (implemented):
     * **Mouse/Scroll**: `mouse_delta` and `scroll_delta` are summed per-viewport; the **last** `position` seen is recorded and forwarded with mouse/wheel events.
     * **Key/Button events**: Preserved in order and forwarded as-is.
   * Thread-safety: per-viewport `ViewportAccumulator` protected by a mutex and the views map guarded by `map_mutex_` (implemented via `EnsureViewport`).
   * Drain/View semantics (implemented):
     * `AccumulatedInput Drain(ViewId view)` returns an `AccumulatedInput` where `mouse_delta`, `scroll_delta`, `last_position` are copied and `key_events`/`button_events` are moved out (via `std::move`).
     * After `Drain`, `mouse_delta`, `scroll_delta`, and `last_position` are reset to defaults; key/button containers are moved so the viewport no longer owns the drained events.
   * Focus loss (implemented): `OnFocusLost(ViewId)` clears the accumulated deltas (mouse + scroll) but preserves ordered key/button events.
   * Adapter & forwarding (implemented): `InputAccumulatorAdapter::DispatchForView` forwards `mouse_delta`/`scroll_delta` together with `last_position` to an `IInputWriter`, which the `EditorModule` implements to send engine `InputEvent`s.
   * Managed bridge (implemented): `OxygenInput` pushers convert managed event structs to native `Editor*Event` and call the `Push*` methods on the accumulator.
   * Tests added (native MSTest): `projects/Oxygen.Editor.Interop/test/native/src/InputAccumulator_native_test.cpp` includes:
     * `DrainAggregatesMotionAndKeys`
     * `DrainClearsAccumulator`
     * `EventsAreScopedToView`
     * `MouseWheelAggregationAndPosition`
     * `ButtonEventsOrdering`
     * `MultipleKeyEventsOrdering`
     * `OnFocusLostClearsDeltasKeepsEvents`
     * `DrainEmptyReturnsNothing`
   * Test results: all tests pass locally (8/8) — validating the implemented behavior and adapter dispatch.

4. [X] **Implement Input Dispatcher**:
   * In the host's frame-start/dispatch (or the interop layer's per-frame hook):
     * Drain the `InputAccumulator`.
     * Convert raw data to `platform::InputEvent`.
     * Send events to the engine via the `InputEvents::ForWrite()` writer.
   * In engine modules' input phase (register for `kInput`):
     * Manage IMC activation/deactivation based on current mode.

5. [X] **Define minimal editor navigation actions + mappings** (no tools/picking/selection yet):
   * Create `IMC_Editor_Viewport` containing only the navigation-related inputs used by the compositor:
     * `Editor.Modifier.Alt` (`LeftAlt` / `RightAlt`)
     * `Editor.Mouse.LeftButton`, `Editor.Mouse.MiddleButton`, `Editor.Mouse.RightButton`
     * `Editor.Mouse.Delta` (`MouseXY`)
     * `Editor.Camera.Zoom` (`MouseWheelY`)
     * `Editor.Fly.W/A/S/D/Q/E` + `Editor.Fly.Shift`
   * Keep any game IMCs deactivated while in Editing mode.
   * Defer all bindings from §4.2–§4.5 (selection, gizmo modes, utilities) until Phase 2.

6. [X] (CANCELED) BAD TASK - **Update EngineRunner (Interop) — navigation only**:
  This task is intentionally canceled. Navigation input already flows through the existing interop input bridge (`OxygenInput` → `InputAccumulator` → `InputEvents::ForWrite()` via `EditorModule`). Adding parallel `EngineRunner::Send*` methods would duplicate the pipeline and invite divergence.

7. [ ] **Implement WinUI viewport event handlers — navigation only**:
   * In `Viewport.xaml.cs`, bind `Pointer*`, `Key*`, and focus events.
   * Implement **Focus Management**:
     * Request focus on click.
     * On `LostFocus`: call `OnFocusLost(viewId)` to clear accumulated mouse/scroll deltas for this viewport (preserve key/button events).
   * Implement **Hover Tracking**: maintain `last_hovered_viewport_id` for scroll routing.
   * Implement **Cursor Management** for camera manipulation (orbit/pan/zoom/fly):
     * On navigation drag start (e.g., `Alt+LMB`/`Alt+MMB`/`Alt+RMB`): hide cursor and restore it on release.
   * Do **not** implement pick requests, mouse constraints, or selection callbacks in Phase 1.

8. [X] **Implement viewport navigation compositor**:
  Implemented `EditorViewportNavigation` and per-action features (`Orbit`, `Pan`, `Dolly`, `WheelZoom`, `Fly`). Navigation is applied during `EditorModule::OnSceneMutation` using `InputSnapshot` and a per-view `focus_point`, and updates the camera via local-only transforms to respect the renderer-owned world transform update pass.

### Phase 1.5 (Leftovers): Pivot and multi-viewport

* [ ] **Focus/Pivot: make `focus_point` meaningful (not default origin)**. Add an explicit "has pivot" state per `EditorView` (or equivalent) so we can distinguish between a real pivot and the default. Implement a first-pass pivot initialization rule for new views (e.g., set `focus_point` in front of the camera at a reasonable default distance, or derive it from initial scene contents once available). When selection/picking is introduced (Phase 2), update `focus_point` to the selection center and (optionally) adjust camera radius.

* [ ] **Multi-viewport: ensure navigation applies to the correct view only**. Short-term: track `active_view_id` (focused viewport) and apply navigation only to that view during `OnSceneMutation`. Long-term (correct architecture): make action evaluation window-scoped so the `InputSystem` can produce a per-view (per-window) snapshot, or provide a query API that filters action state/transitions by `WindowId`. Verify scroll/hover routing and focus rules (keyboard to focused viewport; wheel to last-hovered viewport).

### Phase 2 (Deferred): Picking, selection, gizmos, snapping

1. [ ] **Update EngineRunner (Interop) — picking API** (deferred):
   * Add **Pick Request API** (matches `CreateScene`/`CreateViewAsync` pattern):

     ```cpp
     void RequestPick(
         ViewId viewId,
         Vector2 screenPos,
         PickType type,
         std::function<void(PickResult)> callback
     );
     ```

   * **PickResult Structure**:
     * `EntityId entity_id` — picked entity (0 = miss).
     * `ViewId view_id` — originating viewport.
     * `Vector2 screen_axis_direction` — if gizmo part: axis projected to screen space (normalized).
     * `bool is_gizmo` — true if entity is gizmo part.

2. [ ] **Implement GPU-Based Picking** (deferred; designed in `editor-artifacts-rendering.md` §5.3):
    * `RequestPick` enqueues `PickCommand` with stored callback.
    * Execute during `OnPreRender`, read back 1–2 frames later.
    * Apply timeout/invalidation rules from §6.3.

3. [ ] **Define remaining input actions and mappings** (deferred):
    * Add bindings from §4.2–§4.5 (selection, gizmo modes, viewport utilities).
    * Add `IMC_Editor_Gizmos` and `IMC_Editor_Tools` on top of navigation.

4. [ ] **Implement GizmoManager** (deferred):
    * Register `IMC_Editor_Gizmos` at startup.
    * Add hover detection (via GPU picking) + drag consumption.
    * Add transform snapping (engine responsibility) and optional mouse constraint (WinUI responsibility) as described in §6.2.

## 8 Deliberate Omissions (No Bloat)

Features NOT included (deferred or unnecessary for Phase 1):

* **Box Selection**: Marked as future (§4.2). Rarely used in 3D editors (more common in 2D).
* **Multi-Object Batch Updates**: Deferred until profiling shows bottlenecks (§6.4). Current test scenes have ~10 objects.
* **Complex Snapping Modes**: No vertex snapping, surface snapping, or alignment tools. Grid/angle snapping covers 90% of use cases.
* **Gesture Support**: No touch/pen gestures. Desktop-first.
* **Macro Recording**: No input recording/playback. Out of scope.

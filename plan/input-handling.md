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
| **Fly Mode** | `RMB` hold + `WASD` | Free camera movement (FPS-style) |
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

The `EditorCameraController` processes `Editor.Camera.*` actions during the engine's `kInput` phase. It retrieves action values from the global `InputSystem` snapshot.

1. **State Tracking**: The controller maintains the camera's state (Position, Rotation, Pivot, Zoom Level) for each `ViewId`.
2. **Matrix Calculation**: During `OnInput`, the controller calculates the new **View Matrix** based on the accumulated deltas from the local `InputSystem`'s actions.
3. **ViewContext Update**: The updated matrix is pushed to the `ViewContext` associated with the `ViewId`.

   ```cpp
   void EditorCameraController::OnInput(ViewId viewId, const InputSnapshot& snapshot) {
       auto& cam = cameras_[viewId];

       // Accumulate deltas from global InputSystem actions (only active if IMC_Editor_Viewport is active)
       Axis2D orbitDelta = snapshot.GetActionValue<Axis2D>("Editor.Camera.Orbit");
       Axis2D panDelta = snapshot.GetActionValue<Axis2D>("Editor.Camera.Pan");
       Axis1D zoomDelta = snapshot.GetActionValue<Axis1D>("Editor.Camera.Zoom");

       // Update camera state
       cam.Orbit(orbitDelta);
       cam.Pan(panDelta);
       cam.Zoom(zoomDelta);

       // Sync with ViewContext
       auto viewContext = viewManager_->GetViewContext(viewId);
       viewContext->SetViewMatrix(cam.GetViewMatrix());
   }
   ```

4. **Renderer Synchronization**: The `Renderer` uses the `ViewContext`'s matrix during the next frame's draw call, ensuring that the viewport reflects the latest input before rendering begins.

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

1. [ ] **Refactor `InputEvents` Component**:
   * Modify `Oxygen.Engine/src/Oxygen/Platform/InputEvents.h` to support injected events.
   * Expose `ForWrite()` on `InputEvents` to allow external producers to send events.
   * Ensure `ProcessPlatformEvents` (the SDL poller) is skipped or disabled when `PlatformConfig.headless` is true so the component does not subscribe to the `EventPump` in hosted mode.
   * Make the selection of pull vs injected-mode **automatic** based on `PlatformConfig.headless` (no public mode toggle).

2. [ ] **Platform testing**:
   * Add unit tests for the Platform, following OxCo testing patterns for coroutines.

3. [ ] **Implement Input Accumulator**:
   * Create a host-side `InputAccumulator` class (e.g., in the interop layer) to buffer raw input data from the host/WinUI.
   * Implement **Coalescing Logic**: Sum mouse deltas (`dx`, `dy`) and scroll deltas to ensure one update per frame.
   * Ensure thread safety (mutex) as WinUI writes and the engine reads.
   * **Delta Accumulation Details**:
     * `InputAccumulator` maintains per-frame staging buffers:
       * `Vector2 mouse_delta_accum_` — Sum of all pointer move deltas since last drain.
       * `float scroll_delta_accum_` — Sum of all mouse wheel deltas since last drain.
       * `std::vector<KeyStateChange> key_events_` — Ordered list of key press/release events (NOT accumulated, preserved in order).
       * `std::vector<ButtonStateChange> button_events_` — Ordered list of mouse button press/release events (NOT accumulated).
     * **WinUI→Accumulator** (UI Thread):
       * `PointerMoved`: `mouse_delta_accum_ += delta` (thread-safe write).
       * `PointerWheelChanged`: `scroll_delta_accum_ += wheel_delta`.
       * `KeyDown/KeyUp`: Append to `key_events_`.
       * `PointerPressed/PointerReleased`: Append to `button_events_`.
     * **Accumulator→Engine** (host frame-start / dispatch, Engine Thread):
       * Drain accumulator (acquire lock, copy values, reset accumulators).
       * Generate single `InputEvent::MouseMove` with accumulated `mouse_delta_accum_`.
       * Generate single `InputEvent::MouseWheel` with accumulated `scroll_delta_accum_`.
       * Generate `InputEvent::Key` for each key event (preserve order).
       * Generate `InputEvent::MouseButton` for each button event (preserve order).
       * Send all events to the engine via the `InputEvents::ForWrite()` writer.
   * **Focus-Loss Handling**:
     * When a viewport loses focus (`LostFocus` event in WinUI):
       * Acquire lock on accumulator for that viewport.
       * Discard accumulated `mouse_delta_accum_` and `scroll_delta_accum_`.
       * Keep key/button events (state transitions must be preserved).
     * **Rationale**: Prevents stale accumulated deltas from being applied after focus returns.
   * **Testing**: Use a direct-injection test harness to validate the accumulator and injection behavior; do not require `EditorModule` in tests.
4. [ ] **Implement Input Dispatcher**:
   * In the host's frame-start/dispatch (or the interop layer's per-frame hook):
     * Drain the `InputAccumulator`.
     * Convert raw data to `platform::InputEvent`.
     * Send events to the engine via the `InputEvents::ForWrite()` writer.
   * In engine modules' input phase (register for `kInput`):
     * Manage IMC activation/deactivation based on current mode.

5. [ ] **Update EngineRunner (Interop)**:
   * Expose methods `SendMouseMove`, `SendMouseButton`, `SendKey`, `SendWheel`.
   * Forward these calls to `EditorModule` which will send them to the engine via the `InputEvents::ForWrite()` writer.
   * **Pick Request API** (matches `CreateScene`/`CreateViewAsync` pattern):

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
   * **Implementation**: `RequestPick` enqueues `PickCommand` to EditorModule command queue. Command executes in OnPreRender, invokes callback when GPU pick completes.

6. [ ] **Implement WinUI Event Handlers**:
   * In `Viewport.xaml.cs`, bind `Pointer*`, `Key*`, and focus events.
   * Implement **Focus Management**:
     * Request focus on click.
     * On `LostFocus`: Clear accumulated mouse/scroll deltas for this viewport (preserve key/button events).
   * Implement **Cursor Management** (Local WinUI State):
     * Track orbit/pan state by detecting `Alt+LMB` press/release:
       * On `Alt+LMB` press: Save `cursor_saved_pos`, set `CoreWindow.PointerCursor = null`.
       * On `Alt+LMB` release: Restore position and cursor.
   * Implement **Pick Request** (with per-request callback):
     * On `PointerPressed`:

       ```csharp
       EngineRunner.RequestPick(viewId, cursorPos, PickType.Click, result => {
           if (result.is_gizmo) {
               active_constraint = result.screen_axis_direction;
           } else if (result.entity_id != 0) {
               // Scene object picked - trigger selection
               UpdateSelection(result.entity_id);
           }
       });
       ```

   * Implement **Mouse Constraint** (applied in `PointerMoved`):
     * If `active_constraint` exists, project delta onto constraint.
     * On `PointerReleased`: Clear `active_constraint`.
   * Implement **Hover Tracking**: Maintain `last_hovered_viewport_id` for scroll routing.

7. [ ] **Define Input Actions and Mappings**:
   * Implement all bindings from §4 (Editor Input Bindings).
   * Create IMCs: `IMC_Editor_Navigation`, `IMC_Editor_Gizmos`, `IMC_Editor_Tools`.
   * Map actions to inputs per binding tables (§4.1-§4.5).

8. [ ] **Implement EditorCameraController**:
   * Create class to consume `Editor.Camera.*` actions.
   * Update `ViewContext` view matrix based on deltas.
   * Handle "Fly Mode" (WASD + RMB) logic.
   * **Note**: Cursor hiding is handled by WinUI layer when it detects `Alt+LMB` press. Engine just processes camera deltas.

9. [ ] **Implement GizmoManager**:
    * Create `GizmoManager` class.
    * Register `IMC_Editor_Gizmos` at startup (persistent, not dynamically activated).
    * Implement gizmo action handlers:
      * `Editor.Gizmo.BeginDrag`: Check `hovered_gizmo_part_ != 0` before consuming input.
      * `Editor.Gizmo.ContinueDrag`: Consume deltas only if drag is active.
    * **Hover Detection**:
      * Process hover pick results (updated every frame in OnPreRender).
      * Maintain `hovered_gizmo_part_` state.
    * **Transform Snapping**:
      * Read `EditorSettings` for grid/angle snap configuration.
      * In `ApplyDrag()`, quantize transforms based on snap settings.
      * Check `InputSystem::GetKeyState(Key::Ctrl)` for snap toggle (matches §4.4).
    * **Note**: Mouse constraint is handled by WinUI layer (see §6.2). Engine receives pre-constrained deltas.

10. [ ] **Implement GPU-Based Picking** (Note: Already designed in `editor-artifacts-rendering.md` §5.3):
    * **Pick Command**: `RequestPick` enqueues `PickCommand` with stored callback.
    * **Execution** (OnPreRender phase):
      * Generate GPU pick request (1×1 viewport at cursor position).
      * Render MainScene + GizmoScene with ID shader.
      * Readback result (1-2 frames later).
    * **Result Construction**:
      * `entity_id`: From readback.
      * `is_gizmo`: Check if entity is in gizmo scene.
      * `screen_axis_direction`: If gizmo, get axis from scene node, project to screen space using view-projection matrix, return normalized.
    * **Callback Invocation**: Invoke stored callback with `PickResult`.
    * **Timeout Handling**: If >100ms since request, skip callback invocation, log warning.
    * **Multi-Viewport**: Each request tagged with `view_id`, callback invoked with originating viewport's context.

## 8 Deliberate Omissions (No Bloat)

Features NOT included (deferred or unnecessary for Phase 1):

* **Box Selection**: Marked as future (§4.2). Rarely used in 3D editors (more common in 2D).
* **Multi-Object Batch Updates**: Deferred until profiling shows bottlenecks (§6.4). Current test scenes have ~10 objects.
* **Complex Snapping Modes**: No vertex snapping, surface snapping, or alignment tools. Grid/angle snapping covers 90% of use cases.
* **Gesture Support**: No touch/pen gestures. Desktop-first.
* **Macro Recording**: No input recording/playback. Out of scope.

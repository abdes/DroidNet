# Editor Input Handling and Camera Control Design

## 1. Introduction & Philosophy

This document specifies the design for a **Unified Input Pipeline** in the Oxygen Editor. A "best-in-class" engine architecture does not treat editor input and game input as two separate, disconnected systems. Instead, it uses a single, robust pipeline that is **context-aware** and **priority-driven**.

### 1.1 The Unified Pipeline Philosophy

Our design is based on four core principles:

1. **Thread-Safe Command Relay**: Input events from the WinUI thread are treated as asynchronous commands to the engine. This ensures that input is processed at the correct point in the engine's frame lifecycle, avoiding race conditions.
2. **Contextual Routing**: Every input event is tagged with a `ViewId`. The engine uses this to determine which viewport, camera, or gizmo should receive the input.
3. **Priority-Based Consumption**: Input flows through a stack of handlers. High-priority editor tools (like Gizmos) can "consume" an event, preventing it from reaching lower-priority systems (like game logic).
4. **Stateful Cursor Management**: The system must handle cursor visibility, locking, and wrapping (infinite scroll) natively during continuous operations like orbiting or dragging.

### 1.2 Why a Unified System?

By routing editor input through the engine's `InputSystem` rather than bypassing it, we gain several professional-grade features:

* **Chorded Actions**: Easily handle complex combinations like `Alt + Shift + LMB` without manual state tracking.
* **Rebindable Hotkeys**: Users can customize editor shortcuts (e.g., changing the "Translate" tool from `W` to something else) using the same mapping system as the game.
* **Consistent State**: The engine maintains a single source of truth for key/button states (e.g., "Is the Ctrl key currently pressed?").
* **Transaction Awareness**: Input actions that modify scene state (like Gizmo drags) can automatically delimit Undo/Redo transactions (Begin/End).

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
> * **Hover**: Sufficient for `Zoom` (Scroll Wheel) and `Gizmo Pre-highlighting`.
> * **Focus**: Required for `Keyboard` shortcuts and `Orbit/Pan` operations. Clicking a viewport should grant it focus.

---

## 4. Technical Implementation Details

### 4.1 Direct Input Injection

In a standalone game, the `Platform` layer pulls input directly from the OS (via SDL3). In the Editor, this relationship is inverted: the `Platform` layer must accept input provided by the host application (WinUI).

To achieve this efficiently, we bypass the `EventPump` entirely in Editor Mode.

#### 4.1.1 Configurable Input Source

The `InputEvents` component in `Oxygen.Engine` will be refactored to support two modes of operation:

1. **Pull Mode (Standalone)**: The default behavior. It pulls events from the `EventPump` (which polls SDL).
2. **Push Mode (Editor)**: It exposes a public `InjectEvent(std::unique_ptr<InputEvent>)` method. In this mode, it does **not** subscribe to `EventPump`.

#### 4.1.2 Editor-Side Injection

1. **Configuration**: When the `EditorModule` initializes the engine, it configures the `InputEvents` component to run in **Push Mode**.
2. **Event Bridge**:
    * WinUI events are captured by `Viewport.xaml.cs`.
    * Passed to `EditorModule` via `EngineRunner`.
    * `EditorModule` translates them into `InputEvent` objects.
    * `EditorModule` calls `InputEvents::InjectEvent()` directly.
3. **Seamless Consumption**: The engine's `InputSystem` remains completely unaware of this swap. It continues to read from the `InputEvents` channel, receiving events that originated in WinUI.

#### 4.1.3 Handling High-Frequency Input (Throttling)

Since we are bypassing the OS event queue, the **EditorModule** becomes responsible for flow control.

* **Accumulation**: The `EditorModule` maintains a thread-safe "Input Accumulator" that receives raw events from the WinUI thread.
* **Coalescing**:
  * **Mouse Moves**: Continuous mouse deltas are summed into a single "Frame Delta".
  * **Scroll Wheel**: Deltas are summed.
  * **Keys/Buttons**: All state changes are preserved in order.
* **Injection**: Once per frame (during `kFrameStart` phase), the `EditorModule` drains the accumulator, generates optimized `InputEvent`s (e.g., one MouseMove event representing the sum of 100 raw moves), and injects them into the engine.
* **Benefit**: This prevents channel flooding and ensures the engine processes exactly one logical input update per simulation step, matching the frame rate.

### 4.2 Context-Based Routing

Instead of maintaining a separate `InputSystem` for the editor, we leverage the engine's native **Input Mapping Context (IMC)** system to handle mode switching. This ensures a unified architecture where the Editor is just another "player" with high-priority input needs.

1. **Unified Input Stream**: All events (from WinUI or SDL) flow into the single, global `InputSystem`.
2. **Mode Switching**: The `EditorModule` is responsible for activating and deactivating IMCs based on the current engine state.

| Mode | Active Contexts | Behavior |
| :--- | :--- | :--- |
| **Editing** | `IMC_Editor_Gizmos`, `IMC_Editor_Viewport`, `IMC_Editor_Global` | Game contexts are **Deactivated**. The game receives no input. |
| **Play-In-Editor (PIE)** | `IMC_Game`, `IMC_Editor_Global` (limited) | Editor viewport navigation is **Deactivated**. Game logic receives full input. |

#### 4.2.1 Advantages

* **Simplicity**: Single code path for input processing.
* **Consistency**: Editor controls use the exact same Action/Axis system as the game.
* **Configurability**: Users can rebind editor keys using the same UI used for game controls.

#### 4.2.2 Priority and Consumption

We use the standard **IMC priority system** to manage conflicts:

1. **Gizmo Priority**: `IMC_Editor_Gizmos` has the highest priority. If a user clicks a Gizmo handle, the action "consumes" the input.
   * *Cursor Hiding*: When a Gizmo action starts (e.g., dragging an axis), the cursor should be hidden or locked to the axis to prevent it from leaving the viewport.
2. **Navigation Fallback**: If no gizmo or tool consumes the input, it falls through to `IMC_Editor_Viewport` (Orbit/Pan/Zoom).
3. **Clean State**: When switching modes, we simply call `ActivateMappingContext` / `DeactivateMappingContext` on the relevant contexts.

### 4.3 Camera Control & View Matrix Updates

The `EditorCameraController` processes `Editor.Camera.*` actions during the engine's `kInput` phase. It retrieves action values from the global `InputSystem` snapshot.

1. **State Tracking**: The controller maintains the camera's state (Position, Rotation, Pivot, Zoom Level) for each `ViewId`.
2. **Matrix Calculation**: During `OnInput`, the controller calculates the new **View Matrix** based on the accumulated deltas from the local `InputSystem`'s actions.
3. **ViewContext Update**: The updated matrix is pushed to the `ViewContext` associated with the `ViewId`.

   ```cpp
   void EditorCameraController::OnInput(ViewId viewId, const InputSnapshot& snapshot) {
       auto& cam = cameras_[viewId];

       // Accumulate deltas from global actions (only active if IMC_Editor_Viewport is active)
       float2 orbitDelta = snapshot.GetActionValue<float2>("Editor.Camera.Orbit");
       float2 panDelta = snapshot.GetActionValue<float2>("Editor.Camera.Pan");
       float zoomDelta = snapshot.GetActionValue<float1>("Editor.Camera.Zoom");

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

### 4.4 Selection & Raycasting

Selection is triggered by the `Editor.Select` action (usually bound to `LMB`) within the local editor input context.

1. **Ray Generation**: The `EditorModule` uses the `ViewId` to get the viewport's camera and projection parameters. It generates a world-space ray from the screen-space mouse coordinates.
2. **Scene Query**: The ray is passed to the `PhysicsSystem` or a specialized `SceneQuerySystem` to perform a raycast against the `GameScene`.
   * *Note*: For high-precision selection (e.g., selecting a specific vertex or edge), we may eventually need an **ID Buffer** pass, but raycasting is sufficient for object-level selection in Phase 1.
3. **Hit Result**: If an object is hit, its `EntityId` is retrieved.
4. **Selection Update**: The `EditorModule` updates the `SelectionManager`, which in turn triggers a refresh of the Properties panel and updates the stencil-based outlining in the renderer.
   * *Multi-Select*: The system must check for `Shift` or `Ctrl` modifiers to support Add/Toggle selection modes.

---

## 5. Advanced Interaction Details

### 5.1 Cursor Policy

To provide a professional feel, the cursor behavior must change based on the active operation:

* **Default**: Standard arrow pointer.
* **Hovering Gizmo**: Context-specific icon (e.g., "Move X", "Rotate").
* **Orbiting/Panning**:
  * **Hide**: The cursor is hidden.
  * **Lock/Wrap**: The cursor position is locked to the start point, OR wrapped around screen edges to allow infinite movement.
  * **Restore**: On release, the cursor reappears at the original position (if locked) or current position (if wrapped).

### 5.2 Focus Policy

* **Click-to-Focus**: A viewport gains focus when clicked. This prevents accidental camera movement when interacting with other panels.
* **Hover-for-Scroll**: Mouse wheel zoom should work on hover without requiring a click, as per standard UI conventions.

---

## 6. Implementation Plan

1. [ ] **Refactor `InputEvents` Component**:
   * Modify `Oxygen.Engine/src/Oxygen/Platform/InputEvents.h` to support a "Push Mode".
   * Implement `public void InjectEvent(std::unique_ptr<InputEvent> evt)` that writes directly to the internal channel.
   * Ensure `ProcessPlatformEvents` (the SDL poller) is skipped or disabled when in Push Mode.
2. [ ] **Update Platform Configuration**:
   * Add an option to `PlatformConfig` (e.g., `input_mode: kPull, kPush`) to enable Push Mode during initialization.

3. [ ] **Implement Input Accumulator**:
   * Create `EditorModule::InputAccumulator` class to buffer raw input data from the interop layer.
   * Implement **Coalescing Logic**: Sum mouse deltas (`dx`, `dy`) and scroll deltas to ensure one update per frame.
   * Ensure thread safety (mutex) as WinUI writes and Engine reads.
4. [ ] **Implement Input Dispatcher**:
   * In `EditorModule::OnFrameStart`:
     * Drain the `InputAccumulator`.
     * Convert raw data to `platform::InputEvent`.
     * Call `InputEvents::InjectEvent()` to push to the global channel.
   * In `EditorModule::OnInput` (register for `kInput` phase):
     * Manage IMC activation/deactivation based on current mode.

5. [ ] **Update EngineRunner (Interop)**:
   * Expose methods `SendMouseMove`, `SendMouseButton`, `SendKey`, `SendWheel`.
   * Forward these calls to `EditorModule::EnqueueInput()`.

6. [ ] **Implement WinUI Event Handlers**:
   * In `Viewport.xaml.cs`, bind `Pointer*` and `Key*` events.
   * Implement **Focus Management**: Ensure Viewport requests focus on click.
   * Implement **Cursor Wrapping/Locking**: Add C# logic to handle `SetCursorPos` when requested by the engine.

7. [ ] **Define Input Actions**:
   * Create `Editor.Camera.Orbit`, `Editor.Camera.Pan`, `Editor.Camera.Zoom`.
   * Create `IMC_Editor_Navigation` and map these to `Alt+LMB`, `Alt+MMB`, `MouseWheel`.

8. [ ] **Implement EditorCameraController**:
   * Create class to consume `Editor.Camera.*` actions.
   * Update `ViewContext` view matrix based on deltas.
   * Handle "Fly Mode" (WASD + RMB) logic.

9. [ ] **Implement GizmoManager**:
    * Create `GizmoManager` class.
    * Implement `IMC_Editor_Gizmos` with high-priority actions.
    * Implement `OnInput` handler to consume clicks on gizmo handles.

10. [ ] **Implement Selection Raycast**:
    * Implement `EditorModule::GetMouseRay(ViewId)`.
    * Integrate with `PhysicsSystem::Raycast`.
    * Update `SelectionManager` on click.

11. [ ] **Undo/Redo Integration**:
    * Ensure Gizmo drag operations call `UndoSystem::BeginTransaction()` and `EndTransaction()`.

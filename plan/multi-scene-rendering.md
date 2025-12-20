# Multi-Scene and Editor Overlay Rendering Design

## 1. Introduction & Mental Model

Welcome to the Oxygen Engine's rendering architecture! As you begin working with the engine, it's important to understand how we handle complex editor features like transformation gizmos, selection outlines, and viewport tripods.

### The Core Concept: "Scene-per-View"

In many simple engines, there is one "Global Scene" that everything gets dumped into. In Oxygen, we use a **Scene-per-View** model.

* **A Scene** is a collection of objects (nodes) in 3D space.
* **A View** is a "camera" or "window" looking into a scene.
* **The Innovation**: We allow a single frame to have multiple views, and each view can look at a *different* scene.

**Why do we do this?**
Imagine you are building a game. You have your beautiful game world (the `GameScene`). Now you want to add a transformation gizmo (the red/green/blue arrows) to move an object. If you put the gizmo inside the `GameScene`, it becomes part of the game world. It might get hit by game rays, show up in reflections, or be saved into your level file by mistake.

By using a separate `EditorScene` for the gizmo and rendering it in a separate **View** on top of the game, we keep the game world "clean" while still showing the editor tools to the user.

---

## 2. Key Concepts & Glossary

Before diving into the code, familiarize yourself with these terms:

| Term | Definition |
| :--- | :--- |
| **FrameContext** | The "bucket" that holds everything needed to render exactly one frame. It travels through the entire pipeline. |
| **ViewContext** | Contains the settings for a specific view (viewport size, which scene to look at, where to draw the output). |
| **ScenePrep** | The phase where the engine decides *what* is visible in a scene and prepares the data for the GPU. |
| **RenderGraph** | A sequence of "passes" (like Opaque, Transparent, Post-Processing) that defines how pixels actually get drawn. |
| **ClearFlags** | Instructions on whether to wipe the screen (Color), the depth buffer (Depth), or the stencil buffer (Stencil) before drawing. |

---

## 3. The "Why" and "How" of Editor Features

### 3.1 Viewport Orientation Tripod (Bottom-Left XYZ)

**The Why**: The user needs to know which way is North/Up/East at all times. This tripod should stay in the corner and rotate as the camera rotates, but it shouldn't move when the camera moves (it's "fixed" in the corner).

**The How**:

1. **Separate View**: Create a view with a small viewport (e.g., 128x128) in the bottom-left corner.
2. **Rotation-Only Camera**: When calculating the view matrix for this tripod, we use the main camera's rotation but set its position to `(0, 0, 0)`.
3. **Depth Clear**: We tell the renderer to `ClearFlags::Depth` for this small corner right before drawing the tripod. This ensures the tripod is drawn "on top" of whatever game objects were in that corner.

### 3.2 Transformation Gizmos

**The Why**: Gizmos need to be "depth-aware" (you should see when they go behind a wall) but they also need to be "always-on-top" for clicking.

**The How**:

1. **EditorScene**: We keep the gizmo meshes in a dedicated `EditorScene`.
2. **Synchronization**: Every frame, we look at what is selected in the `GameScene`. We take that object's position and update the gizmo's position in the `EditorScene` to match.
3. **Layered Rendering**: We render the `GameScene` first, then we render the `EditorScene` (the gizmos) into the same buffer. Because we don't clear the depth buffer between them, the gizmos naturally occlude correctly against the game world.

### 3.3 Selection Highlighting (Stencil Outlines)

**The Why**: When you click an object, it should have a glowing outline. We use the **Stencil Buffer** because it's a high-performance way to "mask" specific pixels.

**The How**:

1. **Tagging**: During the main game render, if an object is "Selected," we tell the GPU to write a special value (e.g., `1`) into the Stencil Buffer for every pixel that object covers.
2. **Outlining**: After the scene is drawn, we run a "Post-Process Pass." This pass looks at the Stencil Buffer. If it finds a pixel that is `0` (not selected) but is right next to a pixel that is `1` (selected), it draws a highlight color. This creates the outline effect.

## 4. Advanced "Best-in-Class" Improvements

To elevate this design to the level of industry-leading engines (like Unreal or Unity), we can incorporate the following advanced patterns:

### 4.1 Visibility Masks (Layers)

**The Concept**: Instead of strictly separating objects into different scenes, we use **Visibility Masks**. Each `SceneNode` has a 64-bit mask, and each `View` has a corresponding `ViewMask`.

* **Why**: This allows an object to exist in the `GameScene` but only be visible in the "Editor View" (e.g., a light source icon). It's more flexible than moving nodes between scenes.
* **Implementation**: During `ScenePrep`, we perform a bitwise AND between the node's mask and the view's mask. If the result is zero, the node is culled.

### 4.2 Object ID Picking (Pixel-Perfect Selection)

**The Concept**: Instead of relying solely on CPU-side raycasting, we render a hidden "Object ID" pass.

* **Why**: Raycasting against complex meshes or tiny gizmo handles can be inaccurate or slow.
* **Implementation**: We add a pass to the `RenderGraph` that draws every object using a unique 32-bit integer (its ID) as the color. When the user clicks, we simply read the pixel value at the mouse coordinates from this "ID Buffer."

### 4.3 Constant Screen-Space Scaling

**The Concept**: Gizmos should stay the same size on your screen whether the camera is 1 meter or 100 meters away.

* **Why**: If gizmos shrink as you move away, they become impossible to use.
* **Implementation**: In the `EditorModule` or the Gizmo shader, we calculate a scale factor based on the distance to the camera: `Scale = Distance * Tan(FOV / 2) * Constant`.

### 4.4 Primitive Batching (DebugDraw API)

**The Concept**: For simple shapes like the grid, tripod lines, or selection boxes, we use a "Immediate Mode" style API.

* **Why**: Creating full `SceneNode`s for every line in a grid is overkill and slow to update.
* **Implementation**: Provide a `DebugDraw::Line(start, end, color)` API that collects all primitives into a single vertex buffer and draws them in one go at the end of the frame.

---

## 5. Implementation Guide for Beginners

If you are tasked with implementing or modifying this, follow these steps:

### Step 1: Update the View Structure

You will need to modify `ViewContext` to hold a pointer to a `Scene`.

* **File to look at**: `Oxygen/Core/Types/View.h` (or similar).
* **Action**: Add `observer_ptr<scene::Scene> scene;`.

### Step 2: Make ScenePrep Re-entrant

The `Renderer` currently probably assumes there is only one scene. You need to change the loop.

* **Logic**:

    ```cpp
    for (auto& view_id : frame_context.GetRegisteredViews()) {
        auto& view_ctx = frame_context.GetViewContext(view_id);
        RunScenePrep(view_ctx, view_ctx.scene); // Prepare THIS scene for THIS view
    }
    ```

### Step 3: Handle Clear Flags in the RenderGraph

When the `RenderGraph` executes a pass for a view, it must check the `ClearFlags`.

* **Logic**: If `view.clear_flags & ClearFlags::Depth`, call the graphics API to clear the depth buffer for that view's viewport before drawing.

### Step 4: Selection State Plumbing

The `EditorModule` needs to tell the `Renderer` which IDs are selected.

* **Action**: Add a `StageSelection(std::vector<NodeId> selected_ids)` method to `FrameContext`. The `Renderer` will use this list during the Opaque pass to set the Stencil state.

---

## 5. Summary of Data Flow

1. **EditorModule**: Updates Gizmo positions in `EditorScene` based on `GameScene` selection.
2. **FrameContext**: Collects all Views (Game View, Gizmo View, Tripod View).
3. **Renderer**:
   * Prepares `GameScene` for Game View.
   * Prepares `EditorScene` for Gizmo View.
4. **RenderGraph**:
   * Draws Game View (writes to Stencil for selected objects).
   * Draws Gizmo View (on top of Game View).
   * Draws Tripod View (clears depth in corner first).
   * Runs Outline Pass (reads Stencil, draws highlights).

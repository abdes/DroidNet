# View Abstraction

## View Features

1. **Encapsulation of View State**
   - Store all camera/view parameters (view matrix, projection matrix, viewport,
     scissor, etc.) in a single class.

2. **Matrix and Frustum Caching**
   - Cache derived matrices (view-projection, inverses) and frustums.
   - Only recompute them when the underlying parameters change.

3. **Viewport and Scissor Management**
   - Include viewport and scissor rectangles as part of the view state.

4. **Pixel Offset Support**
   - Support a pixel offset for TAA, jitter, or subpixel rendering.

5. **Reverse Depth and Mirroring Flags**
     - Track whether the view uses reverse depth or is mirrored. Reverse-Z is
         respected when extracting frustum planes.

6. **Visibility/Frustum Queries**
   - Provide methods to get the frustum and test bounding boxes for visibility.

## How to Integrate Camera and View

### 1. Camera as Scene Component

- Your Camera (perspective or ortho) is attached to a scene node/component.
- It defines the view and projection parameters (position, orientation, FOV,
  near/far, etc.).
- The camera is updated as part of the scene update (e.g., following a player,
  animation).

### 2. View as Render-Time Snapshot

- At render time, create or update a View object using the current state of the
  camera.
- The View takes the camera’s world transform and projection parameters and sets
  its internal matrices (view, projection, etc.).
- The View also manages render-specific state: viewport, scissor, pixel offset,
  array slice, and cached matrices/frustums.

### 3. Renderer Consumes the View

- The renderer uses the View to get all the information it needs for rendering:
  view/projection matrices, frustum for culling, viewport/scissor, etc.
- This decouples the scene/camera logic from the rendering logic, allowing for
  features like jitter, multi-view, or custom viewports without modifying the
  camera itself.

---

### Integration Flow

1. Query the camera’s transform and projection.
2. Set these values on the View: view.SetMatrices(camera.GetViewMatrix(),
   camera.GetProjectionMatrix());
3. Set any render-specific state on the View (viewport, pixel offset, etc.).
4. Pass the View to the renderer.

---

### Benefits

- Keeps camera logic focused on scene representation.
- Lets View handle all render-specific details and optimizations.
- Makes it easy to support advanced rendering features in the future (e.g., TAA,
  split-screen, VR) without changing your camera or scene logic.

---

### Summary Table

| Camera (Scene)         | View (Rendering)           |
|------------------------|----------------------------|
| Scene node/component   | Standalone render object   |
| World transform        | View matrix                |
| Projection params      | Projection matrix          |
| No render state        | Viewport, scissor, offset  |
| No caching             | Caches derived data        |
| Scene update           | Updated per-frame for render |

**In short:** The camera defines what to see; the view defines how to render it.
You update the view from the camera before

## Finalized fields and mapping

The concrete `Types/View` implementation constructs from `View::Params` and
exposes getters for matrices, cached inverses, pixel jitter, viewport/scissor,
camera position, reverse-Z, and mirroring. The frustum is cached using
`Frustum::FromViewProj(view_proj, reverse_z)`.

Values written to `SceneConstants` each frame are sourced from `View`.

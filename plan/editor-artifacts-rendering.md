# Editor Artifacts Rendering Design

## 1. Executive Summary

This document outlines the rendering architecture for editor artifacts (gizmos, selection outlines, tripods) in the Oxygen Engine. The design leverages the engine's **Scene-per-View** capability and introduces **Visibility Layers** to provide a flexible, high-performance, and "clean" editor experience.

The core philosophy is strict separation of concerns:

* **Game World Integrity**: The game scene remains untouched by editor tools.
* **View-Driven Composition**: The final image is a composition of multiple views (Game View, Editor View, Overlay View), each rendering into its own framebuffer, which are then composited by the `EditorCompositor`.

---

## 2. Core Architecture

### 2.1 The "Scene-per-View" Model

Oxygen employs a **Scene-per-View** model.

* **Scene**: A container of `SceneNode`s (spatial data).
* **View**: A camera configuration and rendering intent.
* **The Mechanism**: The `Renderer` iterates over registered *Views*. For each View, it retrieves the associated *Scene* and performs culling and rendering into the view's output Framebuffer.

This allows us to have:

1. **Main Scene**: Contains game objects AND editor-only objects (lights, volumes, camera icons). Visibility layers control what each view sees.
2. **Gizmo Scene**: Contains transformation gizmos and manipulation handles only.
3. **Tripod Scene**: Contains axis arrows (X/Y/Z) for the orientation gizmo. Separate scene justified because it uses a completely different camera setup.
4. **Composition**: The `EditorCompositor` blends multiple view outputs into the final image.

**Rationale**: UE5/Unity/Godot all use a single scene with flags. Oxygen's scene-per-view capability is powerful but should be used surgically. Most content shares the main scene; only geometrically/spatially distinct rendering contexts (like the tripod) warrant a separate scene.

### 2.2 Visibility Layers (Unified Visibility)

We unify visibility control into a single **Visibility Layer Mask**, replacing the binary `kVisible` flag to avoid redundant/conflicting systems.

* **Integration**: `SceneNodeFlags::kVisible` is **removed**. The `LayerMask` becomes the sole source of truth for visibility.
* **LayerMask**: A bitmask defining which views can see this object.
  * `kNone` (0): Invisible everywhere (Hidden).
  * `kDefault` (1 << 0): Visible in standard views (Game, Scene).
  * `kEditorIcon` (1 << 2): Visible for editor icon rendering (cameras, lights, volumes).
  * `kGizmo` (1 << 3): Visible for gizmo rendering (transformation handles).
  * `kAll` (0xFF): Visible in all views.
* **Alignment**: Bit positions match stencil buffer layout (Section 4.2.1) for efficient masking.
* **Logic**: A node is visible in a view if and only if:
  `(Node.LayerMask & View.LayerMask) != 0`.

---

## 3. Data Structures & Contracts

### 3.1 ViewContext Updates

The `ViewContext` must explicitly link a View to a Scene.

**Constraint**: A View *must* have a valid Scene to be rendered. If `scene` is null, the Renderer skips the view (or treats it as a pure post-process pass if supported).

**Impact Analysis**:

* **Strict Contract**: The `Renderer` will *only* use `ViewContext::scene`. `FrameContext::GetScene()` is strictly for simulation systems (Physics, Scripting) and is forbidden in the rendering pipeline.
* **No Fallback**: If `ViewContext::scene` is null, the view is invalid and will not be rendered. There is no fallback to a global scene.
* **Editor Scene Assignment**:
  * `GameView` → `MainScene` (game objects + editor icons).
  * `GizmoView` → `GizmoScene` (transformation handles).
  * `TripodView` → `TripodScene` (orientation widget axes).

```cpp
// Oxygen/Core/FrameContext.h

struct ViewContext {
    ViewId id {};
    View view;
    ViewMetadata metadata;

    // The Scene to be rendered by this view.
    // CONTRACT: Must be non-null for geometry rendering views.
    // CONTRACT: The Scene must contain the Camera node used by this View.
    observer_ptr<scene::Scene> scene {};

    // Render target (set by Renderer/Compositor)
    observer_ptr<graphics::Framebuffer> output {};
};
```

### 3.2 View Configuration

We add a strongly-typed layer mask to the View configuration.

```cpp
// Oxygen/Core/Types/View.h

struct View {
    // ... existing fields ...

    // Bitmask for visibility culling.
    // Determines which layers this view can see.
    // Default kAll means view sees all layers (typical for game view).
    // Editor views typically set specific bits (e.g., kEditorIcon | kGizmo).
    LayerMask visibility_mask = LayerMask::kAll;
};
```

### 3.3 SceneNode Configuration

We add the layer mask to `SceneNodeImpl`. This should be a distinct property, separate from the `SceneFlags`.

```cpp
// Oxygen/Scene/SceneNodeImpl.h

class SceneNodeImpl : ... {
    // ... existing members ...

    // Visibility layer mask.
    LayerMask layer_mask_ = LayerMask::kDefault;

public:
    void SetLayerMask(LayerMask mask) { layer_mask_ = mask; }
    auto GetLayerMask() const -> LayerMask { return layer_mask_; }
};
```

---

## 4. Rendering Pipeline Integration

### 4.1 ScenePrep Impact

**Current State**: `Renderer::OnPreRender` iterates all views and calls `RunScenePrep` for each.

**Impact**:

* `ScenePrepPipeline::Collect` is called $N$ times for $N$ views.
* **Optimization**: The `ScenePrepState` caches the "Frame-Phase" (global scene traversal). The "View-Phase" (culling) reuses this cache.
* **New Logic**: The View-Phase extractor must check the `LayerMask`.

```cpp
// Inside ScenePrepPipeline::CollectImpl (View Phase)
if (view.has_value()) {
    const auto& resolved_view = *view.value();
    if ((node.GetLayerMask() & resolved_view.Config().visibility_mask) == LayerMask::kNone) {
        return; // Cull: Layer mismatch
    }
}
```

### 4.2 Render Graph & Composition

The rendering process is defined as a sequence of explicit **Render Passes** executed by the `RenderGraph`.

#### 4.2.1 Pass Definitions

**Stencil Buffer Layout** (D32S8 format, 8-bit stencil):

* **Bit 0 (0x01)**: Primary selection
* **Bit 1 (0x02)**: Child/grouped selection
* **Bit 2 (0x04)**: Editor icons layer
* **Bit 3 (0x08)**: Gizmo layer
* **Bit 7 (0x80)**: Transparent objects flag
* **Bits 4-6**: Reserved for future use

1. **`MainScenePass`**
    * **Input**: `MainScene` (contains game objects + editor icons).
    * **Subpass 1: Game Objects**:
        * **Filter**: `LayerMask::kDefault` (game layer).
        * **Output**: `RT_GameColor` (RGBA16F, linear space), `DS_Depth` (D32S8).
        * **Stencil**: Write `0x01` for primary selected objects, `0x02` for child selections, `0x00` otherwise.
    * **Subpass 2: Editor Icons** (Same render target, no clear):
        * **Filter**: `LayerMask::kEditorIcon` (editor icon layer).
        * **Depth**: Test against `DS_Depth`, write enabled.
        * **Stencil**: Set bit `0x04` (OR operation, preserves selection bits).
        * **Benefit**: Icons depth-test against game geometry (no separate pass needed).

2. **`GizmoPass`**
    * **Input**: `GizmoScene` (transformation handles only).
    * **Output**: `RT_EditorColor` (RGBA16F, linear space).
    * **Depth**: Test against `DS_Depth` from MainScenePass (LOAD, not CLEAR), write enabled.
    * **Stencil**: Set bit `0x08` (OR operation, preserves selection + icon bits).
    * **Clear**: Color only (transparent). Depth/Stencil inherited from MainScenePass.
    * **Benefit**: Single depth buffer throughout, no bandwidth waste.

3. **`SelectionOutlinePass`**
    * **Input**: `RT_GameColor`, stencil buffer (bits `0x01` and `0x02`).
    * **Output**: `RT_SelectionMask` → `RT_DistanceField` → `RT_Outline` (RGBA16F, linear space).
    * **Algorithm**: Jump Flooding (see 5.2).

4. **`TripodPass`**
    * **Input**: `TripodScene` (3 axis arrows + labels).
    * **Viewport**: 128×128 corner overlay.
    * **Output**: `RT_Tripod` (RGBA16F, linear space, transparent background).
    * **Depth**: Separate depth buffer (cleared, independent from main scene).

5. **`CompositePass`**
    * **Input**: `RT_GameColor`, `RT_EditorColor`, `RT_Outline`, `RT_Tripod` (all linear HDR).
    * **Output**: `Backbuffer` (sRGB).
    * **Process**:
      1. Composite in linear space: `Base = Game; Base = lerp(Base, EditorColor, EditorColor.a); Base = lerp(Base, Outline, Outline.a); Base = lerp(Base, Tripod, Tripod.a);`
      2. Apply tonemapping: `Base = ACESFilmic(Base)` or equivalent.
      3. Convert to sRGB: `Output = LinearToSRGB(Base)`.
    * **Rationale**: Compositing before tonemapping ensures outlines on HDR objects (bright lights, emissive materials) maintain correct color. UE5 standard practice.

#### 4.2.2 Execution Flow

The `EditorCompositor` (or `EditorRenderGraph`) manages the passes explicitly, following the pattern in `Oxygen.Editor.Interop/src/EditorModule/RenderGraph.cpp`.

```cpp
class EditorRenderGraph {
    // Explicit pass instances
    std::shared_ptr<GameColorPass> game_pass_;
    std::shared_ptr<EditorIconPass> icon_pass_;
    std::shared_ptr<EditorGizmoPass> gizmo_pass_;
    std::shared_ptr<SelectionOutlinePass> outline_pass_;
    std::shared_ptr<TripodPass> tripod_pass_;
    std::shared_ptr<CompositePass> composite_pass_;

public:
    void SetupPasses() {
        // Initialize passes with configurations
        game_pass_ = std::make_shared<GameColorPass>(...);
        icon_pass_ = std::make_shared<EditorIconPass>(...);
        gizmo_pass_ = std::make_shared<EditorGizmoPass>(...);
        outline_pass_ = std::make_shared<SelectionOutlinePass>(...);
        tripod_pass_ = std::make_shared<TripodPass>(...);
        composite_pass_ = std::make_shared<CompositePass>(...);
    }

    auto RunPasses(const RenderContext& ctx, CommandRecorder& recorder) -> co::Co<> {
        // 1. Game Pass
        co_await game_pass_->Execute(ctx, recorder);

        // 2. Icon Pass (reads Game Depth)
        icon_pass_->SetDepthInput(game_pass_->GetDepthOutput());
        co_await icon_pass_->Execute(ctx, recorder);

        // 3. Gizmo Pass (inherits Game Depth)
        gizmo_pass_->SetDepthInput(game_pass_->GetDepthOutput());
        co_await gizmo_pass_->Execute(ctx, recorder);

        // 4. Selection Outline Pass (Reads Game Stencil)
        outline_pass_->SetStencilInput(game_pass_->GetDepthOutput());
        co_await outline_pass_->Execute(ctx, recorder);

        // 5. Tripod Pass (separate scene, independent depth)
        co_await tripod_pass_->Execute(ctx, recorder);

        // 6. Composite Pass
        composite_pass_->SetInputs(
            game_pass_->GetColorOutput(),
            gizmo_pass_->GetColorOutput(),
            outline_pass_->GetColorOutput(),
            tripod_pass_->GetColorOutput()
        );
        co_await composite_pass_->Execute(ctx, recorder);
    }
};
```

---

## 5. Feature Implementation Details

### 5.1 Transformation Gizmos

* **Scene**: `GizmoScene`.
* **Lifecycle**: Managed by `EditorModule`.
* **Screen-Space Scaling**: Updated per-frame to maintain constant screen size regardless of camera distance.
  * **Algorithm**:

    ```cpp
    float distance = (gizmoPosition - camera.position).Length();
    float screenHeight = viewport.height;
    float gizmoPixelSize = 128.0f; // Target size in pixels
    float scale = distance * tan(camera.fov / 2.0f) * (gizmoPixelSize / screenHeight);
    gizmo.SetScale(Vector3(scale, scale, scale));
    ```

  * **Rationale**: This ensures gizmos occupy consistent screen real estate (e.g., 128px) at all zoom levels.
  * **Edge Case**: Clamp scale at close distances to prevent gizmos from becoming too large when camera is very near.

### 5.2 Selection Highlighting (Jump Flooding)

* **Pass**: `SelectionOutlinePass` (Post-Process, Multi-Step).
* **Input**: Stencil buffer from `MainScenePass` (bits `0x01` for primary, `0x02` for children).
* **Algorithm**: **Jump Flooding Distance Field** (UE5's technique).
  1. **Mask Pass**: Generate `RT_SelectionMask` (R8G8) from stencil buffer:
     * **Red Channel**: Extract pixels where stencil bit `0x01` is set (primary selection).
     * **Green Channel**: Extract pixels where stencil bit `0x02` is set (child/grouped selection).
     * **Note**: Stencil writing happens during `MainScenePass`, not here.
  2. **Jump Flood Passes** (Log2(TextureSize) iterations):
     * **Step 1**: Sample 8 neighbors at offset = TextureSize/2. Store nearest "inside" pixel UV + distance.
     * **Step N**: Halve offset. Repeat until offset = 1.
     * **Result**: Distance field texture (`RT_DistanceField`, 2 channels).
  3. **Outline Shader**:
     * Sample both distance field channels.
     * `primary_mask = smoothstep(0, thickness, dist.r) - smoothstep(thickness, thickness+1, dist.r)`
     * `child_mask = smoothstep(0, thickness, dist.g) - smoothstep(thickness, thickness+1, dist.g)`
     * Blend primary outline (orange) and child outline (blue) over game color.
* **Configuration**:
  * `thickness`: 2.0px (user-adjustable).
  * `primary_color`: Orange (255, 128, 0) - UE5 standard.
  * `child_color`: Blue (94, 119, 155) - UE5 standard.
  * `glow_intensity`: 0.3 (subtle bloom around outline).
  * `show_occluded`: false (X-ray mode for seeing selection through walls).
* **Multi-Selection Hierarchy**: Allows users to distinguish primary selection from grouped/child objects visually.
* **Advantages**: Smooth anti-aliased outlines, configurable width, works with alpha-blended objects, clear selection hierarchy.

### 5.3 Object ID Picking

* **Method**: **Unified GPU Picking** (1-frame latency, imperceptible in editor UI).
* **Process**:
  1. **Frame N** (UI Thread): User clicks. Queue picking render pass with modified projection (1x1 pixel under cursor).
  2. **Frame N+1** (Engine Thread):
     * **Opaque Pass**: Render opaque geometry (MainScene + GizmoScene) with ID shader to `R32_UINT` texture.
     * **Transparent Pass**: Render transparent geometry with ID shader, depth test enabled, depth write disabled.
     * **Readback**: Map persistent texture, read pixel value. If opaque ID exists (non-zero), use it. Otherwise use transparent ID.
  3. **Frame N+2** (UI Thread): Picked EntityID delivered to editor. Selection state updates.
* **Effective Latency**: 1-2 frames (imperceptible in editor interactions).
* **Transparent Object Handling**:
  * **Problem**: Alpha-blended objects don't write stencil reliably for outline rendering.
  * **Solution**: Separate picking pass for transparent objects. Pick result prioritizes opaque over transparent (more intuitive).
  * **Stencil**: Transparent objects write to high stencil bit (`0x80`) during selection outline mask pass.
* **Rationale**:
  * Both CPU raycast and GPU pick have 1-frame latency due to thread boundaries.
  * CPU "try first, GPU fallback" wastes 2 frames on miss.
  * GPU pick is predictable, consistent, and handles all cases (gizmos + scene objects + transparent objects).
  * 1-frame latency is imperceptible in editor interactions.
* **Implementation Note**: No manual fence management needed. Engine's frame boundary synchronization is automatic.
* **Critical Understanding**: **All editor→engine operations are 1-frame latency minimum** due to thread boundaries.

### 5.4 Orientation Tripod (View Axis Gizmo)

* **Concept**: A small 3D viewport in the corner showing the world axes, rotating in sync with the main camera.
* **Implementation**:
  1. **Dedicated View**: Create a `TripodView`.
  2. **Dedicated Scene**: A simple scene containing only the axis meshes (Red X, Green Y, Blue Z) + label geometry. **Separate scene is optimal** because tripod uses orthographic projection centered at origin, completely different from main camera.
  3. **Camera Sync**:
     * The `TripodView` camera is positioned at a fixed distance from the origin.
     * Every frame, the `TripodView` camera's **rotation** is set to match the Main View's camera rotation.
     * The `TripodView` camera's **target** is always $(0,0,0)$.
  4. **Rendering**:
     * The `TripodView` is configured with a small viewport rectangle (e.g., $128 \times 128$ pixels) positioned at the bottom-left of the target surface.
     * It renders *after* the main scene render.
     * **Clear**: Clears Depth, but *not* Color (transparent background).
  5. **Performance Optimization (Render Caching)**:
     * **Problem**: Tripod geometry is static; only camera rotation changes. Re-rendering every frame wastes GPU cycles.
     * **Solution**: Cache the tripod render target and only re-render when camera rotation changes.
     * **Implementation**:

       ```cpp
       class TripodRenderer {
           Quaternion last_rotation_;
           std::shared_ptr<Framebuffer> cached_tripod_rt_;

           auto ShouldRender(const Quaternion& current_rotation) -> bool {
               constexpr float kRotationThreshold = 0.001f; // ~0.06 degrees
               float dot = Quaternion::Dot(last_rotation_, current_rotation);
               return std::abs(1.0f - dot) > kRotationThreshold;
           }

           auto Render(const ViewContext& main_view) -> Framebuffer* {
               if (ShouldRender(main_view.view.rotation)) {
                   // Re-render tripod with updated rotation
                   tripod_view_.view.rotation = main_view.view.rotation;
                   RenderTripodPass(tripod_view_, cached_tripod_rt_);
                   last_rotation_ = main_view.view.rotation;
               }
               return cached_tripod_rt_.get(); // Return cached texture
           }
       };
       ```

     * **Benefit**: Tripod renders ~1-10 times per second (only when camera moves) instead of 60-144 times per second.
     * **Rationale**: Matches UE5's approach. Tripod is a 128×128 overlay with ~300 triangles—caching saves ~0.05ms/frame at 60fps, but more importantly avoids redundant state changes.

### 5.5 Icon Billboarding (Missing Feature)

* **Purpose**: Camera/Light icons should always face the camera (Godot: `add_unscaled_billboard`).
* **Implementation**:
  * **CPU**: Before rendering, calculate billboard matrix:

    ```cpp
    Matrix4x4 billboard = Matrix4x4::LookAt(iconPos, camera.position, Vector3::Up);
    ```

  * **GPU**: Vertex shader discards model rotation, applies billboard matrix.
* **Distance Scaling**: Icons maintain constant screen size:

  ```cpp
  float distance = (iconPos - camera.position).Length();
  float screenHeight = viewport.height;
  float iconPixelSize = 32.0f; // Target icon size in pixels
  float scale = distance * tan(camera.fov / 2.0f) * 2.0f * (iconPixelSize / screenHeight);
  ```

  * **Formula Breakdown**: `tan(fov/2)` = half-height at unit distance. Multiply by 2 for full height. Scale by pixel ratio.

### 5.6 Icon Distance Culling (Missing Feature)

* **Purpose**: Don't render icons when zoomed out (Unity: distance check, Godot: BVH culling).
* **Implementation**:

  ```cpp
  constexpr float kMaxIconDistance = 1000.0f;
  if ((iconNode.position - camera.position).LengthSquared() > kMaxIconDistance * kMaxIconDistance)
      return; // Skip rendering
  ```

### 5.7 Gizmo Depth Visualization (Missing Feature)

* **Purpose**: Dim gizmos when occluded to indicate they're behind geometry (Unity technique).
* **Implementation**:
  * **Pass 1**: Render gizmo with depth test DISABLED, full opacity.
  * **Pass 2**: Render same gizmo with depth test ENABLED, 30% opacity, additive blend.
  * **Result**: Occluded parts are dimmer, visible parts are brighter.

---

## 6. Summary of Required Changes

### 6.1 Core Architecture

* [ ] **Scene Organization**:
  * `MainScene`: Game objects + editor icons (lights, cameras, volumes). Use `LayerMask` to filter per view.
  * `GizmoScene`: Transformation gizmos only.
  * `TripodScene`: Axis arrows for orientation gizmo (separate scene justified by unique camera setup).
* [ ] **`ViewContext` Update**: Add `observer_ptr<scene::Scene> scene`. Strictly enforce non-null requirement.
* [ ] **`LayerMask` Type**: Define bitmask enum in `Oxygen/Scene/Types` with `kNone`, `kDefault`, `kEditorIcon`, `kGizmo`, `kAll`.
* [ ] **`SceneNodeImpl` Update**: Add `LayerMask layer_mask_` property.
* [ ] **`ScenePrep` Culling**: Add layer mask check in View-Phase collection.

### 6.2 Picking System

* [ ] **Persistent Pick Texture**: Create 1x1 `R32_UINT` CPU-readable texture, reused across frames.
* [ ] **Unified ID Shader**: Render both MainScene + GizmoScene with entity ID shader during pick requests.
* [ ] **Transparent Picking**: Implement separate transparent pass with depth test enabled, depth write disabled. Prioritize opaque over transparent results.

### 6.3 Rendering Pipeline

* [ ] **Unified Depth Buffer**: Remove depth clear from GizmoPass. Use stencil bits for layer separation (documented layout: bits 0-3 for layers, bit 7 for transparency).
* [ ] **Jump Flood Outline**: Implement Jump Flooding algorithm with dual-channel distance field for multi-selection hierarchy (primary=orange, children=blue).
* [ ] **HDR Compositing**: Use RGBA16F for all intermediate render targets. Composite in linear space, apply tonemapping AFTER outline blend.
* [ ] **Gizmo Screen-Space Scaling**: Implement formula: `scale = distance × tan(fov/2) × (pixelSize / screenHeight)`.
* [ ] **Transparent Selection Rendering**: Transparent objects write to stencil bit `0x80` during outline mask generation. Separate rendering path required.
* [ ] **Billboard System**: Add `Matrix4x4 GetBillboardMatrix(pos, cam)` utility for icon billboarding.
* [ ] **Icon Culling**: Add distance check before rendering editor icons (max distance ~1000 units).
* [ ] **Gizmo Depth Viz**: Render gizmos twice (no depth test + depth test with dimming).

### 6.4 Compositor

* [ ] **Tripod Render Caching**: Implement quaternion-based change detection to only re-render tripod when camera rotation changes (threshold ~0.001 or 0.06 degrees). Cache 128×128 render target between frames.
* [ ] **Multi-RT Composition**: Update `EditorCompositor` to handle MainScene, Gizmo, Outline, Tripod inputs.

# EditorModule Refactor - Implementation Tasks

**Status:** ✅ COMPLETE (Except Input Handling - Out of Scope)

Build with:

```powershell
msbuild "F:\projects\DroidNet\projects\Oxygen.Editor.Interop\src\Oxygen.Editor.Interop.vcxproj" /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

---

## Phase 1: Core Scaffolding

- [x] **Create EditorView Class**: Define class structure, config struct, and basic lifecycle states.
- [x] **Create ViewRenderer Class**: Define class structure for encapsulating render passes and graph registration.
- [x] **Create ViewManager Class**: Define registry map, ID generation, and thread-safe creation methods.
- [x] **Create EditorCompositor Class**: Define structure for managing backbuffers and composition blits.

## Phase 2: State Migration

- [x] **Migrate Camera Management**: Move camera logic to `EditorView` ownership.
- [x] **Migrate Resource Creation**: Move offscreen texture and framebuffer creation to `EditorView::OnPreRender`.
- [x] **Implement ViewManager Registry**: Implement `CreateViewAsync`, `DestroyView` with async callbacks.
- [x] **Update EditorModule Hooks**: Connect `OnSceneMutation` to iterate `ViewManager` views.

## Phase 3: Rendering Pipeline

- [x] **Implement ViewRenderer**: Implement `RegisterWithEngine`, `SetFramebuffer`, and pass reuse logic.
- [x] **Implement EditorCompositor**: Implement `EnsureFramebuffersForSurface` and `CompositeToSurface` blit logic.
- [x] **Connect PreRender Phase**: Update `EditorModule::OnPreRender` to call `EditorView::OnPreRender`.
- [x] **Connect Compositing Phase**: Implement `EditorModule::OnCompositing` to drive `EditorCompositor` operations.

## Phase 4: Interop EngineRunner View Management API

### 4a: View Lifecycle Operations

- [ ] **Add ViewId Type Mapping**: Create managed ViewId struct/wrapper that maps to native `engine::ViewId`. Support serialization for callbacks.
- [ ] **Create ViewConfig Wrapper**: Managed wrapper for `EditorView::Config` to specify view name, purpose, clear color, and **compositing_target surface at creation**.
- [ ] **Add CreateViewAsync Method**: Expose `EditorModule::CreateViewAsync` through `EngineRunner`. Accept config with `compositing_target` surface, return ViewId via callback.
- [ ] **Add DestroyView Method**: Expose `EditorModule::DestroyView` through `EngineRunner`. Accept ViewId, synchronous no-op on invalid.
- [ ] **Add View Query Methods**: `GetAllViews()`, `IsViewVisible()` to query view state from managed code.
- [ ] **Implement Thread Safety**: Ensure view operations can be called from UI thread, route to engine thread via dispatcher.
- [ ] **Add Error Handling**: Invalid ViewIds result in no-ops (not exceptions).

### 4b: View Visibility Operations

- [ ] **Implement State Queries**: Query current state (Creating, Ready, Hidden, Releasing, Destroyed).
- [ ] **Add ShowView Method**: Expose `EditorModule::ShowView`. Accept ViewId, synchronous queue operation.
- [ ] **Add HideView Method**: Expose `EditorModule::HideView`. Accept ViewId, synchronous queue operation.
- [ ] **Add IsViewVisible Query**: Expose `EditorModule::IsViewVisible`. Accept ViewId, return bool.
- [ ] **Unit Tests**: Verify show/hide operations toggle visibility correctly, queries return correct state.

### 4c: Surface-View Association Operations

- [ ] **Implement GuidKey Mapping**: Map managed Surface GUIDs to native Surface pointers via SurfaceRegistry lookup.
- [ ] **Add GetViewSurface Query**: Expose `EditorModule::GetViewSurface`. Accept ViewId, return surface pointer.
- [ ] **Add GetSurfaceViews Query**: Expose `EditorModule::GetSurfaceViews`. Accept surface, return vector of ViewIds.
- [ ] **Error Handling**: Handle invalid views, detached views (return nullptr/empty vector).
- [ ] **Unit Tests**: Verify surface-view associations are queryable correctly.

### 4d: Integration Testing & Documentation

- [ ] **Add XML Comments**: Document all new EngineRunner methods with summary, parameters, returns, remarks.
- [ ] **Create Integration Tests**: Test create view → attach to surface → render on one frame.
- [ ] **Test Multi-Surface**: Create multiple surfaces with multiple views, verify all render correctly.
- [ ] **Test Show/Hide Cycles**: Hide/show views repeatedly, verify no leaks or crashes.
- [ ] **Performance Baseline**: Measure time to create 10 views, attach to surfaces, render one frame.
- [ ] **Add Samples**: Create managed code examples showing typical view management workflow.

## Phase 5: Input System

- [ ] **Define InputEvent Struct**: Event type (mouse move/click, keyboard, etc.), payload data.
- [ ] **Implement ProcessInputEvent**: Thread-safe input entry point in EditorModule.
- [ ] **Add Focus Management**: Track focused view, route input to it.
- [ ] **Add Optional Hit Testing**: For mouse events, determine view under cursor.
- [ ] **Implement ViewManager::RouteInput**: Determine target view (focus or hit test).
- [ ] **Implement EditorView::OnInput**: Handle input, update camera/gizmo state.
- [ ] **Unit Tests**: Verify input routes to correct view, updates view state.
- [ ] **Integration Tests**: Verify input routing and delivery work correctly.

---

## Phase 6: Camera Management

### 6a: External Camera Control

- [ ] **Define CameraConfig Struct**: Position, rotation, FOV, near/far planes.
- [ ] **Implement SetViewCamera**: Update view's camera node from config.
- [ ] **Implement GetViewCamera**: Query current camera configuration.
- [ ] **Add Camera Validation**: Ensure reasonable values for FOV, planes, etc.
- [ ] **Unit Tests**: Verify camera updates are applied and readable.

### 6b: Shared Cameras

- [ ] **Add Camera Node Ownership**: Allow views to reference external camera nodes.
- [ ] **Implement SetViewCamera(SceneNode)**: Support external camera nodes.
- [ ] **Update OnSceneMutation**: Handle both owned and external cameras.
- [ ] **Unit Tests**: Verify multiple views can share camera, updates propagate.

### 6c: Camera Controllers

- [ ] **Define ICameraController Interface**: Abstract controller with Update and OnInput methods.
- [ ] **Implement OrbitCameraController**: Rotate around target, zoom in/out.
- [ ] **Implement FlyCameraController**: WASD movement, mouse look.
- [ ] **Implement FixedCameraController**: Non-interactive camera.
- [ ] **Implement SetViewCameraController**: Assign controller to view.
- [ ] **Update OnUpdate Loop**: Call controller->Update each frame.
- [ ] **Connect Input Routing**: Route input events to active view's controller.
- [ ] **Unit Tests**: Verify controllers update camera correctly.
- [ ] **Integration Tests**: Verify camera moves correctly with input events.

---

## Phase 7: Render Graph Library

- [ ] **Define RenderGraphs Namespace**: Create factory functions for built-in graphs (Solid, Wireframe, Unlit, DebugNormals, DebugUVs, ShadowMapDebug).
- [ ] **Implement Solid Graph**: Standard opaque geometry with lighting and materials.
- [ ] **Implement Wireframe Graph**: Render geometry as wireframe (no fill).
- [ ] **Implement Unlit Graph**: Geometry rendered with diffuse color only (no shading).
- [ ] **Implement Debug Graphs**: Normal visualization, UV visualization, shadow map visualization.
- [ ] **Implement Graph Composition**: Support combining multiple render graphs into one (e.g., split-screen showing different graphs).
- [ ] **Unit Tests**: Verify each graph executes correctly and produces expected results.

**Estimated Effort:** 2 sprints
**Dependencies:** Phase 3 (rendering pipeline) complete

---

## Phase 8: View Templates

- [ ] **Define ViewTemplate Class**: Template configuration struct with name pattern, view config, camera, and render graph.
- [ ] **Implement Template Registry**: Static registry for registering/unregistering templates.
- [ ] **Add Built-in Templates**: Default, wireframe_pip, debug, unlit, etc.
- [ ] **Implement CreateFromTemplate**: Factory method that creates views with template settings.
- [ ] **Add Template Namespacing**: Support organizing templates by category.
- [ ] **Unit Tests**: Verify templates create views with correct configuration.

---

## Phase 9: View Pooling

- [ ] **Define ViewPool Class**: Pool implementation with acquire/release/trim methods.
- [ ] **Implement Acquire Logic**: Reuse pooled views when available, create new if needed.
- [ ] **Implement Release Logic**: Return views to pool (hide but retain resources).
- [ ] **Implement Trim Method**: Shrink pool to target size, destroy excess views.
- [ ] **Add Pool Metrics**: Track pool size, active count, reuse rate.
- [ ] **Unit Tests**: Verify acquire/release, reuse behavior, cleanup on destruction.
- [ ] **Performance Tests**: Benchmark 1000+ acquire/release cycles, compare vs direct creation.

**Estimated Effort:** 1.5 sprints
**Dependencies:** Phase 2 (view lifecycle) complete

---

## Phase 10: View Groups

- [ ] **Define ViewGroup Class**: Group container with add/remove methods.
- [ ] **Implement ShowAll/HideAll**: Bulk visibility operations.
- [ ] **Implement DestroyAll**: Bulk destruction.
- [ ] **Implement SetRenderGraph**: Bulk render graph assignment.
- [ ] **Add Group Queries**: GetViews, GetVisibleCount, etc.
- [ ] **Unit Tests**: Verify bulk operations apply to all members.

---

## Phase 11: Advanced Compositing

### 9a: Custom Composite Regions

- [ ] **Define CompositeRegion Struct**: Viewport, opacity, blend mode fields.
- [ ] **Implement SetViewCompositeRegion**: Store region per view.
- [ ] **Update Compositor**: Use per-view regions instead of fullscreen/fixed regions.
- [ ] **Unit Tests**: Verify regions applied correctly during composition.

### 9b: Custom Compositor Interface

- [ ] **Define ICompositor Interface**: Abstract base for custom compositing logic.
- [ ] **Implement BlitCompositor**: Simple texture copy.
- [ ] **Implement BlendCompositor**: Alpha blending with opacity.
- [ ] **Implement DownsampleCompositor**: Filtered downsampling.
- [ ] **Implement Custom Compositor Support**: SetViewCompositor method.
- [ ] **Unit Tests**: Verify custom compositors execute during composition phase.

### 9c: Multi-Pass Compositing

- [ ] **Define CompositePipeline Struct**: Stage vector with per-stage view/region/compositor.
- [ ] **Implement SetSurfaceCompositePipeline**: Store pipeline per surface.
- [ ] **Update OnCompositing**: Execute pipeline stages in order.
- [ ] **Unit Tests**: Verify stages execute in correct order, output correct results.

---

## Phase 10: Camera Management

### 10a: External Camera Control

- [ ] **Define CameraConfig Struct**: Position, rotation, FOV, near/far planes.
- [ ] **Implement SetViewCamera**: Update view's camera node from config.
- [ ] **Implement GetViewCamera**: Query current camera configuration.
- [ ] **Add Camera Validation**: Ensure reasonable values for FOV, planes, etc.
- [ ] **Unit Tests**: Verify camera updates are applied and readable.

### 10b: Shared Cameras

- [ ] **Add Camera Node Ownership**: Allow views to reference external camera nodes.
- [ ] **Implement SetViewCamera(SceneNode)**: Support external camera nodes.
- [ ] **Update OnSceneMutation**: Handle both owned and external cameras.
- [ ] **Unit Tests**: Verify multiple views can share camera, updates propagate.

### 10c: Camera Controllers

- [ ] **Define ICameraController Interface**: Abstract controller with Update and OnInput methods.
- [ ] **Implement OrbitCameraController**: Rotate around target, zoom in/out.
- [ ] **Implement FlyCameraController**: WASD movement, mouse look.
- [ ] **Implement FixedCameraController**: Non-interactive camera.
- [ ] **Implement SetViewCameraController**: Assign controller to view.
- [ ] **Update OnUpdate Loop**: Call controller->Update each frame.
- [ ] **Connect Input Routing**: Route input events to active view's controller.
- [ ] **Unit Tests**: Verify controllers update camera correctly.

---

## Phase 11: Scene Filtering

- [ ] **Define ISceneFilter Interface**: Abstract filter with ShouldRender method.
- [ ] **Implement LayerFilter**: Render only nodes in specified layers.
- [ ] **Implement TagFilter**: Render only nodes with specified tags.
- [ ] **Implement SetViewSceneFilter**: Assign filter to view.
- [ ] **Update ViewRenderer**: Check filter before rendering each node.
- [ ] **Unit Tests**: Verify filtered views only render subset of scene.
- [ ] **Performance Tests**: Verify filtering reduces draw calls appropriately.

**Estimated Effort:** 1.5 sprints
**Dependencies:** Phase 3 (rendering pipeline) complete

---

## Phase 12: Input System

- [ ] **Define InputEvent Struct**: Event type (mouse move/click, keyboard, etc.), payload data.
- [ ] **Implement ProcessInputEvent**: Thread-safe input entry point in EditorModule.
- [ ] **Add Focus Management**: Track focused view, route input to it.
- [ ] **Add Optional Hit Testing**: For mouse events, determine view under cursor.
- [ ] **Implement ViewManager::RouteInput**: Determine target view (focus or hit test).
- [ ] **Implement EditorView::OnInput**: Handle input, update camera/gizmo state.
- [ ] **Unit Tests**: Verify input routes to correct view, updates view state.
- [ ] **Integration Tests**: Verify camera moves correctly with input events.

---

## Phase 13: Texture Capture

- [ ] **Implement GetViewColorTexture**: Direct access to view's color texture.
- [ ] **Implement CaptureView Callback**: Async capture waiting for GPU completion.
- [ ] **Add GPU Readback**: Read texture data to CPU (optional).
- [ ] **Add File Export**: SaveTextureToFile helper for PNG/DDS/etc.
- [ ] **Unit Tests**: Verify texture readable and matches rendered content.

**Estimated Effort:** 1 sprint
**Dependencies:** Phase 3 (rendering pipeline) complete

---

## Phase 14: Post-Process Stack

- [ ] **Define PostProcessEffect Interface**: Abstract effect with Execute method.
- [ ] **Define PostProcessStack Class**: Ordered effect container with add/remove methods.
- [ ] **Implement BloomEffect**: HDR bloom post-processing.
- [ ] **Implement SSAOEffect**: Screen-space ambient occlusion.
- [ ] **Implement Depth of Field**: Per-view configurable DOF.
- [ ] **Update Compositing Phase**: Apply post-process stack before final composite.
- [ ] **Unit Tests**: Verify effects execute in order, produce expected output.
- [ ] **Performance Tests**: Benchmark multi-effect stacks on 10+ views.

**Estimated Effort:** 2 sprints
**Dependencies:** Phase 9 (advanced compositing) complete

---

## Implementation Dependencies

```text
Phase 1-3: Core (COMPLETE)
    ↓
Phase 4: Interop EngineRunner API (NEW - enables managed rendering)
    ↓
Phase 5: Input System
    ↓
Phase 6: Camera Management
    ↓
Phase 7: Render Graph Library
    ↓
├─→ Phase 8: View Templates
├─→ Phase 9: View Pooling
├─→ Phase 10: View Groups
├─→ Phase 11: Advanced Compositing
├─→ Phase 12: Scene Filtering
└─→ Phase 13: Texture Capture
    ↓
Phase 14: Post-Process Stack (builds on Phase 11)
```

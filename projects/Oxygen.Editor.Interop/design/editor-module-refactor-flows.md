# EditorModule Refactor - Flow Diagrams

**Version:** 2.0
**Date:** December 6, 2025
**Status:** ✅ Implementation Complete

---

## 1. View Lifecycle State Machine

```mermaid
stateDiagram-v2
    [*] --> Creating: CreateView()
    Creating --> Ready: OnSceneMutation() success
    Creating --> Destroyed: Error during init

    Ready --> Hidden: HideView()
    Hidden --> Ready: ShowView()

    Ready --> Releasing: DestroyView()
    Hidden --> Releasing: DestroyView()

    Releasing --> Destroyed: OnFrameStart cleanup
    Destroyed --> [*]

    note right of Creating
        Resources being allocated
        Camera node created
        Not yet registered
    end note

    note right of Ready
        Fully initialized
        Registered with FrameContext
        Registered with Renderer
        Rendering active
    end note

    note right of Hidden
        Resources retained
        NOT registered with FrameContext
        NOT registered with Renderer
        No rendering
    end note

    note right of Releasing
        Unregistered from Renderer
        Unregistered from FrameContext
        GPU resources scheduled for defer release
        Camera detached
    end note
```

---

## 2. Frame Lifecycle - Phase by Phase

### OnFrameStart Phase

```mermaid
sequenceDiagram
    participant Interop as Interop Layer
    participant EM as EditorModule
    participant SR as SurfaceRegistry
    participant VM as ViewManager
    participant EV as EditorView[]
    participant FC as FrameContext

    Note over EM: PHASE: FrameStart

    EM->>SR: ProcessSurfaceRegistrations()
    SR-->>EM: New surfaces committed

    EM->>SR: ProcessSurfaceDestructions()
    SR-->>EM: Surfaces destroyed
    Note over EM: Deferred GPU release scheduled

    EM->>VM: CleanupViewsForSurface(destroyed_surface)
    loop For each view on destroyed surface
        VM->>EV: ReleaseResources()
        EV-->>VM: State = kDestroyed
    end

    EM->>SR: ProcessResizeRequests()
    SR-->>EM: Resized surfaces
    Note over EM: Clear framebuffer cache for resized

    EM->>VM: ProcessDestroyedViews()
    VM-->>EM: Destroyed views removed

    EM->>EM: SyncSurfacesWithFrameContext()
    EM->>FC: Update surface list

    EM->>FC: SetScene(scene)
```

### OnSceneMutation Phase

```mermaid
sequenceDiagram
    participant EM as EditorModule
    participant Q as CommandQueue
    participant VM as ViewManager
    participant EV as EditorView
    participant FC as FrameContext

    Note over EM: PHASE: SceneMutation

    EM->>Q: Drain()
    loop For each command
        Q->>EM: Execute(scene)
    end

    EM->>VM: OnFrameStart() - Process pending creates
    loop For each pending create
        VM->>EV: new EditorView(config)
        VM->>EV: Initialize(scene)
        EV->>EV: Create camera node
        EV->>EV: Position camera
        EV-->>VM: State = kReady
    end

    EM->>VM: GetAllRegisteredViews()
    VM-->>EM: visible_views[]

    loop For each visible view
        EM->>EV: SetRenderingContext(ctx)
        EM->>EV: OnSceneMutation()

        EV->>EV: Update camera (viewport, aspect)

        EV->>FC: RegisterView() or UpdateView()
        FC-->>EV: ViewId assigned/updated

        EM->>EV: ClearPhaseRecorder()
    end
```

### OnPreRender Phase

```mermaid
sequenceDiagram
    participant EM as EditorModule
    participant EC as EditorCompositor
    participant VM as ViewManager
    participant EV as EditorView
    participant VR as ViewRenderer
    participant G as Graphics
    participant R as Renderer

    Note over EM: PHASE: PreRender

    EM->>EC: EnsureFramebuffersForSurface()
    loop For each surface
        alt No cached framebuffers
            EC->>G: CreateTexture(depth)
            G-->>EC: depth_texture
            EC->>G: CreateFramebuffer(backbuffer + depth)
            G-->>EC: framebuffer
            EC->>EC: Cache in surface_framebuffers_
        end
    end

    EM->>VM: GetAllRegisteredViews()
    VM-->>EM: visible_views[]

    loop For each visible view
        EM->>EV: OnPreRender(renderer)

        alt Textures need creation/resize
            EV->>G: Release old textures (deferred)
            EV->>G: CreateTexture(color, depth)
            G-->>EV: color_texture, depth_texture
            EV->>G: CreateFramebuffer(color + depth)
            G-->>EV: framebuffer
        end

        EV->>VR: SetFramebuffer(framebuffer)
        EV->>VR: RegisterWithEngine(renderer, view_id, resolver)
        VR->>R: RegisterView(view_id, resolver, graph_factory)
    end
```

### OnRender Phase

```mermaid
sequenceDiagram
    participant EM as EditorModule
    participant VM as ViewManager
    participant FC as FrameContext
    participant R as Renderer
    participant VR as ViewRenderer

    Note over EM: PHASE: Render

    EM->>FC: GetSurfaces()
    FC-->>EM: surfaces[]

    loop For each surface
        EM->>EM: Get cached framebuffer[backbuffer_index]

        EM->>VM: GetViewsForSurface(surface)
        VM-->>EM: view_ids[]

        loop For each view_id
            EM->>FC: SetViewOutput(view_id, framebuffer)
        end
    end

    Note over R: Renderer now executes for each registered view

    loop For each registered ViewId
        R->>R: Call resolver → get camera
        R->>R: Prepare scene frame
        R->>VR: Call graph_factory(view_id, render_ctx, recorder)
        VR->>VR: Execute passes (depth, shader, transparent)
        VR-->>R: Complete
    end
```

### OnCompositing Phase

```mermaid
sequenceDiagram
    participant EM as EditorModule
    participant EC as EditorCompositor
    participant VM as ViewManager
    participant EV as EditorView
    participant FC as FrameContext
    participant G as Graphics

    Note over EM: PHASE: Compositing

    EM->>EC: OnCompositing()

    EC->>VM: GetAllRegisteredViews()
    VM-->>EC: registered_views[]

    loop For each view with compositing_target
        Note over EC: Group views by surface
        EC->>EV: GetColorTexture()
        EC->>EV: GetConfig().compositing_target
    end

    EC->>G: AcquireCommandRecorder("Compositing")
    G-->>EC: recorder

    loop For each surface with views
        EC->>EC: Get current backbuffer
        EC->>recorder: BeginTrackingResourceState(backbuffer, kCommon)

        loop For each view on this surface
            EC->>recorder: BeginTrackingResourceState(color_tex, kCommon)
            EC->>recorder: RequireResourceState(color_tex, kCopySource)
            EC->>recorder: RequireResourceState(backbuffer, kCopyDest)
            EC->>recorder: FlushBarriers()
            EC->>recorder: CopyTexture(color_tex → backbuffer fullscreen)
            EC->>recorder: RequireResourceState(color_tex, kCommon)
            EC->>recorder: FlushBarriers()
        end

        EC->>recorder: RequireResourceState(backbuffer, kPresent)
        EC->>recorder: FlushBarriers()
    end

    Note over EM: Mark surfaces presentable in FrameContext
```

---

## 3. Multi-Surface Scenario: Three Editor Panels

```mermaid
graph TB
    subgraph "Interop Layer"
        A[Create LeftView]
        B[Create CenterView]
        C[Create RightView]
    end

    subgraph "ViewManager"
        D[EditorView: LeftView<br/>Camera: Left angle<br/>State: Ready]
        E[EditorView: CenterView<br/>Camera: Front<br/>State: Ready]
        F[EditorView: RightView<br/>Camera: Right angle<br/>State: Ready]
    end

    subgraph "Surface Association"
        G[LeftSurface]
        H[CenterSurface]
        I[RightSurface]
    end

    subgraph "Rendering (Per View)"
        J[ViewRenderer: LeftView<br/>Offscreen: 800x600<br/>Graph: Solid]
        K[ViewRenderer: CenterView<br/>Offscreen: 1920x1080<br/>Graph: Solid]
        L[ViewRenderer: RightView<br/>Offscreen: 800x600<br/>Graph: Wireframe]
    end

    subgraph "Compositing"
        M[LeftSurface Backbuffer<br/>800x600]
        N[CenterSurface Backbuffer<br/>1920x1080]
        O[RightSurface Backbuffer<br/>800x600]
    end

    A --> D
    B --> E
    C --> F

    D -.Attached to.-> G
    E -.Attached to.-> H
    F -.Attached to.-> I

    D --> J
    E --> K
    F --> L

    J -.Composite.-> M
    K -.Composite.-> N
    L -.Composite.-> O

    style D stroke:#a8d5ba
    style E stroke:#a8d5ba
    style F stroke:#a8d5ba
    style J stroke:#ffd4a8
    style K stroke:#ffd4a8
    style L stroke:#ffd4a8
```

**Key Points:**

- Each view has independent camera, resources, and renderer
- Each view renders to its own offscreen texture
- Each view composites fullscreen to its assigned surface
- Views can have different render graphs (wireframe vs solid)

---

## 4. PiP Scenario: Main View + Wireframe Overlay

```mermaid
graph TB
    subgraph "Single Surface"
        S[MainSurface<br/>1920x1080]
    end

    subgraph "ViewManager"
        V1[EditorView: MainView<br/>Camera: Main<br/>Region: Fullscreen<br/>State: Ready]
        V2[EditorView: PiPView<br/>Camera: Overhead<br/>Region: TopRight 25%<br/>State: Ready]
    end

    subgraph "Rendering"
        R1[ViewRenderer: MainView<br/>Offscreen: 1920x1080<br/>Graph: Solid Shaded]
        R2[ViewRenderer: PiPView<br/>Offscreen: 480x270<br/>Graph: Wireframe]
    end

    subgraph "Compositing Order"
        C1[1. MainView → Fullscreen]
        C2[2. PiPView → Region 1440,0 → 1920,270]
    end

    V1 -.Attached to.-> S
    V2 -.Attached to.-> S

    V1 --> R1
    V2 --> R2

    R1 --> C1
    R2 --> C2

    C1 --> S
    C2 --> S

    style V1 stroke:#a8d5ba
    style V2 stroke:#ffc8d5
    style R1 stroke:#ffd4a8
    style R2 stroke:#d4a8ff
    style S stroke:#d5e8ff
```

**Key Points:**

- Two views attached to same surface
- Compositing happens in order (main first, PiP overlays)
- PiP uses smaller offscreen resolution (optimization)
- Each view has different camera position and render graph

---

## 5. Surface Resize Flow

```mermaid
sequenceDiagram
    participant Win as Window
    participant SR as SurfaceRegistry
    participant EM as EditorModule
    participant VM as ViewManager
    participant EV as EditorView[]
    participant G as Graphics

    Note over Win: User resizes window
    Win->>SR: RequestResize(surface)
    SR->>SR: Mark surface.ShouldResize()

    Note over EM: Next Frame: OnFrameStart
    EM->>SR: ProcessResizeRequests()

    EM->>G: Flush() (ensure GPU idle)
    EM->>EM: Clear surface_framebuffers_[surface]
    Note over EM: Drop all backbuffer references

    EM->>SR: Surface->Resize()
    SR->>Win: Native resize
    Win-->>SR: New backbuffers

    EM->>VM: GetViewsForSurface(surface)
    VM-->>EM: affected_views[]

    Note over EM: Continue to OnSceneMutation

    loop For each affected view
        EM->>EV: OnSceneMutation(ctx)

        EV->>EV: Check texture size vs new surface size
        alt Size mismatch
            EV->>G: Schedule deferred release (old textures)
            EV->>EV: Create new textures (new size)
            EV->>EV: Create new framebuffer
            EV->>EV: Update camera aspect ratio
        end
    end

    Note over EM: OnPreRender
    EM->>EM: EnsureFramebuffers()
    EM->>G: Create new depth textures (new size)
    EM->>G: Create new framebuffers (new backbuffers + depth)
```

---

## 6. View Destruction Flow

```mermaid
sequenceDiagram
    participant Interop as Interop Layer
    participant EM as EditorModule
    participant VM as ViewManager
    participant EV as EditorView
    participant VR as ViewRenderer
    participant R as Renderer
    participant G as Graphics
    participant FC as FrameContext

    Interop->>EM: DestroyView(view_id)
    EM->>VM: DestroyView(view_id)

    VM->>EV: ReleaseResources()

    EV->>VR: UnregisterFromEngine(renderer)
    VR->>R: UnregisterView(view_id)
    R-->>VR: Resolver + factory removed
    VR-->>EV: Unregistered

    EV->>EV: Mark state = kReleasing

    EV->>G: DeferredObjectRelease(color_texture)
    EV->>G: DeferredObjectRelease(depth_texture)
    EV->>G: DeferredObjectRelease(framebuffer)
    Note over G: GPU resources queued for<br/>deferred destruction

    EV->>EV: camera_node.DetachCamera()
    EV->>EV: Mark state = kDestroyed

    EV-->>VM: Destruction complete
    VM->>VM: Mark view for removal

    Note over VM: Next Frame: OnFrameStart
    EM->>VM: ProcessDestroyedViews()
    VM->>VM: Remove view_id from maps
    VM->>VM: Destroy unique_ptr<EditorView>
    Note over EV: EditorView destructor runs<br/>Final cleanup
```

---

## 7. View Visibility Control (Register/Unregister)

> **Note:** The implementation uses `RegisterView()`/`UnregisterView()` for visibility control, not separate `ShowView()`/`HideView()` methods.

```mermaid
sequenceDiagram
    participant Interop as Interop Layer
    participant EM as EditorModule
    participant VM as ViewManager
    participant EV as EditorView
    participant VR as ViewRenderer
    participant R as Renderer
    participant FC as FrameContext

    Note over Interop: Caller unregisters view
    Interop->>VM: UnregisterView(view_id)
    VM->>VM: Mark view.is_registered = false

    Note over EM: Next Frame: OnSceneMutation
    EM->>VM: GetAllRegisteredViews()
    VM-->>EM: [] (unregistered view excluded)

    Note over EV: View NOT registered<br/>with FrameContext or Renderer<br/>No rendering occurs<br/>Resources RETAINED

    rect rgba(42, 44, 42, 1)
        Note over Interop: Caller re-registers view
        Interop->>VM: RegisterView(view_id)
        VM->>VM: Mark view.is_registered = true

        Note over EM: Next Frame: OnSceneMutation
        EM->>VM: GetAllRegisteredViews()
        VM-->>EM: [view_id] (now included)

        EM->>EV: SetRenderingContext(ctx)
        EM->>EV: OnSceneMutation()
        EV->>FC: RegisterView()
        EM->>EV: ClearPhaseRecorder()

        Note over EM: OnPreRender
        EM->>EV: OnPreRender(renderer)
        EV->>VR: RegisterWithEngine(renderer)
        VR->>R: RegisterView()

        Note over EV: View now rendering again<br/>using existing resources
    end
```

**Key Points:**

- Unregistered views keep all resources (textures, framebuffer, camera)
- Unregistered views are NOT included in rendering pipeline
- Register/Unregister is fast - no resource allocation/deallocation
- Use for temporary visibility toggles (debug overlays, etc.)

---

## 8. Resource State Tracking (Compositor)

```mermaid
stateDiagram-v2
    [*] --> Common: Initial state

    Common --> CopySource: Composite begins<br/>BeginTracking + Require
    CopySource --> Copying: FlushBarriers
    Copying --> CopySource: CopyTexture completes
    CopySource --> Common: Restore state<br/>Require + Flush
    Common --> [*]: Composite complete

    note right of Common
        Neutral state
        GPU can read/write
        No barriers needed
    end note

    note right of CopySource
        Texture readable as source
        Cannot render to it
        Compositor can read pixels
    end note

    note right of Copying
        GPU copy operation active
        Barriers applied
        Texture → Backbuffer region
    end note
```

**Critical Pattern:**

1. **Begin tracking** - Tell recorder initial state when first touching resource
2. **Require state** - Transition to needed state (insert barriers)
3. **Flush barriers** - Execute transitions before operation
4. **Perform operation** - Copy, render, etc.
5. **Restore state** - Return to neutral state for next frame
6. **Flush again** - Ensure restoration complete

This prevents stale state assumptions across frames!

---

## 9. View Creation → Destruction Lifecycle

```mermaid
gantt
    title View Lifecycle Timeline
    dateFormat X
    axisFormat %s

    section View State
    Creating     :active, 0, 1
    Ready        :active, 1, 10
    Hidden       :crit, 5, 2
    Ready (shown):active, 7, 3
    Releasing    :milestone, 10, 1
    Destroyed    :11, 1

    section FrameContext
    Not Registered  :0, 1
    Registered      :1, 4
    Unregistered    :5, 2
    Registered      :7, 3
    Unregistered    :10, 2

    section Renderer
    Not Registered  :0, 1
    Registered      :1, 4
    Unregistered    :5, 2
    Registered      :7, 3
    Unregistered    :10, 2

    section Resources
    Allocating   :active, 0, 1
    Active       :active, 1, 9
    Deferred Free:crit, 10, 1
    Released     :11, 1
```

**Timeline Events:**

- **Frame 0**: CreateView() called, state=kCreating
- **Frame 1**: Initialize() + OnSceneMutation(), state=kReady, rendering starts
- **Frame 5**: HideView(), unregister from FC/Renderer, keep resources
- **Frame 7**: ShowView(), re-register with FC/Renderer, resume rendering
- **Frame 10**: DestroyView(), unregister, schedule GPU deferred release, state=kReleasing
- **Frame 11**: ProcessDestroyedViews(), EditorView destroyed

---

## Summary

These flow diagrams illustrate:

✅ **Clear phase responsibilities** - Each phase has specific tasks
✅ **Explicit state transitions** - No implicit state changes
✅ **Resource lifecycle** - From creation through deferred destruction
✅ **Multi-surface coordination** - Multiple views and surfaces interact cleanly
✅ **Compositing pattern** - Offscreen render + blit to backbuffer
✅ **Error resilience** - Graceful handling of surface/view invalidation

All flows follow the proven patterns from MultiView while supporting the editor's dynamic view management requirements.

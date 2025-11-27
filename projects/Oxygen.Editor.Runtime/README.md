# Oxygen.Editor.Runtime

## Overview

The **Oxygen.Editor.Runtime** module provides the infrastructure layer that bridges the managed .NET editor with the native Oxygen Engine. It serves as the runtime integration layer for all editor types, managing engine lifecycle, viewport rendering surfaces, and synchronizing world domain models with the running engine.

This module sits between the editor UI layer and the lower-level components, providing high-level services that editor features can consume.

## Purpose

The Runtime module exists to:

1. **Manage Engine Lifecycle** - Initialize, start, stop, and configure the embedded native engine
2. **Arbitrate Rendering Surfaces** - Allocate and manage viewport surfaces across multiple editor documents
3. **Synchronize Game Objects** - Keep world domain models (scenes, nodes, components) in sync with the running engine
4. **Provide Editor Services** - Expose a clean, high-level API for editor features to consume

By centralizing these concerns, we achieve:
- **Reusability** across different editor types (World Editor, Material Editor, Particle Editor, etc.)
- **Clear separation** between UI concerns and engine integration
- **Testability** through well-defined interfaces
- **Performance** via batching and intelligent synchronization

## Architecture

### Module Layering

The Runtime module occupies the integration layer between presentation and platform:

```mermaid
graph TD
    subgraph Presentation["Presentation Layer"]
        WE[WorldEditor]
        ME[MaterialEditor]
        PE[ParticleEditor]
    end

    subgraph Runtime["Runtime Integration Layer"]
        ENG[Engine Subsystem]
        SYNC[Sync Subsystem]
    end

    subgraph Foundation["Foundation Layer"]
        INTEROP[Interop<br/>C++/CLI Bridge]
        WORLD[World<br/>Domain Models]
        PROJECTS[Projects<br/>Workspace Management]
        CORE[Core<br/>Low-level Utilities]
    end

    WE --> ENG
    WE --> SYNC
    ME -.Future.-> ENG
    PE -.Future.-> ENG

    ENG --> INTEROP
    SYNC --> INTEROP
    SYNC --> WORLD
    ENG --> WORLD

    style Runtime fill:#0d47a1,stroke:#64b5f6,stroke-width:3px
    style Presentation fill:#4a148c,stroke:#ba68c8
    style Foundation fill:#e65100,stroke:#ffab91
```

### Core Components

The module is organized into two primary subsystems:

```mermaid
graph LR
    subgraph Runtime["Oxygen.Editor.Runtime"]
        subgraph Engine["Engine/"]
            ES[EngineService]
            VSL[ViewportSurfaceLease]
            VSR[ViewportSurfaceRequest]
            ESS[EngineServiceState]
            LIMITS[EngineSurfaceLimits]
        end

        subgraph Sync["Sync/"]
            SS[SceneSynchronizer]
            NS[NodeSynchronizer]
            NSS[NodeSyncState]
            SB[SyncBatch]
        end
    end

    ES -.manages.-> VSL
    VSR -.creates.-> VSL
    SS -.uses.-> NS
    NS -.tracks.-> NSS
    SS -.batches via.-> SB

    style Engine fill:#1b5e20,stroke:#81c784
    style Sync fill:#f57f17,stroke:#ffd54f
```

## Engine Subsystem

### Responsibilities

1. **Lifecycle Management**: Initialize, run, and shutdown the native engine
2. **Surface Allocation**: Provide rendering surfaces to viewports
3. **Resource Arbitration**: Enforce limits on surface allocation
4. **Configuration**: Manage engine settings (FPS, logging, etc.)
5. **Thread Safety**: Ensure UI-thread-bound operations are safe

### Engine Lifecycle State Machine

```mermaid
stateDiagram-v2
    [*] --> Created: Service instantiated

    Created --> Initializing: InitializeAsync()
    Initializing --> Ready: Success
    Initializing --> Faulted: Failure

    Ready --> Running: First surface attached
    Running --> Ready: All surfaces detached
    Running --> Stopping: StopEngine()
    Ready --> Stopping: Dispose()

    Stopping --> Created: Cleanup complete

    Faulted --> [*]: Service unusable
    Created --> [*]: DisposeAsync()

    note right of Created
        No engine resources allocated
        Can be initialized
    end note

    note right of Ready
        Engine initialized in headless mode
        Render loop NOT running
        Ready to attach surfaces
    end note

    note right of Running
        Render loop active
        At least one surface attached
        Rendering to viewports
    end note
```

### Surface Leasing Model

Viewports acquire rendering surfaces through a resource lease pattern, with operations queued and processed by the EditorModule:

```mermaid
sequenceDiagram
    participant VM as ViewportViewModel
    participant ES as EngineService
    participant ER as EngineRunner<br/>(Interop)
    participant SR as SurfaceRegistry<br/>(Queue)
    participant EM as EditorModule<br/>(EngineModule)
    participant ENG as Native Engine

    VM->>ES: AttachViewportAsync(request, panel)

    ES->>ES: Check state (must be Ready/Running)
    ES->>ES: Validate surface limits
    ES->>ES: Get or create lease

    alt Engine not running
        ES->>ER: RunEngineAsync()
        ER->>ENG: Start render loop
        ES->>ES: State = Running
    end

    ES->>ER: RegisterSurfaceAsync(panel)
    ER->>SR: Queue registration request
    Note over SR: Request queued<br/>Returns immediately
    ER-->>ES: Task (pending)
    ES-->>VM: Return IViewportSurfaceLease

    Note over ENG: Next frame starts
    ENG->>EM: OnFrameStart()
    EM->>SR: DrainPendingRegistrations()
    SR-->>EM: Queued registration
    EM->>ENG: Create & register surface
    EM->>SR: CommitRegistration()
    EM->>ER: Invoke callback (success)
    ER-->>ES: Registration complete

    Note over VM: User resizes viewport
    VM->>ES: lease.ResizeAsync(w, h)
    ES->>ER: ResizeSurfaceAsync()
    ER->>SR: Queue resize request
    SR-->>ER: Queued

    Note over ENG: Next frame starts
    ENG->>EM: OnFrameStart()
    EM->>SR: ProcessResizeRequests()
    EM->>ENG: Apply resize on surface
    EM->>ER: Invoke callback (success)

    Note over VM: Viewport closed
    VM->>ES: lease.DisposeAsync()
    ES->>ER: UnregisterSurfaceAsync()
    ER->>SR: Queue destruction request
    SR-->>ER: Queued

    Note over ENG: Next frame starts
    ENG->>EM: OnFrameStart()
    EM->>SR: DrainPendingDestructions()
    SR-->>EM: Queued destruction
    EM->>ENG: RegisterDeferredRelease()
    EM->>ER: Invoke callback (success)

    alt No more surfaces
        ES->>ER: StopEngine()
        ER->>ENG: Stop render loop
        ES->>ES: State = Ready
    end
```


### Surface Limits

Resource constraints are enforced to prevent exhaustion:

```mermaid
graph TD
    REQ[Surface Request]

    REQ --> CHKG{Total < MaxTotal?}
    CHKG -->|No| ERRG[Reject: Global limit]
    CHKG -->|Yes| CHKD{Doc count < MaxPerDoc?}

    CHKD -->|No| ERRD[Reject: Per-document limit]
    CHKD -->|Yes| GRANT[Grant Lease]

    GRANT --> TRACK[Track in activeLeases]
    TRACK --> INCR[Increment counters]

    style ERRG fill:#b71c1c,stroke:#ef5350
    style ERRD fill:#b71c1c,stroke:#ef5350
    style GRANT fill:#1b5e20,stroke:#81c784
```

**Default Limits:**
- Maximum total surfaces: 16 (configurable)
- Maximum surfaces per document: 4 (configurable)

## Sync Subsystem

### Responsibilities

1. **Change Observation**: Monitor property changes on domain models
2. **Translation**: Convert editor model changes to engine API calls
3. **Batching**: Coalesce rapid changes for performance
4. **Throttling**: Prevent overwhelming the engine with updates
5. **Lifecycle Management**: Handle component add/remove/update
6. **State Tracking**: Know which nodes are synchronized

### Synchronization Architecture

```mermaid
graph TB
    subgraph Editor["World Domain Models"]
        SCENE[Scene]
        NODE1[SceneNode 1]
        NODE2[SceneNode 2]
        TRANS1[Transform]
        TRANS2[Transform]

        SCENE --> NODE1
        SCENE --> NODE2
        NODE1 --> TRANS1
        NODE2 --> TRANS2
    end

    subgraph Observer["Synchronizer Layer"]
        SS[SceneSynchronizer]
        NS1[NodeSynchronizer 1]
        NS2[NodeSynchronizer 2]
        BATCH[SyncBatch]

        SS --> NS1
        SS --> NS2
        NS1 --> BATCH
        NS2 --> BATCH
    end

    subgraph Engine["Engine Layer"]
        ER[EngineRunner]
        NENG[Native Engine]
    end

    NODE1 -.PropertyChanged.-> NS1
    NODE2 -.PropertyChanged.-> NS2
    TRANS1 -.PropertyChanged.-> NS1
    TRANS2 -.PropertyChanged.-> NS2

    BATCH --> ER
    ER --> NENG

    style Editor fill:#0d47a1,stroke:#64b5f6
    style Observer fill:#f57f17,stroke:#ffd54f
    style Engine fill:#1b5e20,stroke:#81c784
```

### Synchronization Flow

```mermaid
sequenceDiagram
    participant Model as SceneNode/Component
    participant Obs as PropertyChanged Event
    participant Sync as SceneSynchronizer
    participant Batch as SyncBatch
    participant ER as EngineRunner
    participant Eng as Native Engine

    Note over Model: User modifies property
    Model->>Model: SetField(ref field, value)
    Model->>Obs: OnPropertyChanged("Position")

    Obs->>Sync: PropertyChanged event

    Sync->>Sync: Check if node.IsActive

    alt Node is active
        Sync->>Batch: Queue change

        Note over Batch: Coalesce similar changes<br/>(e.g., multiple Position updates)

        alt Batch full OR timeout
            Batch->>ER: UpdateTransformAsync(nodeId, transform)
            ER->>Eng: Native API call
        end
    else Node is inactive
        Sync->>Sync: Ignore (not in engine)
    end
```

### Node Synchronization States

A SceneNode can be in different sync states:

```mermaid
stateDiagram-v2
    [*] --> Untracked: Node created

    Untracked --> Observed: Synchronizer.AttachNode()

    Observed --> Syncing: IsActive = true
    Syncing --> Observed: IsActive = false

    Observed --> Syncing: Sync requested

    Syncing --> Synced: Engine acknowledges
    Synced --> Syncing: Property changed

    Syncing --> Failed: Engine error
    Failed --> Syncing: Retry

    Observed --> [*]: Synchronizer.DetachNode()
    Synced --> [*]: Node destroyed

    note right of Untracked
        Node exists in editor
        Not monitored by synchronizer
    end note

    note right of Observed
        Synchronizer listening to changes
        NOT in engine yet
        Changes queued but not sent
    end note

    note right of Synced
        Node exists in engine
        Editor and engine in sync
        Changes propagated immediately
    end note
```

### Batching Strategy

To optimize performance, the synchronizer batches changes:

```mermaid
graph LR
    subgraph Changes["Property Changes"]
        PC1[Position: 1,0,0]
        PC2[Position: 1.5,0,0]
        PC3[Position: 2,0,0]
        RC1[Rotation: 0,45,0]
    end

    subgraph Batch["Batch Window<br/>(16ms)"]
        COAL[Coalesce]
    end

    subgraph Result["Sent to Engine"]
        FINAL[Position: 2,0,0<br/>Rotation: 0,45,0]
    end

    PC1 --> COAL
    PC2 --> COAL
    PC3 --> COAL
    RC1 --> COAL

    COAL --> FINAL

    style Batch fill:#f57f17,stroke:#ffd54f
    style Result fill:#1b5e20,stroke:#81c784
```

**Batching Rules:**
- Batch window: One frame (~16ms at 60 FPS)
- Coalesce: Later values override earlier ones for same property
- Flush: On frame boundary or when batch reaches size limit
- Priority: Critical changes (e.g., IsActive) may bypass batch

### Synchronization Modes

```mermaid
graph TD
    MODE{Sync Mode}

    MODE --> ACTIVE[Active Sync]
    MODE --> PASSIVE[Passive Mode]
    MODE --> BI[Bidirectional<br/>Future]

    ACTIVE --> A1[Node.IsActive = true]
    ACTIVE --> A2[Changes sent to engine]
    ACTIVE --> A3[Batched for performance]

    PASSIVE --> P1[Node.IsActive = false]
    PASSIVE --> P2[Changes observed]
    PASSIVE --> P3[NOT sent to engine]

    BI --> B1[Engine → Editor updates]
    BI --> B2[Physics, animations]
    BI --> B3[Not yet implemented]

    style ACTIVE fill:#1b5e20,stroke:#81c784
    style PASSIVE fill:#f57f17,stroke:#ffd54f
    style BI fill:#0d47a1,stroke:#64b5f6,stroke-dasharray: 5 5
```

## Design Patterns

### Observable Pattern
- Domain models implement `INotifyPropertyChanged`
- Synchronizers subscribe to property change events
- Decouples models from sync logic

### Lease Pattern
- Viewports don't directly own resources
- Resources released automatically on dispose
- Prevents resource leaks

### Singleton Service (EngineService)
- One engine instance per application
- Globally accessible via DI
- Manages shared state (surfaces, lifecycle)

### Per-Document Synchronizer
- Each Scene has its own SceneSynchronizer
- Independent lifecycles
- Isolated failure domains

## Threading Model

The Runtime module operates across **three distinct threading domains**: the editor UI thread, the managed editor space (any thread), and the native engine threads (main thread and render thread pools). The critical bridge between these domains is the **EditorModule**, a native C++ engine module that ensures all engine operations happen at the correct frame cycle phase.

### Thread Domains and Boundaries

```mermaid
graph TB
    subgraph EDITOR["Editor Domain (Managed .NET)"]
        subgraph UI["UI Thread"]
            VM[ViewModels]
            PANEL[SwapChainPanel]
            ES_UI[EngineService<br/>Surface Ops]
        end

        subgraph ANY["Any Thread"]
            ES_QUERY[EngineService<br/>Queries]
            SYNC[SceneSynchronizer<br/>Property Observers]
        end

        subgraph INTEROP["Interop Layer (C++/CLI)"]
            ER[EngineRunner<br/>Managed Facade]
            SR[SurfaceRegistry<br/>Thread-safe Queue]
        end
    end

    subgraph ENGINE["Engine Domain (Native C++)"]
        subgraph MAIN["Engine Main Thread"]
            EDMOD[EditorModule<br/>Frame-cycle aware]
            FRAME[Frame Lifecycle]
        end

        subgraph RENDER["Render Thread Pool"]
            CMD[Command Recording]
            PRESENT[Present/Swap]
        end
    end

    VM --> ES_UI
    ES_UI --> PANEL
    VM --> SYNC
    VM --> ES_QUERY

    ES_UI --> ER
    SYNC --> ER
    ES_QUERY --> ER

    ER --> SR
    SR -.Queues Requests.-> EDMOD

    EDMOD --> FRAME
    FRAME --> CMD
    FRAME --> PRESENT

    style EDITOR fill:#0d47a1,stroke:#64b5f6
    style ENGINE fill:#e65100,stroke:#ffab91
    style INTEROP fill:#4a148c,stroke:#ba68c8,stroke-width:2px
    style EDMOD fill:#1b5e20,stroke:#81c784,stroke-width:3px
```

### EditorModule: The Phase-Aware Bridge

The **EditorModule** is a native C++ `EngineModule` that runs **inside the engine** as part of its frame lifecycle. This design provides:

1. **Thread Safety**: All engine state mutations occur on the engine's main thread
2. **Phase Awareness**: Operations execute at the correct frame cycle phase
3. **Clean Separation**: Editor threads never directly touch engine internals
4. **Asynchronous Queuing**: Editor operations are queued and processed by the module

**Key Characteristics:**
- **Priority**: `kModulePriorityHighest` - Runs before other modules
- **Phases**: Participates in `kFrameStart` and `kCommandRecord`
- **Ownership**: Owns the `SurfaceRegistry` for thread-safe surface management

### Frame Cycle Integration

The EditorModule processes editor requests at specific phases of each engine frame:

```mermaid
sequenceDiagram
    participant Editor as Editor Thread<br/>(UI or Any)
    participant Interop as EngineRunner<br/>(C++/CLI)
    participant Registry as SurfaceRegistry<br/>(Thread-safe Queue)
    participant Module as EditorModule<br/>(Engine Module)
    participant Engine as Engine<br/>(Frame Lifecycle)

    Note over Engine: Frame N starts
    Engine->>Module: OnFrameStart()

    Note over Module: Process queued operations
    Module->>Registry: DrainPendingRegistrations()
    Registry-->>Module: Surface registrations
    Module->>Module: CommitRegistration()

    Module->>Registry: DrainPendingDestructions()
    Registry-->>Module: Surface destructions
    Module->>Module: RegisterDeferredRelease()

    Module->>Registry: SnapshotSurfaces()
    Registry-->>Module: Active surfaces
    Module->>Module: ProcessResizeRequests()

    Module->>Engine: SyncSurfacesWithFrameContext()

    Note over Editor: User action occurs<br/>(e.g., resize viewport)
    Editor->>Interop: ResizeSurfaceAsync()
    Interop->>Registry: QueueResize()

    Note over Engine: Continue to CommandRecord phase
    Engine->>Module: OnCommandRecord()
    Module->>Module: Render to surfaces

    Note over Engine: Frame N+1 starts
    Engine->>Module: OnFrameStart()
    Module->>Registry: DrainResizeCallbacks()
    Registry-->>Module: Callbacks for Frame N resize
    Module->>Interop: Invoke callbacks
    Interop-->>Editor: Resize complete
```

### Phase-Specific Processing

The EditorModule operates in two key phases:

```mermaid
graph TD
    subgraph FrameStart["kFrameStart Phase"]
        REG[Process Surface Registrations]
        DEST[Process Surface Destructions]
        RESIZE[Process Resize Requests]
        SYNC[Sync Surfaces with FrameContext]

        REG --> DEST
        DEST --> RESIZE
        RESIZE --> SYNC
    end

    subgraph CommandRecord["kCommandRecord Phase"]
        SNAP[Get Surfaces from FrameContext]
        ACQ[Acquire Command Recorder]
        BIND[Bind Framebuffer]
        RENDER[Render to Surface]

        SNAP --> ACQ
        ACQ --> BIND
        BIND --> RENDER
    end

    FrameStart -.Next Phase.-> CommandRecord

    style FrameStart fill:#1b5e20,stroke:#81c784
    style CommandRecord fill:#f57f17,stroke:#ffd54f
```

**kFrameStart Phase:**
1. **Drain Queues**: Pull pending operations from `SurfaceRegistry`
2. **Commit Registrations**: Add new surfaces to the engine
3. **Defer Destructions**: Queue surface cleanup for safe deferred release
4. **Apply Resizes**: Execute resize operations on the engine thread
5. **Sync Frame Context**: Update the frame's surface list

**kCommandRecord Phase:**
1. **Iterate Surfaces**: Process each surface registered in the frame
2. **Acquire Recorder**: Get a command recorder for the graphics queue
3. **Create Framebuffer**: Set up rendering target
4. **Record Commands**: Clear and render to the surface

### Thread-Safe Communication Pattern

Operations from editor threads are queued and deferred to the appropriate engine phase:

```mermaid
stateDiagram-v2
    [*] --> EditorThread: User action

    EditorThread --> EngineRunner: Call async method
    EngineRunner --> SurfaceRegistry: Queue operation
    SurfaceRegistry --> [*]: Returns immediately

    [*] --> EngineMainThread: Frame starts
    EngineMainThread --> EditorModule: OnFrameStart()
    EditorModule --> SurfaceRegistry: Drain queue
    SurfaceRegistry --> EditorModule: Pending operations
    EditorModule --> EngineInternal: Execute on engine thread
    EngineInternal --> Callback: Invoke completion
    Callback --> EditorThread: Notify result

    note right of SurfaceRegistry
        Thread-safe queue
        Lock-protected access
        Supports concurrent reads/writes
    end note

    note right of EditorModule
        Runs on engine main thread
        Phase-aware processing
        Frame-synchronized operations
    end note
```

### Thread Safety Guarantees

| Component | Thread Affinity | Safety Mechanism |
|-----------|----------------|------------------|
| **EngineService** (queries) | Any thread | Internal locks, immutable state |
| **EngineService** (surface ops) | UI thread only | `HostingContext.Dispatcher` check |
| **SceneSynchronizer** | Any thread | Queues to `EditorModule` |
| **SurfaceRegistry** | Any thread | Mutex-protected queues |
| **EditorModule** | Engine main thread | Engine phase system |
| **EngineRunner** | Any thread → queue | Delegates to `SurfaceRegistry` |
| **WinUI SwapChainPanel** | UI thread only | WinUI requirement |

### Why This Design?

**Problem**: The editor UI and domain models live on managed .NET threads, but the engine requires all state mutations to happen on its own main thread at specific frame phases.

**Solution**: The EditorModule acts as a **phase-aware adapter**:

1. **Editor threads** call `EngineRunner` methods (e.g., `RegisterSurfaceAsync()`)
2. **EngineRunner** queues the request in the thread-safe `SurfaceRegistry`
3. **EditorModule** drains the queue during `OnFrameStart()` on the engine main thread
4. **Operations execute** safely within the engine's frame lifecycle
5. **Callbacks** notify the editor thread of completion

**Benefits:**
- ✅ **No race conditions**: Engine state only mutated on engine thread
- ✅ **No frame tearing**: Operations synchronized with frame boundaries
- ✅ **Editor simplicity**: Editor code remains async/await, unaware of engine phases
- ✅ **Clean separation**: Editor and engine threading models are independent



## Performance Considerations

### Batching
- Coalesce rapid property changes (e.g., dragging)
- Flush on frame boundaries
- Reduces interop overhead

### Throttling
- Limit update frequency for high-churn properties
- Use debouncing for Transform updates
- Prevent engine overload

### Resource Limits
- Cap total surfaces (prevents memory exhaustion)
- Cap per-document surfaces (prevents single document monopoly)
- Fail fast on limit violation

### Lazy Activation
- Engine starts only when first surface attached
- Engine stops when all surfaces detached
- Conserves resources when no viewports open

## Dependencies

### Project References
- `Oxygen.Editor.Interop` - C++/CLI bridge to native engine
- `Oxygen.Editor.World` - World domain models (Scene, SceneNode, Transform, components)
- `Oxygen.Editor.Projects` - Project/workspace management (Project, ProjectInfo)
- `DroidNet.Hosting.WinUI` - UI thread context management

### NuGet Packages
- `Microsoft.WindowsAppSDK` - WinUI integration
- `Microsoft.Extensions.DependencyInjection` - Service registration
- `Microsoft.Extensions.Logging` - Diagnostic logging

## Future Enhancements

### Planned Features
- **Bidirectional Sync**: Engine → Editor updates (physics, animations)
- **Play Mode Sync**: Different behavior during play/simulate
- **Undo/Redo Integration**: Command pattern for synchronization
- **Prefab Support**: Template-instance synchronization
- **Network Sync**: Multi-user collaborative editing

### Extensibility Points
- Custom synchronizer implementations via `ISceneSynchronizer`
- Pluggable batching strategies
- Alternative engine backends via `IEngineService`

## Design Principles

1. **Separation of Concerns**: Engine and Sync are distinct subsystems
2. **Interface-Based Design**: Depend on abstractions, not concretions
3. **Observable Models**: Models remain pure, synchronizers observe
4. **Resource Safety**: Leases prevent resource leaks
5. **Performance First**: Batching and throttling are core design elements

## Related Documentation

- [Oxygen.Editor.Interop](../Oxygen.Editor.Interop/README.md) - Platform bridge layer
- [Oxygen.Editor.World](../Oxygen.Editor.World/) - World domain models
- [Oxygen.Editor.Projects](../Oxygen.Editor.Projects/) - Project workspace management
- [Synchronization Design](./docs/sync-design.md) - Detailed sync architecture
- [Surface Management](./docs/surface-management.md) - Detailed surface allocation

## License

Distributed under the MIT License. See accompanying `LICENSE` file or visit
[https://opensource.org/licenses/MIT](https://opensource.org/licenses/MIT).

---

**SPDX-License-Identifier**: MIT

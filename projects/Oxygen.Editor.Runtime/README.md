# Oxygen.Editor.Runtime

## Overview

The **Oxygen.Editor.Runtime** module provides the infrastructure layer that bridges the managed .NET editor with the native Oxygen Engine. It serves as the runtime integration layer for all editor types, managing engine lifecycle and viewport rendering surfaces.

This module sits between the editor UI layer and the lower-level components, providing high-level services that editor features can consume.

## Purpose

The Runtime module exists to:

1. **Manage Engine Lifecycle** - Initialize, start, stop, and configure the embedded native engine
2. **Arbitrate Rendering Surfaces** - Allocate and manage viewport surfaces across multiple editor documents
3. **Provide View Management** - Create, show, hide, and destroy views in the native engine
4. **Provide Editor Services** - Expose a clean, high-level API for editor features to consume

By centralizing these concerns, we achieve:

- **Reusability** across different editor types (World Editor, Material Editor, Particle Editor, etc.)
- **Clear separation** between UI concerns and engine integration
- **Testability** through well-defined interfaces
- **Resource Safety** via surface limits and lease patterns

## Architecture

### Module Layering

The Runtime module occupies the integration layer between presentation and the native engine:

```mermaid
graph TD
    subgraph Presentation["Presentation Layer"]
        WE[WorldEditor]
        ME[MaterialEditor]
        PE[ParticleEditor]
    end

    subgraph Runtime["Runtime Integration Layer"]
        ENG[Engine Subsystem]
    end

    subgraph Foundation["Foundation Layer"]
        INTEROP[Interop<br/>C++/CLI Bridge]
        WORLD[World<br/>Domain Models]
        PROJECTS[Projects<br/>Workspace Management]
    end

    WE --> ENG
    ME -.Future.-> ENG
    PE -.Future.-> ENG

    ENG --> INTEROP
    ENG --> WORLD
    ENG --> PROJECTS

    style Runtime fill:#0d47a1,stroke:#64b5f6,stroke-width:3px
    style Presentation fill:#4a148c,stroke:#ba68c8
    style Foundation fill:#e65100,stroke:#ffab91
```

### Core Components

The module's Engine subsystem consists of:

```mermaid
graph LR
    subgraph Runtime["Oxygen.Editor.Runtime"]
        subgraph Engine["Engine/"]
            IES[IEngineService]
            ES[EngineService]
            VSL[ViewportSurfaceLease]
            VSR[ViewportSurfaceRequest]
            ESS[EngineServiceState]
            EC[EngineConstants]
        end
    end

    IES --> ES
    ES -.manages.-> VSL
    VSR -.creates.-> VSL
    ES -.enforces.-> EC

    style Engine fill:#1b5e20,stroke:#81c784
```

## Engine Subsystem

### Responsibilities

1. **Lifecycle Management**: Initialize, start, and shutdown the native engine
2. **Viewport Surface Management**: Provide and manage rendering surfaces attached to WinUI SwapChainPanels
3. **Resource Arbitration**: Enforce surface allocation limits
4. **Engine Configuration**: Manage engine settings (target FPS, logging verbosity)
5. **Engine Access**: Provide a world instance for querying and mutating the engine world
6. **View Management**: Create, destroy, show, and hide editor views in the native engine

### Engine Lifecycle State Machine

```mermaid
stateDiagram-v2
    [*] --> NoEngine: Service created

    NoEngine --> Initializing: InitializeAsync()
    Initializing --> Ready: Success
    Initializing --> Faulted: Failure

    Ready --> Starting: StartAsync()
    Starting --> Running: Success
    Starting --> Faulted: Failure

    Ready --> ShuttingDown: ShutdownAsync()
    Running --> ShuttingDown: ShutdownAsync()
    ShuttingDown --> NoEngine: Success
    ShuttingDown --> Faulted: Failure

    Faulted --> Initializing: InitializeAsync() retry

    note right of NoEngine
        No engine resources allocated
        Can call InitializeAsync()
    end note

    note right of Initializing
        Transient state
        Creating and initializing engine
        No operations allowed
    end note

    note right of Ready
        Engine initialized in headless mode
        Render loop not running
        Can query/modify properties
        Can call StartAsync()
    end note

    note right of Starting
        Transient state
        Spawning engine loop task
        No operations allowed
    end note

    note right of Running
        Engine frame loop active
        Surface and view operations allowed
        Can call ShutdownAsync()
    end note

    note right of ShuttingDown
        Transient state
        Stopping engine and releasing resources
        No operations allowed
    end note

    note right of Faulted
        Fatal error occurred
        Only InitializeAsync() allowed
    end note
```

### Surface Leasing Model

Viewports acquire rendering surfaces through a resource lease pattern. The EngineService manages surface attachment, resizing, and cleanup. The engine must be in the Running state before surfaces can be attached:

```mermaid
sequenceDiagram
    participant Caller as Caller
    participant ES as EngineService
    participant ER as EngineRunner<br/>(Interop)
    participant ENG as Native Engine

    Caller->>ES: InitializeAsync()
    ES->>ER: Create engine in headless mode
    ER->>ENG: CreateEngine()
    ER-->>ES: EngineContext

    Caller->>ES: StartAsync()
    ES->>ER: RunEngineAsync()
    ER->>ENG: Start render loop
    ER-->>ES: Loop running

    Caller->>ES: AttachViewportAsync(request, panel)
    ES->>ES: Check state is Running
    ES->>ES: Validate surface limits
    ES->>ES: Get or create lease

    ES->>ER: RegisterSurfaceAsync(panel)
    ER->>ENG: Register surface with engine
    ER-->>ES: Async completion
    ES-->>Caller: Return IViewportSurfaceLease

    Note over Caller: User resizes viewport
    Caller->>ES: lease.ResizeAsync(w, h)
    ES->>ER: ResizeSurfaceAsync()
    ER->>ENG: Resize surface
    ER-->>ES: Async completion

    Note over Caller: Viewport closed
    Caller->>ES: lease.DisposeAsync()
    ES->>ER: UnregisterSurfaceAsync()
    ER->>ENG: Unregister surface
    ER-->>ES: Async completion
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

**Surface Limits:**

- Maximum total surfaces: 8 (defined in `EngineConstants.MaxTotalSurfaces`)
- Maximum surfaces per document: 4 (defined in `EngineConstants.MaxSurfacesPerDocument`)

## Design Patterns

### Lease Pattern

- Viewports don't directly own resources
- Resources released automatically on dispose
- Prevents resource leaks

### Singleton Service (EngineService)

- One engine instance per application
- Globally accessible via DI
- Manages shared state (surfaces, lifecycle)

## Threading Model

The Runtime module operates across **two distinct threading domains**: the editor UI thread (where WinUI operations must occur) and the managed editor code space (any thread for queries and configuration).

### Thread Domains and Thread Safety

| Component | Thread Affinity | Safety Mechanism |
|-----------|----------------|------------------|
| **EngineService** (queries/config) | UI thread only for surface ops | `HostingContext.Dispatcher` validation in surface attachment |
| **EngineService** (lifecycle/properties) | UI thread | Single-threaded access via gates |
| **EngineRunner** (Interop) | UI thread | Delegates to engine via C++/CLI bridge |
| **WinUI SwapChainPanel** | UI thread only | WinUI framework requirement |

### Design Rationale

**Surface Operations (AttachViewportAsync)** require the UI thread because they:

1. Accept a `SwapChainPanel` parameter (WinUI component)
2. Must compute DPI-aware dimensions based on `XamlRoot.RasterizationScale`
3. Marshal the panel to a native COM pointer for engine registration
4. Interact with the WinUI dispatcher context

**Lifecycle and Configuration** operations enforce UI thread access to ensure:

1. Consistent state transitions without race conditions
2. Serialized access to the engine runner instance
3. Safe coordination between initialization, starting, and shutdown

### Asynchronous Operations

While surface attachment and engine operations occur on the UI thread, they are exposed as `async` / `ValueTask` methods to allow non-blocking UI updates. The underlying implementation delegates to the `EngineRunner` (C++/CLI) which communicates with the native engine asynchronously.

## Performance Considerations

### Resource Limits

- Cap total surfaces (prevents memory exhaustion)
- Cap per-document surfaces (prevents single document monopoly)
- Fail fast on limit violation

### Explicit Lifecycle Management

- Engine is created during `InitializeAsync()` in headless mode (no render loop yet)
- Engine loop starts explicitly via `StartAsync()` - not automatic
- Requires explicit `StartAsync()` call before attaching viewports
- Caller must coordinate initialization and startup explicitly

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

- **Lazy Engine Loop Start**: Automatically start the engine frame loop when the first viewport surface is attached, rather than requiring explicit `StartAsync()` call
- **Engine Loop Auto-Tuning**: Dynamically adjust engine behavior based on rendering workload
  - Pause render loop when no views are active (conserve CPU/GPU)
  - Resume render loop when views become active
- **Synchronization Subsystem**: Monitor and propagate editor model changes to the engine
  - Domain model property observation
  - Batched change application for performance
  - Active/passive synchronization modes
- **Bidirectional Sync**: Engine → Editor updates (physics, animations)
- **Play Mode Sync**: Different behavior during play/simulate
- **Undo/Redo Integration**: Command pattern for synchronization
- **Prefab Support**: Template-instance synchronization
- **Network Sync**: Multi-user collaborative editing

### Extensibility Points

- Pluggable synchronization implementations
- Custom surface allocation strategies
- Alternative engine backends via `IEngineService`

## Design Principles

1. **Separation of Concerns**: Lifecycle, surfaces, and views managed distinctly
2. **Interface-Based Design**: `IEngineService` and `IViewportSurfaceLease` abstractions
3. **Resource Safety**: Lease pattern prevents surface leaks
4. **Thread Safety**: UI thread requirements enforced at boundaries
5. **Async/Await**: Modern async patterns for non-blocking operations

## Related Documentation

- [Oxygen.Editor.Interop](../Oxygen.Editor.Interop/README.md) - Platform bridge layer
- [Oxygen.Editor.World](../Oxygen.Editor.World/) - World domain models
- [Oxygen.Editor.Projects](../Oxygen.Editor.Projects/) - Project workspace management

## License

Distributed under the MIT License. See accompanying `LICENSE` file or visit
[https://opensource.org/licenses/MIT](https://opensource.org/licenses/MIT).

---

**SPDX-License-Identifier**: MIT

# Oxygen Engine Architecture (Master Overview)

This document summarizes the current high-level architecture of Oxygen based
on `src/Oxygen`. It is intended as the starting point for new features and new
engine components.

## Scope

This is a module map and systems overview, not a full design spec. For deep
dives, follow the module-specific READMEs and docs referenced here.

## Core Principles

- Modular C++ engine with explicit ownership between subsystems.
- Frame-based orchestration driven by coroutines (OxCo).
- Composition-based object model with pooled and unique components.
- Bindless-first rendering architecture with explicit resource state tracking.
- Snapshot-driven parallelism with deterministic ordered phases.

## Runtime Orchestration (Engine)

Entry point: `src/Oxygen/Engine/AsyncEngine.h`.

The AsyncEngine coordinates the frame loop and owns the phase schedule. It
manages module lifetimes through `ModuleManager`, owns the `FrameContext`, and
publishes snapshots for parallel work. The frame flow and task categories are
documented in `src/Oxygen/Engine/README.md`.

Key artifacts:

- `AsyncEngine`: frame orchestration, phase ordering, module integration.
- `FrameContext`: cross-module frame state and phase contracts in
  `src/Oxygen/Core/FrameContext.h`.
- `EngineConfig`: global engine configuration in `src/Oxygen/Config/EngineConfig.h`.
- `ModuleManager` and `EngineModule`: module system and lifecycle callbacks.

## Rendering Stack

Rendering is split into a renderer module (high-level render graph and passes)
and a backend-agnostic graphics layer (device, resources, queues).

Renderer:

- `src/Oxygen/Renderer/README.md` is the design entry point.
- Render graphs are coroutines composed from pass objects.
- `RenderContext` is the per-frame shared state and pass registry.
- Passes live under `src/Oxygen/Renderer/Passes/` and follow
  Validate → PrepareResources → Execute.

Graphics:

- `src/Oxygen/Graphics/README.md` outlines the graphics architecture and
  subsystem responsibilities.
- `Graphics` is the device facade; `CommandRecorder` is the primary recording API.
- `DescriptorAllocator` and `ResourceRegistry` provide bindless descriptor and
  view management.
- Backends implement the interfaces in `src/Oxygen/Graphics/Common/` and are
  loaded via `src/Oxygen/Loader/`.

Key flow:

- Scene + view data is resolved into renderable snapshots.
- Render graphs execute via renderer-owned passes and graphics command recorders.
- Presentation is coordinated by the graphics backend and engine phase order.

## Scene

Scene graph:

- `Scene` is the authoritative scene graph manager in
  `src/Oxygen/Scene/Scene.h`.
- Nodes are stored in a `ResourceTable` and accessed via `SceneNode` handles.
- Hierarchy operations (create, destroy, reparent, adopt) are all routed through
  `Scene`.
- Scene-global environment systems are owned by `SceneEnvironment` in
  `src/Oxygen/Scene/Environment/`.

## Composition & Type System

The Composition module provides the object model used across the engine.

Primary references:

- `src/Oxygen/Composition/README.md` for the design and storage model.
- `src/Oxygen/Base/Resource.h` and `ResourceTable.h` for pooled storage.

Key ideas:

- `Object` and `Component` provide RTTI-free type IDs.
- `Composition` aggregates components with hybrid storage.
- Pooled components use `ResourceTable` + handles for dense storage.
- Unique components are stored directly on the composition.

## Content & Data

Content pipeline:

- `src/Oxygen/Content/README.md` is the entry point for the PAK system.
- Assets and resources are packaged in PAK files and loaded via `AssetLoader`.
- `LoaderContext` carries readers, dependency collectors, and source tokens.

Data representations:

- `src/Oxygen/Data/README.md` describes the runtime asset data model.
- Assets are immutable after construction and shareable across systems.
- Geometry assets are structured as Mesh → SubMesh → MeshView.

## Input

- `src/Oxygen/Input/README.md` defines the action/trigger/mapping system.
- Input is snapshot-based with explicit frame phases:
  FrameStart → Input → Snapshot → FrameEnd.
- Platform input events are translated into actions and contexts.

## Platform

The Platform module provides windows, input event capture, and OS services.

Key files:

- `src/Oxygen/Platform/Platform.h`, `Window.h`, `Input.h`.
- SDL integration under `src/Oxygen/Platform/SDL/`.

Platform is consumed by Engine and Input; it is not directly tied to rendering.

## Console & Configuration

- `src/Oxygen/Console/README.md` documents CVars and command execution.
- Config types are in `src/Oxygen/Config/` for engine, graphics, renderer, and
  path discovery.

Console CVars are applied at deterministic frame boundaries by the engine.

## Coroutines & Concurrency (OxCo)

- `src/Oxygen/OxCo/README.md` covers structured concurrency primitives.
- Engine phases and renderer passes use OxCo coroutines and nurseries.

## Bindless & Index Reuse (Nexus)

`src/Oxygen/Nexus/` contains utilities for stable index allocation and reuse,
used by bindless descriptor systems and resource tables.

Key types:

- `GenerationTracker`, `FrameDrivenSlotReuse`, `TimelineGatedSlotReuse`.

## Serialization & Streams (Serio)

`src/Oxygen/Serio/` provides stream abstractions, readers, and writers for
binary serialization and file I/O (`Stream.h`, `Reader.h`, `Writer.h`).

## UI & Editor Integration

- `src/Oxygen/ImGui/` provides ImGui integration and rendering passes.
- `src/Oxygen/EditorInterface/` exposes an editor-facing API and engine runner
  for tooling or host applications.

## Module Map (Quick Reference)

| Module | Responsibility | Key Entry Points |
| --- | --- | --- |
| `Base` | Core utilities, logging, resource handles, type lists | `src/Oxygen/Base/Logging.h`, `Resource.h` |
| `Core` | Shared engine types, frame context | `src/Oxygen/Core/FrameContext.h` |
| `Composition` | Object/component system with pooled storage | `src/Oxygen/Composition/README.md` |
| `Engine` | Frame orchestration and module lifecycle | `src/Oxygen/Engine/AsyncEngine.h` |
| `Renderer` | Render graph, passes, render context | `src/Oxygen/Renderer/README.md` |
| `Graphics` | Device abstraction, resources, command recording | `src/Oxygen/Graphics/README.md` |
| `Loader` | Graphics backend loader | `src/Oxygen/Loader/README.md` |
| `Scene` | Scene graph, nodes, queries, environment | `src/Oxygen/Scene/Scene.h` |
| `Content` | PAK asset pipeline and loaders | `src/Oxygen/Content/README.md` |
| `Data` | Immutable runtime asset representations | `src/Oxygen/Data/README.md` |
| `Input` | Actions/triggers/mappings + snapshots | `src/Oxygen/Input/README.md` |
| `Platform` | Windowing, input capture, OS services | `src/Oxygen/Platform/Platform.h` |
| `Console` | CVars and command execution | `src/Oxygen/Console/README.md` |
| `Config` | Engine/graphics/renderer config structs | `src/Oxygen/Config/` |
| `OxCo` | Structured concurrency | `src/Oxygen/OxCo/README.md` |
| `Nexus` | Bindless index reuse utilities | `src/Oxygen/Nexus/` |
| `Serio` | Serialization streams | `src/Oxygen/Serio/` |
| `ImGui` | ImGui integration and passes | `src/Oxygen/ImGui/` |
| `EditorInterface` | Editor-facing API and engine runner | `src/Oxygen/EditorInterface/Api.h` |
| `Clap` | CLI utilities | `src/Oxygen/Clap/README.md` |
| `TextWrap` | Text layout utilities | `src/Oxygen/TextWrap/README.md` |

## Integration Contracts (What to Touch for New Features)

New engine module:

- Implement `EngineModule` and register it with `ModuleManager`.
- Hook lifecycle phases using `OnFrameStart`, `OnInput`, `OnSnapshot`,
  `OnPreRender`, `OnRender`, `OnCompositing`, `OnFrameEnd`.
- Use `FrameContext` as the authoritative per-frame state.

New render pass:

- Add a pass class under `src/Oxygen/Renderer/Passes/`.
- Follow the RenderPass lifecycle and register the pass in a render coroutine.
- Use bindless descriptors through the `ResourceRegistry` and `DescriptorAllocator`.

New scene system:

- Add a component under `Scene/Environment` and attach it to
  `SceneEnvironment`.
- Keep authored parameters on the component and derived GPU resources in the
  renderer.

New asset type:

- Define packed descriptors in `src/Oxygen/Data/PakFormat.h`.
- Add a Data wrapper class in `src/Oxygen/Data/`.
- Register loader in `Content` and wire dependencies via `LoaderContext`.

New input features:

- Extend triggers or action types under `src/Oxygen/Input/`.
- Integrate with InputSystem lifecycle phases and snapshot semantics.

## Architectural Boundaries (Keep Clean)

- Engine owns orchestration, not subsystem internals.
- Renderer owns render graph and pass orchestration; Graphics owns device APIs.
- Scene owns hierarchy data; renderer reads snapshots only.
- Content owns asset loading; Data owns immutable runtime representations.

## Where to Go Deeper

- Frame model and phase contracts: `src/Oxygen/Engine/README.md`.
- Render graph and pass architecture: `src/Oxygen/Renderer/README.md`.
- Graphics backend contracts: `src/Oxygen/Graphics/Common/README.md`.
- Scene environment and rendering implications: `src/Oxygen/Scene/Environment/README.md`.
- Content pipeline and asset dependencies: `src/Oxygen/Content/README.md`.

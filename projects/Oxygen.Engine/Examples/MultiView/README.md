# Multi-View Rendering Example

Production‑style **Multi‑View** demo with explicit render graphs and
compositing. The render graph is just C++ code: you pass in data and the graph
executes logic directly in coroutines.

## Features

- **Two simultaneous views** rendering the same scene with different cameras.
- **Main View**: full‑screen solid view.
- **PiP View**: picture‑in‑picture view with its own viewport/scissor.
- **Compositing graph**: view outputs are composed into the swapchain.
- **GUI overlay**: ImGui renders after compositing.
- **Contracts enforced**: hard CHECK_* assertions on required data.

## Architecture Overview

### View Render Graph (per view)

Each view uses a `ViewRenderer` render graph that is configured with a
`ViewRenderData` struct (textures, flags, clear color). The graph then executes
passes using that data.

Key points:

- **Data‑only inputs** (`ViewRenderData`).
- **Graph is C++**: branching and customization happen in code.
- **Per‑view isolation** via view‑specific `RenderContext` data.

### Compositing Graph

`CompositorGraph` runs after view rendering. It composites all view outputs
to the swapchain backbuffer, then renders ImGui **after** compositing.

### GUI Overlay

ImGui is rendered post‑composite through the view renderer graph routine,
ensuring a consistent render order and correct resource states.

## Building

```bash
cmake --build out/build --target Oxygen.Examples.MultiView
```

## Running

```bash
./out/build/bin/Debug/Oxygen.Examples.MultiView.exe
```

## Architecture Notes

- Scene created once; views share the scene but not view state.
- Each view registers its own resolver and render graph with the engine.
- Compositing is isolated in a dedicated graph stage.
- Runtime contracts are enforced via CHECK_* assertions.

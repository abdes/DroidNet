# Examples/Common — Reusable utilities for Oxygen Engine examples

This folder contains small, reusable helper classes used by the Oxygen Engine example projects. The goal is to centralize common example scaffolding (window, app bootstrap, module base, and a minimal render graph) so other examples can focus on demonstrating engine features instead of boilerplate.

## Location

- `projects/Oxygen.Engine/Examples/Common`

## Contents

- `AppWindow.h` / `AppWindow.cpp` — Lightweight platform window wrapper used by examples to create and manage the main window and handle basic input/resizing.
- `AsyncEngineApp.h` — Small application bootstrap helper for running the engine main loop on a background thread and coordinating startup/shutdown for examples.
- `ExampleModuleBase.h` / `ExampleModuleBase.cpp` — Base class for example modules. Provides common lifecycle hooks (Initialize, Update, Render, Shutdown) and helper utilities for registering resources with the engine.
- `ExampleComposition.h` — Utility composing Renderer/Scene/Camera sets used by multiple examples to avoid duplicated setup code.
- `RenderGraph.h` / `RenderGraph.cpp` — A simplified render graph implementation used by examples to demonstrate rendering passes without pulling in the full engine render-pipeline code.

## Quick usage

- Include the headers from your example source files, for example:

```cpp
#include "Examples/Common/ExampleModuleBase.h"
#include "Examples/Common/AppWindow.h"
```

- Derive your example module from `ExampleModuleBase` and implement `OnInitialize`, `OnUpdate`, and `OnRender` (or the engine-specific lifecycle hooks provided). Use `ExampleComposition` helpers to set up a default camera/scene when appropriate.

- Typical example flow:
  1. Create an `AppWindow` instance and show the window.
  2. Instantiate your derived `ExampleModuleBase` and register it with the example's app/bootstrap helper (`AsyncEngineApp` or equivalent).
  3. Let the example run; module lifecycle methods will be invoked by the example runtime.

## Build notes

- Examples expect the engine project to be available in the solution. Add this folder to your example project's include paths so headers resolve correctly (project-relative include paths are used in the examples).
- No external dependencies beyond the Oxygen Engine core are required by these helpers. If an example expands functionality (e.g., GPU/third-party libs), update that example's project file accordingly.

## Style & conventions

- Files in this folder are intentionally minimal and example-oriented — they prioritize clarity and small surface area over production-level robustness.
- Follow the repository coding conventions when modifying these files: explicit access modifiers, `this.` for instance members, and nullability where appropriate.

## License & attribution

- These files are part of the `DroidNet` repository and follow the repository `LICENSE` located at the project root.

# Oxygen.Editor.Interop

## Overview

The Oxygen.Editor.Interop project provides a mixed-mode C++/CLI interop layer to let managed .NET
Editor code interact with the native Oxygen Engine. The project exposes a small, well-defined
managed API surface used by the editor for engine lifecycle management, logging integration, surface
registration, and minimal world helpers.

This repository contains a compact managed facade (C++/CLI) that owns and marshals native engine
resources (std::shared_ptr) and implements conversion helpers for configuration types.

## Technology Stack

- **Language:** C++ (C++/CLI for managed interop) and C#
- **Platform:** Windows (x64)
- **Target Framework:** net9.0 (managed), C++/CLI (MSVC v145) with C++20
- **Build System:** MSBuild / CMake (Premake5)
- **Key Dependencies:** fmt, glm, Oxygen.Engine, Microsoft.WindowsAppSDK
- **Testing:** MSTest for managed tests

## Core components and API surface

The interop exposes a focused set of managed types (under Oxygen::Editor::EngineInterface and
Oxygen::Interop::World). The most important pieces are:

- EngineRunner (Oxygen::Editor::EngineInterface::EngineRunner)
  - Primary managed facade used by the editor.
  - ConfigureLogging(LoggingConfig) -> bool — initialize the native loguru backend.
  - ConfigureLogging(LoggingConfig, Object logger) -> bool — forward native logs to the provided
    managed ILogger (the object must implement Microsoft.Extensions.Logging.ILogger).
  - CreateEngine(EngineConfig) / CreateEngine(EngineConfig, IntPtr swapChainPanel) -> EngineContext
    — create a native engine context. Passing a non-zero swapChainPanel configures the engine
    headless and allows swap-chain attachment by the caller.
  - RunEngine / RunEngineAsync / StopEngine — start and stop the engine run-loop on a dedicated
    render thread; RunEngineAsync returns a Task that completes when the engine stops.
  - RegisterSurface / ResizeSurface / UnregisterSurface — UI thread-bound helpers to register and
    manage per-viewport surfaces.
  - CaptureUiSynchronizationContext — capture the current SynchronizationContext so headless runs
    can still post cleanup / dispatch operations back to a known UI context.

- EngineContext (Oxygen::Editor::EngineInterface::EngineContext)
  - A thin managed wrapper that owns a std::shared_ptr`<native EngineContext>`.
  - Supports deterministic cleanup (destructor + finalizer) and exposes `IsValid`.

- OxygenWorld (Oxygen::Interop::World::OxygenWorld)
  - A small facade for scene helpers. In the current code this type is a placeholder: CreateScene /
    RemoveSceneNode are not implemented and return false.

## Important implementation notes

- Logging forwarding is implemented by a managed LogHandler which registers a native loguru
  callback. The bridge uses a GCHandle and a native callback to safely forward native log messages
  into the managed ILogger via reflection.
- The project includes a thread-safe native SurfaceRegistry keyed by a 16-byte GUID
  (`array<uint8_t,16>`) used by the editor module to track surfaces shared with the engine.
- SimpleEditorModule is an example/utility module registered on the native engine: it snapshots
  registered surfaces and issues a simple blue clear to any presentable surface during command
  recording (this is intentionally minimal and can be replaced by real rendering logic).
- Threading helpers:
  - UiThreadDispatcher captures and enforces a UI SynchronizationContext for UI-bound operations.
  - RenderThreadContext manages the dedicated engine render thread used by RunEngineAsync.

## WinUI 3 and swap-chain notes

When attaching to a WinUI 3 SwapChainPanel, the code queries `ISwapChainPanelNative` using a
desktop/WinUI IID defined in EngineRunner.cpp (63AAD0B8-7C24-40FF-85A8-640D944CC325). Callers should
pass a non-zero IntPtr representing the SwapChainPanel's native pointer and ensure CreateEngine(...)
is invoked on the UI thread (or capture the UI SynchronizationContext first for headless runs).

## Project layout

```plaintext
Oxygen.Editor.Interop/
├── src/
│   ├── EngineRunner.h / .cpp      # Engine lifecycle, logging configuration and engine loop helpers
│   ├── EngineContext.h / .cpp     # Managed wrapper for native EngineContext
│   ├── OxygenWorld.h / .cpp       # Small scene helpers (currently stubs)
│   ├── Config.h / .cpp            # Managed <-> native configuration marshalling
│   ├── AssemblyInfo.cpp           # Managed assembly metadata
│   ├── vcpkg-configuration.json   # vcpkg config used by builds
│   ├── Base/LoguruWrapper.h       # Logging bridge used by tests and the LogHandler
│   ├── UiThreadDispatcher.*       # capture/verify UI SynchronizationContext and enforce UI-thread access
│   ├── RenderThreadContext.*      # manages the dedicated render thread
│   ├── SurfaceRegistry.*          # thread-safe native surface registry
│   └── SimpleEditorModule.*       # example editor module that registers surfaces
├── test/
│   ├── EngineTests.cs             # engine lifecycle + threading tests
│   ├── ConfigureLoggingTests.cs   # logging configuration + forwarding tests
│   └── Oxygen.Editor.Interop.Tests.csproj
├── EditorInterop.sln
├── premake5.lua
└── README.md
```

## Getting started

Prerequisites:

- Oxygen Engine build artifacts and headers should be available under:
  $(ProjectRoot)\Oxygen.Engine\out\install
- Add the Oxygen.Engine runtime directory to your PATH (for test runs):

```powershell
$env:PATH += ";$(ProjectRoot)\Oxygen.Engine\out\install\bin"
```

- .NET 9.0 SDK and Visual Studio with MSVC v145 (C++/CLI) installed
- vcpkg available for native dependencies (see vcpkg-configuration.json)

Build the interop library with MSBuild (use a Visual Studio / Developer Command Prompt where MSBuild
is available):

```powershell
# Build the project (example, Debug/x64):
msbuild .\src\Oxygen.Editor.Interop.vcxproj /p:Configuration=Debug /p:Platform=x64

# Or build the solution:
msbuild .\EditorInterop.sln /p:Configuration=Debug /p:Platform=x64
```

## Running tests

The project includes managed MSTest suites that exercise the interop boundary and its threading/logging expectations.

Run tests (use the built test executable directly - do NOT use `dotnet test` or the VS test runner):

- Build the test project with MSBuild (Debug/x64 shown as example):

  ```powershell
  msbuild .\test\Oxygen.Editor.Interop.Tests.csproj /p:Configuration=Debug /p:Platform=x64
  ```

- Locate the test runner executable produced by the test project (usually under
  `artifacts\bin\Oxygen.Editor.Interop.Tests\Debug_net9.0-windows10.0.26100.0\`). For example:

  The produced executable is the test runner for the project; run it directly to execute tests (it
  will return a non-zero exit code on failures). If you want verbose output, the test executable may
  accept command-line arguments depending on the test runner configured in the project — consult the
  test project file if you need specific runner flags.

## Development notes

- When exposing new native functionality, add explicit managed DTOs and conversion helpers in
  `Config.h` and update the tests.
- Tests show how to supply a managed logger object (the `TestLogger` in `ConfigureLoggingTests.cs`)
  that exposes `Log<TState>(...)` so the native-to-managed forwarding path can validate expected
  messages.
- EngineRunner enforces UI-thread invocation semantics for UI-bound operations. Use
  CaptureUiSynchronizationContext when doing headless runs in tests or background-only scenarios.

## Contributing

Follow the repo coding conventions for C++ (C++20, MSVC) and C# (C# preview features used). Add
managed tests for any new interop surface and keep the interop surface small and explicit: prefer
simple, well-documented transformer/conversion helpers rather than complex logic in the bridging
layer.

## License

Distributed under the 3-Clause BSD License. See LICENSE file for details.

---

## Related documentation

- Oxygen Engine internals: `projects/Oxygen.Engine/.github/copilot-instructions.md`
- DroidNet repository conventions: `.github/copilot-instructions.md`, `.github/instructions/csharp_coding_style.instructions.md`

# Oxygen.Editor.Interop

## Purpose and ownership

This project contains the C++/CLI boundary and native editor module used by
Oxygen Editor to run the embedded engine, manage surfaces and views, translate
input, and project authored scenes into the runtime.

The managed entry point for editor features is
[Oxygen.Editor.Runtime](../Oxygen.Editor.Runtime/README.md).
Authoring data, document history, project policy, and cook orchestration retain
their own module owners. See the
[editor architecture](../../design/editor/ARCHITECTURE.md) and
[runtime integration LLD](../../design/editor/lld/runtime-integration.md).

## Technology and source layout

[Oxygen.Editor.Interop.vcxproj](src/Oxygen.Editor.Interop.vcxproj) targets
Windows x64, .NET 9, and MSVC v145 with C++20. Native command and rendering
translation units are compiled without managed support; the facade and
marshalling code use C++/CLI. Engine headers and libraries come from
`projects/Oxygen.Engine/out/install/{Configuration}`.

| Area | Responsibility |
| --- | --- |
| [EngineRunner](src/EngineRunner.h) | Engine creation, lifetime task, logging, surface and view operations. |
| [EngineContext](src/EngineContext.h) | Managed ownership of a native shared engine context. |
| [World/OxygenWorld](src/World/OxygenWorld.h) | Scene/node operations, components, properties, materials, environment, and cooked-root requests. |
| [Input/OxygenInput](src/Input/OxygenInput.h) | Managed input events forwarded to the native editor input path. |
| [EditorModule](src/EditorModule/EditorModule.h) | Engine-phase integration, command dispatch, viewport navigation, and Vortex integration. |
| [Commands](src/Commands) | Native scene and view commands and component property appliers. |
| [SurfaceRegistry](src/EditorModule/SurfaceRegistry.h) / [ViewManager](src/EditorModule/ViewManager.h) | Separate surface registration and engine-view ownership. |
| [UiThreadDispatcher](src/UiThreadDispatcher.h) / [RenderThreadContext](src/RenderThreadContext.h) | UI-context dispatch and the dedicated engine thread. |

## Managed API

The lifecycle facade is `Oxygen.Interop.EngineRunner`; scene and input
facades are in `Oxygen.Interop.World` and `Oxygen.Interop.Input`.

- `CreateEngine` creates an `EngineContext` from managed configuration.
- `RunEngineAsync` returns the engine lifetime task; `StopEngine` requests
  loop termination. The lifetime task does not represent first-frame readiness.
- `TryRegisterSurfaceAsync`, `TryResizeSurfaceAsync`, and
  `TryUnregisterSurfaceAsync` coordinate native surfaces.
- `TryCreateViewAsync`, `TryDestroyViewAsync`, `TryShowViewAsync`, and
  `TryHideViewAsync` operate on engine views independently of surface identity.
- `OxygenWorld` implements scene creation and destruction, node/hierarchy
  mutation, transform and component updates, geometry/material assignment,
  environment updates, and cooked-root requests.
- `ConfigureLogging` can bridge native log output into a managed logger.

For operation-specific preconditions and completion semantics, use the
[runtime contract](../../design/editor/lld/runtime-integration.md).
In particular, `SyncOutcome.Accepted` means boundary acceptance; it does not
prove asset resolution or a visibly presented frame.

## Native design to preserve

### Explicit frame phases

[EditorCommand](src/EditorModule/EditorCommand.h) requires every command to
select its target phase. [EditorModule](src/EditorModule/EditorModule.cpp)
dispatches frame-start and scene-mutation work in the appropriate engine
callbacks. This keeps ordinary scene/view commands out of arbitrary UI-thread
mutation paths.

### Ownership and short queue locks

Commands enter the queue as `std::unique_ptr<EditorCommand>`, making queued
command ownership explicit. `CommandContext` uses `observer_ptr` for
execution-time engine services; handlers must not retain those pointers.

[ThreadSafeQueue](src/EditorModule/ThreadSafeQueue.h) swaps pending work into a
local batch under its mutex and calls consumers after releasing that mutex.
Phase-filtered draining preserves the order of retained work ahead of new
arrivals. Preserve this separation when adding command handlers or callbacks.

[EngineContext](src/EngineContext.h) pairs deterministic managed disposal with a
finalizer around the native `shared_ptr` holder. This ownership mechanism
still requires correct lifecycle orchestration above it.

### Component-specific property application

[SetPropertiesCommand](src/Commands/SetPropertiesCommand.h) groups scalar
entries by component and dispatches spans to registered appliers.
[PropertyApplierRegistry](src/Commands/PropertyApplierRegistry.cpp) initializes
the built-in transform, perspective-camera, and directional-light appliers
with `std::call_once`. New scalar component behavior belongs in an applier,
while asset references and scene-level environment publication retain their
specialized command paths.

These patterns are implemented, but do not establish complete lifecycle or
runtime-parity validation. Follow-up work covers
[managed shutdown](https://github.com/abdes/DroidNet/issues/3),
[engine-loop supervision](https://github.com/abdes/DroidNet/issues/6),
[superseded asset completions](https://github.com/abdes/DroidNet/issues/5),
and [procedural content ownership](https://github.com/abdes/DroidNet/issues/11).
The concrete-facade boundary is tracked in
[issue #10](https://github.com/abdes/DroidNet/issues/10).

## Build and test entry points

Follow the repository's [editor verification rules](../../design/editor/RULES.md):
use parallel MSBuild and an existing compatible engine installation. Engine
build/verification is a separate owner workflow.

Project entry points:

- [Interop library](src/Oxygen.Editor.Interop.vcxproj).
- [Managed interop tests](test/Oxygen.Editor.Interop.Tests.csproj).
- [Native command/input tests](test/native/Oxygen.Editor.Interop.NativeTests.vcxproj).

Run the built managed test executable using the repository test workflow.
[NativeDllSearchPath](test/NativeDllSearchPath.cs) registers the configuration's
engine install `bin` directory before tests touch the mixed-mode assembly.

[EngineTests](test/EngineTests.cs) covers the runner and surface boundary;
[ConfigureLoggingTests](test/ConfigureLoggingTests.cs) covers log forwarding.
The native tests exercise input accumulation and environment/light commands.
Managed `EngineService` lifecycle tests and controlled asynchronous asset
completion tests remain necessary for the follow-up issues above. Existing
test source is not evidence that a new checkout has been built or run.

## Related documentation

- [Live engine sync](../../design/editor/lld/live-engine-sync.md).
- [Viewport and tools](../../design/editor/lld/viewport-and-tools.md).
- [Property pipeline](../../design/editor/lld/property-pipeline-redesign.md).
- [Implementation and validation status](../../design/editor/IMPLEMENTATION_STATUS.md).

## License

Distributed under the 3-Clause BSD License; see the source file headers.

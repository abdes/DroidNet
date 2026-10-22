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
Windows x64, .NET 10, and MSVC v145 with C++20. Native command and rendering
translation units are compiled without managed support; the facade and
marshalling code use C++/CLI. Engine headers and libraries come from
`projects/Oxygen.Engine/out/install/{Configuration}`.

Engine headers reachable from this boundary must remain C++20-compatible. Use
Oxygen `Result<T, E>` for exposed fallible APIs; keep C++23-only implementation
headers behind forward declarations where their definitions are unnecessary.
For example, recording-use retention returns `Result<void, RegistrationError>`.

| Area                                                                                                  | Responsibility                                                                                   |
| ----------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| [EngineRunner](src/EngineRunner.h)                                                                    | Engine creation, lifetime task, logging, surface and view operations.                            |
| [EngineContext](src/EngineContext.h)                                                                  | Managed ownership of a native shared engine context.                                             |
| [World/OxygenWorld](src/World/OxygenWorld.h)                                                          | Scene/node operations, components, properties, materials, environment, and cooked-root requests. |
| [Input/OxygenInput](src/Input/OxygenInput.h)                                                          | Managed input events forwarded to the native editor input path.                                  |
| [EditorModule](src/EditorModule/EditorModule.h)                                                       | Engine-phase integration, command dispatch, viewport navigation, and Vortex integration.         |
| [Commands](src/Commands)                                                                              | Native scene and view commands and component property appliers.                                  |
| [SurfaceRegistry](src/EditorModule/SurfaceRegistry.h) / [ViewManager](src/EditorModule/ViewManager.h) | Separate surface registration and engine-view ownership.                                         |
| [UiThreadDispatcher](src/UiThreadDispatcher.h) / [RenderThreadContext](src/RenderThreadContext.h)     | UI-context dispatch and the dedicated engine thread.                                             |

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

Cooked-root refresh prepares source metadata and a candidate path resolver on
workers. Scene mutation rejects superseded requests, switches both owners, then
delivers retirement callbacks outside the request mutex. Failed preparation leaves
the previous mounts selected; replaced completion owners cancel managed waiters.
The [Content mount contract](../Oxygen.Engine/src/Oxygen/Content/Docs/loose_cooked_content.md)
owns generation lifetime and stale-candidate rules.

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

Follow the repository's [editor verification rules](../../design/oxygen/RULES.md#validation-and-delivery)
and [build workflow reference](../../tooling/doc/build.md):
use 64-bit Visual Studio MSBuild (`MSBuild\Current\Bin\amd64\MSBuild.exe`) with
`/m /p:PreferredToolArchitecture=x64` from an x64 developer environment and an
existing compatible engine SDK installation. Engine build/verification is a
separate owner workflow.

Project entry points:

- [Interop library](src/Oxygen.Editor.Interop.vcxproj).
- [Managed interop tests](test/Oxygen.Editor.Interop.Tests.csproj).
- [Native command/input tests](test/native/Oxygen.Editor.Interop.NativeTests.vcxproj).

Run the built test outputs using `traverse Invoke-Tests --start projects/Oxygen.Editor.Interop --configuration Debug`.
[NativeDllSearchPath](testing/NativeDllSearchPath.cs), in the
[test kit](testing/README.md), registers the configuration's engine install
`bin` directory; each test project that loads the mixed-mode assembly calls it
from its own module initializer.

[EngineTests](test/EngineTests.cs) covers the runner and surface boundary;
[ConfigureLoggingTests](test/ConfigureLoggingTests.cs) covers log forwarding.
The native tests exercise input accumulation and environment/light commands.
Managed `EngineService` lifecycle tests and controlled asynchronous asset
completion tests remain necessary for the follow-up issues above. Existing
test source is not evidence that a new checkout has been built or run.

### Build performance and C++/CLI guidelines (DOs and DON'Ts)

Because `Oxygen.Editor.Interop` mixes pure native C++20 translation units with
`/clr:netcore` C++/CLI translation units, small project or header mistakes can
severely degrade compile and link times or trigger CLR linker metadata errors.

#### 1. Precompiled headers (`pch.h` vs. `pch_clr.h`)

- **DO** compile pure native `.cpp` files (`EditorModule/*`, `Commands/*`) with
  `<CompileAsManaged>false</CompileAsManaged>` and `<PrecompiledHeaderFile>pch.h</PrecompiledHeaderFile>`
  ([src/pch.h](src/pch.h)).
- **DO** compile `/clr:netcore` C++/CLI `.cpp` files with
  `<PrecompiledHeader>Use</PrecompiledHeader>` and `<PrecompiledHeaderFile>pch_clr.h</PrecompiledHeaderFile>`
  ([src/pch_clr.h](src/pch_clr.h)), and native test `.cpp` files with
  [test/native/src/pch.h](test/native/src/pch.h).
- **DON'T** set `<PrecompiledHeader>NotUsing</PrecompiledHeader>` on translation
  units.
  - _Why:_ MSVC cannot share a single `.pch` between native and `/clr:netcore`
    TUs, and compiling `/clr:netcore` TUs without a PCH forces every managed TU
    to re-parse STL, GLM, and Oxygen headers from scratch (~7–8 s per file).
- **DON'T** include `<Oxygen/OxCo/...>` coroutine headers or heavy umbrella
  headers like `<Oxygen/Content/AssetLoader.h>` inside [src/pch_clr.h](src/pch_clr.h)
  or [src/pch.h](src/pch.h).
  - _Why:_ MSVC's `/clr:netcore` PCH metadata emitter fails with `LNK2022`
    token/alignment collisions when native classes with nested types (such as
    `oxygen::co::ThreadPool`) are precompiled into `pch_clr.obj`. In addition,
    `<Oxygen/Content/AssetLoader.h>` (1,656 lines) transitively pulls in
    `OxCo/ThreadPool.h`, `asio`, and `<winsock2.h>` into every TU; include
    `<Oxygen/Content/IAssetLoader.h>` or `<Oxygen/Content/AssetLoader.h>` only
    in the specific `.cpp` files that require them.

#### 2. `#pragma managed` placement and C++/CLI header hygiene

- **DO** wrap all native C++, STL, GLM, and Oxygen Engine `#include` directives
  in `#pragma managed(push, off)` / `#pragma managed(pop)` before switching to
  `#pragma managed` (or `#pragma managed(push, on)`) in C++/CLI headers and
  `.cpp` files. Always balance `#pragma managed(push, ...)` with
  `#pragma managed(pop)` at the end of headers.
- **DON'T** place `#pragma managed` at the top of a `.cpp` or `.h` file before
  native `#include` blocks.
  - _Why:_ Any native class, inline function, or template parsed while managed
    compilation is active causes the C++/CLI compiler to emit redundant MSIL
    metadata definitions and transition thunks into the `.obj` file, tripling
    link times and risking `LNK2022` ODR metadata conflicts.
- **DON'T** manually forward-declare .NET BCL types (such as `System::Guid` or
  `System::Threading::Tasks::TaskCompletionSource`) in C++/CLI headers.
  - _Why:_ Referenced .NET runtime assemblies are already imported via `/FU`.
    Manual forward declarations conflict with metadata when a generic BCL type
    shares a name with a non-generic type (e.g., `TaskCompletionSource` vs.
    `TaskCompletionSource<TResult>` in .NET 10, causing `C2989`).
- **DON'T** pass managed `^` handles directly to `std::make_shared<T>(...)` for
  native wrapper classes whose constructor accepts a managed handle.
  - _Why:_ `<memory>` is precompiled under `#pragma managed(push, off)`, so
    `std::make_shared`'s internal control block (`_Ref_count_obj2`) is compiled
    with native calling conventions and cannot invoke a `__clrcall` constructor
    (`C3821`). Construct with `std::shared_ptr<T>(new T(handle))` instead.

#### 3. Native and managed header decoupling

- **DO** keep native headers (`Commands/*`, `EditorModule/*`) free of C++/CLI
  syntax (`ref class`, `value struct`, `literal`, `^`, `<msclr/...>`). Share
  wire/property constants needed by both native and managed code in pure C++
  headers such as [Commands/PropertyKeys.h](src/Commands/PropertyKeys.h).
- **DON'T** include C++/CLI headers (such as
  [World/OxygenWorld.h](src/World/OxygenWorld.h) or [Config.h](src/Config.h))
  from native headers or native `.cpp` files.
  - _Why:_ Pulling C++/CLI headers into native headers forces transitive
    consumers to compile with `/clr:netcore`, defeating native PCH reuse and
    native `/MP` parallelism.

#### 4. Exporting native symbols for `NativeTests` (`OXGN_EI_API`)

- **DO** export native classes and functions tested by
  [Oxygen.Editor.Interop.NativeTests](test/native/Oxygen.Editor.Interop.NativeTests.vcxproj)
  using `OXGN_EI_API` / `OXGN_EI_NDAPI` ([src/api_export.h](src/api_export.h))
  so the test project links them directly from `Oxygen.Editor.Interop.lib`.
- **DON'T** add `..\..\src\...` production `.cpp` files as `<ClCompile>` items
  in `Oxygen.Editor.Interop.NativeTests.vcxproj`.
  - _Why:_ Recompiling production `.cpp` files inside the test project compiles
    them a second time under `/clr:netcore` and increases `NativeTests` build
    time by over 7×.

#### 5. Translation unit granularity and project settings

- **DO** group cohesive, small command implementations into consolidated
  translation units (such as
  [Commands/ViewCameraCommands.cpp](src/Commands/ViewCameraCommands.cpp)) rather
  than creating dozens of 20-line single-method `.cpp` files.
- **DO** keep `<MultiProcessorCompilation>true</MultiProcessorCompilation>`
  (`/MP`), `WIN32_LEAN_AND_MEAN`, `NOMINMAX`, and `/Zc:__cplusplus` enabled in
  both `Debug|x64` and `Release|x64`.
- **DON'T** remove the MSBuild `Inputs`/`Outputs` incremental guards on
  `RecordInteropNativeSdk` in
  [src/NativeSdkReceipt.targets](src/NativeSdkReceipt.targets) or revert
  header-stamp hashing in
  [tools/Write-NativeSdkReceipt.ps1](tools/Write-NativeSdkReceipt.ps1) to
  full-file `Get-FileHash` SHA-256 over all SDK headers.
  - _Why:_ Hashing all ~830 SDK header files with SHA-256 on every MSBuild
    evaluation adds ~2 seconds to every incremental build; metadata hashing
    (relative path + byte length + UTC write timestamp) with MSBuild
    `Inputs`/`Outputs` reduces up-to-date receipt checks to ~30 ms while
    retaining SHA-256 verification on the binary `.lib`/`.dll` SDK inputs.

## Related documentation

- [Live engine sync](../../design/editor/lld/live-engine-sync.md).
- [Viewport and tools](../../design/editor/lld/viewport-and-tools.md).
- [Property pipeline](../../design/editor/lld/property-pipeline.md).
- [Implementation and validation status](../../design/editor/IMPLEMENTATION_STATUS.md).

## License

Distributed under the 3-Clause BSD License; see the source file headers.

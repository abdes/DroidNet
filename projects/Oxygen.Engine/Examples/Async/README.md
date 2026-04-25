# Async Example

This small C++ example demonstrates asynchronous programming patterns used by the Oxygen Engine and includes a compact rendering demo driven by the engine's AsyncEngine. The example activates platform and graphics backends, registers engine modules (input, renderer, and the example's `MainModule`) and shows multi-phase frame execution with a camera drone, animated geometry and an optional ImGui-based debug overlay.

## Purpose

- Show how the engine schedules and runs small background/async tasks
- Demonstrate the interplay between the example’s runtime loop and asynchronous work
- Provide a compact, standalone area to prototype async patterns without opening the full engine/editor

## Phase 4 Migration Gate

`Examples/Async` is the Phase 4 first-success integration gate for the Vortex
migration program. The Phase 4 artifact roots for this example are:

- `build/artifacts/vortex/phase-4/async/reference/` — legacy/reference baseline
  captured by `Capture-AsyncLegacyReference.ps1`
- `build/artifacts/vortex/phase-4/async/current/` — current Vortex capture
  produced by `Run-AsyncRuntimeValidation.ps1`

This `04-05` seam-migration lane replaces only the initial runtime seams:

- legacy renderer target linkage
- Async-owned renderer bootstrap ownership
- example-local view-registration assumptions that now move behind a
  Vortex-owned renderer seam

This README preserves the original `04-05` seam boundary truthfully:
DemoShell/AppWindow runtime-UI migration plus the parity/composition proof-pack
closeout were completed later by `04-11`, `04-09`, and `04-06` on the same
artifact root.

`Examples/Async` remains the integration gate, while `VortexBasic` remains the
continuing systems-and-passes validation surface for Phase 4.

## Phase 4 Proof Pack

`04-09` owns the migrated Async parity and harness baseline under
`build/artifacts/vortex/phase-4/async/`. `04-06` reuses that same artifact pack
for composition/presentation closeout; it does not create a second capture
lane.

Composition-specific retained boundaries on the migrated path:

- `ResolveSceneColor` remains the Stage 21 owner when the main scene view needs
  a retained resolved handoff. It snapshots `ResolvedSceneColor` and
  `ResolvedSceneDepth` before Stage 22 consumes the scene signal.
- `PostRenderCleanup` remains the Stage 23 extraction/handoff owner. It
  snapshots `PrevSceneDepth` and `PrevVelocity` after Stage 22 completes, then
  finalizes the history handoff without taking presentation ownership.
- Final presentation stays on the retained `Renderer` compositing/present path.
  The Async proof pack inspects that path through the capture and products
  reports in the `current/` artifact root.

To capture the legacy/reference baseline:

```powershell
powershell -NoProfile -File tools/vortex/Capture-AsyncLegacyReference.ps1 `
  -Output build/artifacts/vortex/phase-4/async/reference
```

To re-run the current Vortex validation against the reference:

```powershell
powershell -NoProfile -File tools/vortex/Run-AsyncRuntimeValidation.ps1 `
  -Output build/artifacts/vortex/phase-4/async/current `
  -ReferenceRoot build/artifacts/vortex/phase-4/async/reference
```

To re-run just the composition closeout proof on an existing capture:

```powershell
powershell -NoProfile -File tools/vortex/Verify-AsyncRuntimeProof.ps1 `
  -CapturePath build/artifacts/vortex/phase-4/async/current/current_renderdoc.rdc `
  -ReferenceRoot build/artifacts/vortex/phase-4/async/reference
```

Successful composition/presentation closeout keeps these checks green in the
generated reports:

- `async_runtime_stage_order_valid=true`
- `compositing_scope_present=true`
- `stage22_tonemap_scope_present=true`
- `final_present_nonzero=true`
- `final_present_vs_tonemap_changed=true`

These artifact checks are necessary but not sufficient for Phase 4 closeout.
The migrated Async demo now also carries a debugger-backed `04-06` audit on the
same runtime path. The clean closeout expectation is:

- `cdb` reaches the Async `exit code: 0` line
- no `D3D12 WARNING` or `D3D12 ERROR` lines appear in
  `build/artifacts/vortex/phase-4/async/04-06.debug-layer.cdb.log`
- the only accepted debugger-only shutdown noise is the documented
  `DXGI WARNING: Live IDXGIFactory ...` line

ImGui / DemoShell overlay correctness is tracked in
`build/artifacts/vortex/phase-4/async/baseline_behaviors.md`. The required
source-audit lines are:

- `imgui_runtime_registered: pass`
- `demoshell_ui_draw_path: pass`

The retained debugger-backed audit artifacts live alongside the proof pack:

- `build/artifacts/vortex/phase-4/async/04-06.debug-layer.cdb.log`
- `build/artifacts/vortex/phase-4/async/04-06.debug-layer.report.txt`

To rerun the debugger-backed audit:

```powershell
cdb.exe -G -g `
  -logo build/artifacts/vortex/phase-4/async/04-06.debug-layer.cdb.log `
  -cf build/artifacts/vortex/phase-4/async/04-06.debug-layer.cdb.commands.txt `
  out/build-ninja/bin/Debug/Oxygen.Examples.Async.exe `
  --frames 14 --fps 30 --vsync false --debug-layer true --capture-provider off
```

## Files

- `CMakeLists.txt` — build configuration, target is `Oxygen.Examples.Async`
- `main_impl.cpp` — program entry; builds the CLI, creates platform / graphics / async engine instances and coordinates startup/shutdown
- `MainModule.cpp` / `MainModule.h` — graphics demo module: scene creation (animated spheres and a two-submesh quad), camera drone spline, per-phase rendering handlers, and ImGui debug panels

## Requirements (Windows-focused)

- Windows 10 / Windows 11 development environment
- Visual Studio 2022 or newer with the "Desktop development with C++" workload
  - MSVC toolset (v143 or newer) is recommended
- CMake 3.29+
- C++23 language support (this example uses cxx_std_23)

## Build (Windows, Visual Studio / MSVC)

This example is part of the Oxygen engine and is easiest to build from the engine root so CMake can configure engine modules correctly.

From the `Oxygen.Engine` root (PowerShell):

```powershell
# Configure & build (release preset):
cmake --preset windows-release
cmake --build --preset windows-release --target oxygen-examples-async

# Configure & build (debug preset):
cmake --preset windows-debug
cmake --build --preset windows-debug --target oxygen-examples-async
```

## Run

After building the example the binary will be placed in the build output folder (for example `out\build\bin\Release\Oxygen.Examples.Async.exe` when using the Visual Studio generator). Note: the CMake target name used by the build system is `oxygen-examples-async` (lowercase, hyphenated). The output executable name is `Oxygen.Examples.Async.exe` (as set by the target's OUTPUT_NAME).

No external runtime libraries are required beyond what the engine build produces. On Windows ensure the engine runtime DLLs (if any) are discoverable via PATH or in the same folder as the executable.

## Command-line arguments

The example exposes a small, focused CLI (program name `async-sim`, version `0.1`). Use `--help` or `-h` for full usage.

- `-f, --frames <count>` — Number of frames to simulate (default: `0` = run until exit)
- `-r, --fps <rate>` — Target frames-per-second for the engine pacing loop (default: `100`)
- `-d, --headless` — Run the example without creating a visible window (default: `false`)
- `-F, --fullscreen` — Start window in fullscreen (boolean, default: `false`)
- `-s, --vsync` — Enable vertical-sync (boolean, default: `true`)

Examples:

```powershell
# Interactive: create window, run until closed
.\out\build\bin\Release\Oxygen.Examples.Async.exe

# Headless: run a short simulation for 100 frames and exit
.\out\build\bin\Release\Oxygen.Examples.Async.exe -f 100 -d

# Run with a fixed target FPS, disable vsync
.\out\build\bin\Release\Oxygen.Examples.Async.exe -r 60 --vsync false
```

When run headless the example still initializes the engine and many subsystems but will use the headless graphics backend and skip creating a visible window and ImGui module (useful for automated runs and CI).

## Implementation notes

- The program parses CLI options and constructs an `AsyncEngineApp` (see `../Common/AsyncEngineApp.h`) which aggregates platform, engine and graphics objects.
- During startup the example:
  - Activates the platform and graphics subsystems
  - Creates `AsyncEngine` with configured target FPS/frame count
  - Registers engine modules: `InputSystem`, example `MainModule` (graphics main module), `Renderer` and — when not headless — an ImGui module
  - `MainModule` sets up procedural assets (sphere LODs and a two-submesh quad), camera drone paths, input bindings, and debug GUI panels for profiling and frame inspection

- The engine uses structured concurrency via `oxygen::OxCo` primitives (nursery / co::Run) to coordinate lifetime and shutdown.

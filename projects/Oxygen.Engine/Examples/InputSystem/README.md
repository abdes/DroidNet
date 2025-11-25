# InputSystem Example

This example demonstrates the input handling code for the Oxygen.Engine project. It's a small C++ example laid out as a CMake project so you can build and run it as a standalone app to experiment with input handling.

## Purpose

- Show how the engine receives and processes input events
- Provide a minimal, standalone example demonstrating core input-related code in the engine
- Offer a small playground to prototype input features without opening the full editor or engine

## Files

- `CMakeLists.txt` — build description for the example
- `main_impl.cpp` — example program entry and small runner code (wires up input logic)
- `MainModule.cpp` / `MainModule.h` — main module implementing the example's core behavior

Additional files:

- `print_sdl_code_names.cpp` — small utility used for printing key / scancode diagnostic names. NOTE: this example does not require an external SDL dependency — it only uses Oxygen.Engine runtime support. The example is Windows-only.

## Requirements (Windows-only)

- Windows 10 / Windows 11 development environment
- Visual Studio 2022 or newer with the "Desktop development with C++" workload
  - MSVC toolset (v143 or newer) is required — this example is built and tested with the Microsoft C++ toolchain
- C++23 language support (the engine's Input module requires C++23 via CMake's cxx_std_23)
- CMake (3.29+ required)
- This example does not depend on an external SDL library — input functionality is provided by the Oxygen Engine runtime and engine-provided input shims.

## Build (Windows, Visual Studio / MSVC)

This example is part of the Oxygen engine project; the recommended way to build it is from the `Oxygen.Engine` project root so CMake can configure engine modules and dependencies correctly.

From the `Oxygen.Engine` root (PowerShell):

```powershell
# Configure + build using repository CMake presets (recommended)
# Use 'windows-release' for an optimized build and 'windows-debug' for debug builds.

# Configure & build (release preset):
cmake --preset windows-release
cmake --build --preset windows-release --target Oxygen.Examples.InputSystem

# Configure & build (debug preset):
cmake --preset windows-debug
cmake --build --preset windows-debug --target oxygen-examples-inputsystem
```

## Run

After building with the top-level workflow above the executable will be placed in the build output folder (for example `out\build\bin\Release\Oxygen.Examples.InputSystem.exe` when using the Visual Studio generator). Run it from a terminal or double-click the binary.

No external runtime libraries are required for this example beyond what the Oxygen Engine build provides. On Windows make sure the engine's runtime DLLs (if any) are discoverable in your PATH or in the same folder as the built binary.

## Command-line arguments

The example exposes a small set of handy CLI switches (the program's help text uses `async-sim` as the program name):

- `-f, --frames <count>` — Number of frames to run / simulate. Default: `0` (no frame limit — run until window close).
- `-r, --fps <rate>` — Target frames per second (frame pacing). Default: `100`.
- `-d, --headless` — Run in headless mode (no window / GUI). Default: `false`.
- `-F, --fullscreen` — Start the example in full-screen mode. Default: `false`.
- `-s, --vsync` — Enable vertical-sync (limit FPS to monitor refresh rate). Default: `true`.

Example invocations:

```powershell
# Run interactive with GUI (default):
.\out\build\bin\Release\Oxygen.Examples.InputSystem.exe

# Run for a fixed small simulation (100 frames) without a window:
.\out\build\bin\Release\Oxygen.Examples.InputSystem.exe -f 100 -d

# Run with a specific target FPS and disable vsync:
.\out\build\bin\Release\Oxygen.Examples.InputSystem.exe -r 60 --vsync false
```

## Notes

- This example is intentionally small — it's intended as a learning and prototyping aid rather than a production sample.
- If you want to adapt the example for debugging input handling in the engine, add logging or breakpoints around `MainModule` and `main_impl.cpp` where events are handled.

## Contributing

If you add features to this example or discover missing steps, please update this README and add notes so others can reproduce your workflow.

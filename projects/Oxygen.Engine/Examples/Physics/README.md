# Physics Example

A comprehensive demonstration of end-to-end physics integration in the **Oxygen Engine**, featuring a procedural "Ramp Gauntlet to Bowl" scenario.

## Overview

This example showcases how scene content, rigid bodies, and runtime simulation are seamlessly integrated. It serves as a standalone validation harness for the `PhysicsModule` and `ScenePhysics` components, providing a compact playground to tune impulses, obstacle behavior, and settlement logic.

The demo builds a dynamic scene from generated primitives:

- **Inclined Ramp**: A starting platform for dynamic objects.
- **Dynamic Obstacles**: Various static and dynamic cubes to test collisions.
- **Hinged Flippers**: Interactive elements to demonstrate joint and constraint behavior.
- **Settlement Bowl**: A collection area at the bottom to verify stable settlement and resting states.

## Project Structure

- `CMakeLists.txt`: Build configuration for the example.
- `main_impl.cpp`: Application entry point and module orchestration using the `OxCo` concurrency framework.
- `MainModule.cpp` / `.h`: Scenario setup, procedural scene generation, and runtime simulation logic.
- `PhysicsDemoPanel.cpp` / `.h`: ImGui-based interface providing live telemetry and scenario controls.
- `demo_settings.json`: Configuration overrides for the demo shell environment.

## Requirements

- **Operating System**: Windows 10 / 11
- **Compiler**: Visual Studio 2022 (MSVC v143 or newer)
- **Language Standard**: C++23
- **Tools**: CMake 3.29+ and Conan 2
- **Graphics**: Direct3D 12 compatible hardware

## Building the Example

This example is integrated into the Oxygen Engine workspace. It is recommended to build it from the repository root using the provided CMake presets.

From the `Oxygen.Engine` root directory:

```powershell
# 1. Configure the project using the repository presets
cmake --preset windows-default

# 2. Build the Physics Example target
# For a Debug build:
cmake --build --preset windows-debug --target Oxygen.Examples.Physics

# For a Release build:
cmake --build --preset windows-release --target Oxygen.Examples.Physics
```

## Running the Example

Once built, the executable is located in the build output directory corresponding to your selected configuration.

### Executable Paths

- **Debug Build**: `.\out\build-ninja\bin\Debug\Oxygen.Examples.Physics.exe`
- **Release Build**: `.\out\build-ninja\bin\Release\Oxygen.Examples.Physics.exe`

### Command-Line Arguments

The application supports several switches to configure the simulation environment:

- `-f, --frames <count>`: Number of frames to simulate. Defaults to `0` (runs indefinitely).
- `-r, --fps <rate>`: Target frames per second for event loop pacing. Default is `100`.
- `-d, --headless`: Run in headless mode without a window or GUI. Default is `false`.
- `-F, --fullscreen`: Start the application in full-screen mode. Default is `false`.
- `-s, --vsync`: Enable vertical synchronization. Default is `true`.

**Example Usage:**

```powershell
# Run the optimized Release build with default settings
.\out\build-ninja\bin\Release\Oxygen.Examples.Physics.exe

# Run a headless simulation for 1000 frames at 60 FPS
.\out\build-ninja\bin\Release\Oxygen.Examples.Physics.exe --headless --frames 1000 --fps 60
```

## Features and Diagnostics

- **Real-Time Telemetry**: Monitor active bodies, contact manifolds, and simulation timing via the on-screen ImGui panel.
- **Interactive Controls**: Manually spawn objects or reset the scenario to test different physical interactions.
- **Bindless Architecture**: Demonstrates the engine's 100% bindless rendering approach even for procedural geometry.
- **Async Simulation**: Leverages `OxCo` to run physics updates and engine logic asynchronously for maximum performance.

## Notes

- This example is focused on validating physical behavior and scene hydration contracts rather than visual fidelity.
- To inspect the simulation flow or add custom obstacles, refer to the scene construction logic in `MainModule.cpp`.

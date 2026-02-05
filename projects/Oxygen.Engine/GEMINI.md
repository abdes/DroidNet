# Oxygen Engine Project Context

This directory contains the source code for **Oxygen Engine**, a modular, high-performance C++ game engine designed with a modern bindless rendering architecture.

## Project Overview

- **Main Technologies:** C++23, CMake (3.29+), Conan 2, D3D12, Vulkan (planned), SDL3, ImGui, Asio.
- **Architecture:**
    - **Modular Design:** The engine is split into functional modules (Base, Core, OxCo, Graphics, Renderer, etc.) using a custom CMake-based module system.
    - **Bindless Rendering:** A core architectural feature where resources are accessed in shaders via stable indices in global descriptor heaps, minimizing pipeline state changes.
    - **OxCo Concurrency:** A custom coroutine and concurrency framework built on top of Asio, providing async/await capabilities, nurseries, and channels.
- **Key Dependencies:**
    - `fmt`: Modern string formatting.
    - `glm`: OpenGL Mathematics for graphics.
    - `asio`: Network and low-level I/O, used as the backbone for `OxCo`.
    - `imgui`: Immediate Mode GUI for tools and debugging.
    - `nlohmann_json`: JSON support.

## Project Structure

- `src/Oxygen/`: Main engine source code.
    - `Base/`: Fundamental utilities (logging, signals, state machines).
    - `Core/`: Core engine types (math, time, transforms, frame context).
    - `OxCo/`: Coroutine-based concurrency framework.
    - `Graphics/`: Hardware abstraction layer and descriptor management.
    - `Renderer/`: High-level rendering logic and pipeline management.
    - `Engine/`: Main engine loop and high-level systems.
- `Examples/`: Demonstration projects and samples.
- `design/`: Architectural documentation and implementation plans.
- `tools/`: Build scripts and code generation tools (e.g., bindless codegen).

## Building and Running

The project uses CMake with Conan for dependency management.

### Prerequisites

- **Visual Studio 2022** with "Desktop development with C++".
- **Python** with a virtual environment (`.venv`).
- **Conan 2** installed and configured.
- **Latest VC Redistributables** (required due to STL mutex issues in recent VS versions).

### Build Commands

   The project uses CMake presets.
   ```powershell
   cmake --build --preset windows-debug
   ```

### Output Locations
- **Builds:** `out/build-ninja/`, `out/build-vs/`, `out/build-asan-ninja/`.
- **Install/Deploy:** `out/install/Debug/`, `out/install/Release/`, `out/install/Asan/`.

## Development Conventions

- **C++ Standard:** Strictly C++23.
- **Naming:** Follows existing patterns (typically PascalCase for types/files, camelCase for variables/functions).
- **No RTTI:** The engine is built with `/GR-` (no RTTI). Use alternative mechanisms (like `EngineTag` or custom type systems).
- **Logging:** Uses `loguru` (wrapped in `Oxygen.Base`). Use the logging macros provided in `Logging.h`.
- **Formatting:**
    - C++: `.clang-format` (Clang-format).
    - CMake: `.gersemirc` (Gersemi).
    - Markdown: `.markdownlint.yaml`.
- **Error Handling:** Prefers `Result<T>` or exceptions where appropriate (ASAN issues with exceptions in some contexts).
- **Bindless Resources:** The engine is 100% bincless rendering. Always register resources with the `ResourceRegistry` to obtain stable indices for shader access.
- **Engine Conventions:** The engine is +Z UP, -Y forward, and all the conventions and constants for complying with them are in `src/Oxygen/Core/Constants.h`

## Key Files for Reference

- `conanfile.py`: Dependency definitions and build options.
- `CMakeLists.txt`: Root build configuration.
- `src/Oxygen/Base/Logging.h`: Main logging entry point.
- `src/Oxygen/OxCo/Co.h`: Coroutine framework entry point.
- `design/BindlessRenderingDesign.md`: Detailed explanation of the rendering architecture.
- `src/Oxygen/Core/Constants.h`: Engine space/directions conventions.

# Copilot Instructions for Oxygen Engine

Provide concise, expert guidance for modern C++ and engine development. When answering:

- Give a clear, direct response and the essential rationale.
- Show a short, actionable plan or steps the reader can follow.
- Offer one or two viable alternatives with trade-offs when appropriate.

Evaluate claims and assumptions, ask a focused clarification question only when necessary, and prefer concrete, testable recommendations over vague advice.

## Architecture & Major Patterns
- **Modular Subsystems**: Graphics, Scene, Data, Renderer, etc., each with a clear API and local `README.md` (see `src/Oxygen/Graphics/README.md`).
- **Bindless Rendering**: All GPU resources are accessed via global indices, not per-draw bindings. See `design/BindlessArchitecture.md`, `BindlessRenderingDesign.md`, and `BindlessRenderingRootSignature.md` for rationale, API, and HLSL/D3D12/GLSL patterns. Shaders use global indices for all resource access.
- **Pipeline State Objects (PSO)**: Immutable, hashable pipeline state abstractions. See `design/PipelineDesign.md` for how PSOs, root signatures, and bindless tables are created and managed. Backend-specific caches (e.g., D3D12) are used for efficient reuse.
- **Render Pass System**: Modular, coroutine-based passes (Forward+, deferred, post-processing, UI) orchestrated by the Renderer. See `design/RenderPassesDesign.md` for pass types, resource transitions, and orchestration.
- **Scene System**: High-performance, hierarchical scene graph with handle/view pattern, resource-table storage, and component-based nodes. See `design/SceneSystemDesignAnalysis.md` for architecture, flags, traversal, and query patterns.
- **Coordinator/Maestro**: Orchestrates frame events, synchronization, and cross-subsystem communication.
- **Command, Factory, Pool Patterns**: Used for render operations, resource creation, and memory management.
- **PIMPL Idiom**: Used for ABI stability and encapsulation (see `Clap/Internal/Args.h`).
- **Coroutine-Driven Frame Execution**: Frame logic is asynchronous, with explicit suspension/resumption points for GPU/CPU sync.

## Developer Workflows
- **Build**: Use CMake and Conan. See `README.md` for Conan setup and build commands. Example:
  ```sh
  conan install . --profile:host=profiles/windows-msvc-asan.ini --profile:build=profiles/windows-msvc-asan.ini --output-folder=out/build --build=missing --deployer=full_deploy -s build_type=Debug
  ```
- **Compiler**: Visual Studio 2022 with "Desktop development with C++" workload is required.
- **Pre-commit Formatting**: Use provided PowerShell commands for clang-format and CMake formatting (see `README.md`).
- **Testing**: All tests use Google Test, included via `Oxygen/Testing/GTest.h`. Tests follow scenario-based, AAA-pattern, and are grouped by functionality in anonymous namespaces.

## Coding & Documentation Conventions
- **C++20 Only**: Use modern C++20 features; avoid legacy patterns.
- **Naming**: Follows Google C++ Style Guide with project-specific tweaks (see `.github/instructions/cpp_coding_style.instructions.md`).
- **Doc Comments**: Use `//!` and `/*! ... */` Doxygen style, with strict placement and formatting rules (see `.github/instructions/doc_comments.instructions.md`).
- **Unit Tests**: Must use scenario-based names, AAA pattern, and custom macros for assertions (see `.github/instructions/unit_tests.instructions.md`).

## Integration & External Dependencies
- **Graphics**: Modular backends for Vulkan and D3D12, with clear abstraction boundaries.
- **Shader Compilation**: Uses external tools (DXC, SPIRV-Cross, shaderc) for cross-platform shader compilation.
- **Logging**: Centralized via `Oxygen/Base/Logging.h`.
- **fmtlib**: Used for formatting, including custom formatters for engine types.

## Project-Specific Patterns & Examples
- **Bindless Resource Management**: All GPU resources are registered and accessed via a global index system. See `src/Oxygen/Graphics/Common/DescriptorAllocator.h`, `ResourceRegistry.h`, and design docs for allocator, handle, and registry patterns. Shaders use global indices for all resource access.
- **Render Passes**: Each pass is a coroutine-based component, with explicit resource dependencies and state transitions. See `design/RenderPassesDesign.md` for pass orchestration and resource management.
- **Scene Queries**: Use the `SceneQuery` API for high-performance, path-based, and batch scene graph queries (see `SceneQuery.h/cpp`). Path-based queries support wildcards and batch execution for performance.
- **Error Handling**: Diagnostic and error messages are constructed with context, using helper functions in `Clap/Internal/Errors.cpp`.
- **Component/Handle Patterns**: Scene nodes, resources, and descriptors use handle/view and component-based patterns for safety and extensibility.
- **Pipeline/Root Signature**: All shaders and pipelines must match the engine's root signature conventions for bindless access. See `BindlessRenderingRootSignature.md` and `PipelineDesign.md` for details.

## When in Doubt or New to a Module
- Check for a `README.md` in the relevant subsystem directory.
- Check for a doc file in `Docs` subdirectory of the relevant subsystem directory.
- Review `.github/instructions/*.md` and `design/*.md` for enforced style, documentation, and architecture rules.
- Follow the patterns in existing code and tests for consistency.

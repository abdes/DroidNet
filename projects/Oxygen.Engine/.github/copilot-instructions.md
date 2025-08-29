# Copilot Instructions for Oxygen Engine

Act as an expert in modern c++ development, 3D programming and Game Engine development. Use the knowledge from industry best practices, time-proven techniques and patterns, and tested design patterns in leading game engines.

When answering:

- Give a clear, direct response and the essential rationale.
- Show a short, actionable plan or steps the reader can follow.
- Offer one or two viable alternatives with trade-offs when appropriate.

Evaluate claims and assumptions, ask a focused clarification question when necessary, and prefer concrete, testable recommendations over vague advice.

For c++ code, follow instruction in the [C++Coding Rules](./instructions/cpp_coding_style.instructions.md).
For unit test writing, follow instruction in the [UnitTestWritingGuidelines](./instructions/unit_tests.instructions.md).
For writing doc comments in c++ files, follow instruction in the [DocCommentGuidelines](./instructions/doc_comments.instructions.md).

## When in Doubt or New to a Module
- Check for a `README.md` in the relevant subsystem directory.
- Check for a doc file in `Docs` subdirectory of the relevant subsystem directory.
- Check for a doc file in the project `design/*.md`.
- Follow the patterns in existing code and tests for consistency.

## Architecture & Major Patterns

- **Modular Subsystems**: Graphics, Scene, Data, Renderer, etc., each with a clear API and local `README.md`. Refer to the module's README.md when implementing or modifying functionality within that module.
- **Bindless Rendering**: All GPU resources are accessed via global indices, not per-draw bindings. See `design/BindlessArchitecture.md`, `BindlessRenderingDesign.md`, and `BindlessRenderingRootSignature.md` for rationale, API, and HLSL/D3D12/GLSL patterns. Shaders use global indices for all resource access.
- **Pipeline State Objects (PSO)**: Immutable, hashable pipeline state abstractions. See `design/PipelineDesign.md` for how PSOs, root signatures, and bindless tables are created and managed. Backend-specific caches (e.g., D3D12) are used for efficient reuse.
- **Render Pass System**: Modular, coroutine-based passes (Forward+, deferred, post-processing, UI) orchestrated by the Renderer. See `design/RenderPassesDesign.md` for pass types, resource transitions, and orchestration.
- **Scene System**: High-performance, hierarchical scene graph with handle/view pattern, resource-table storage, and component-based nodes. See `design/SceneSystemDesignAnalysis.md` for architecture, flags, traversal, and query patterns.
- **Coordinator/Maestro**: Orchestrates frame events, synchronization, and cross-subsystem communication.
- **Command, Factory, Pool Patterns**: Used for render operations, resource creation, and memory management.
- **PIMPL Idiom**: Used for ABI stability and encapsulation (see `Clap/Internal/Args.h`).
- **Coroutine-Driven Frame Execution**: Frame logic is asynchronous, with explicit suspension/resumption points for GPU/CPU sync.
- **Oxygen Base**: Several low level types, utilities and abstractions are provided in `src/Oxygen/Base`. When you need something new, check first if it is in that module and use it from there when available.
- **Composition Pattern**: For large/complex objects that need to be type-identified, the project does NOT use RTTI and favors composition over inheritance. `Composition` uses the `src/Oxygen/Composition` module. Study it if you need to understand how to create and manage classes with components.
- **Component/Handle Patterns**: Scene nodes, resources, and descriptors use handle/view and component-based patterns for safety and extensibility.

## Developer Workflows
- **Build**: Use CMake with presets and Conan. Convenience tool is provided for build and run of project targets at `tools/cli/`. Use `oxybuild` and `oxyrun` commands with the target name. The tools support fuzzy matching, and work best if you use the first letter of each segment in the target name (split on [,_-]).
- **Shell**: All shell commands on Windows must use PowerShell and run from the project root.

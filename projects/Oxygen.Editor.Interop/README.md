# Oxygen.Editor.Interop

## Overview

The **Oxygen.Editor.Interop** project provides a C++/C# interoperability layer that bridges the native Oxygen Engine with managed .NET editor applications. It enables the Oxygen Editor to communicate with the native rendering and world management components of the Oxygen Engine through a well-defined managed interface.

This is a mixed-mode C++/CLI project that exposes native engine functionality to .NET consumers while maintaining clean separation of concerns between the native and managed code.

## Technology Stack

- **Language:** C++ (C++/CLI for managed interop) and C#
- **Platform:** Windows (x64)
- **Target Framework:** .NET 9.0 (managed), C++/CLI with C++20
- **Build System:** MSBuild / CMake (Premake5)
- **Key Dependencies:**
  - `fmt` – C++ formatting library
  - `glm` – Graphics math library
  - Oxygen.Engine (native C++ game engine)
  - Microsoft.WindowsAppSDK (for Windows integration)
- **Testing:** MSTest for managed tests

## Project Architecture

### Core Components

The interop layer consists of three main namespaces and classes:

#### 1. **EngineRunner** (`Oxygen::Editor::EngineInterface::EngineRunner`)

- Main entry point for engine lifecycle management
- Handles native engine initialization and configuration
- Manages logging subsystem integration
- Provides sealed managed wrapper around native engine instance
- **Key Methods:**
  - `ConfigureLogging()` – Set up native logging without or with managed ILogger integration
  - `CreateEngine()` – Instantiate and initialize the native engine
  - Deterministic cleanup via destructor/finalizer

#### 2. **EngineContext** (`Oxygen::Editor::EngineInterface::EngineContext`)

- Represents a valid native engine instance
- Provides safe handle to native context with validity checks
- Supports copy semantics (sharing underlying native resource)
- Ensures deterministic cleanup when managed reference is released

#### 3. **OxygenWorld** (`Oxygen::Interop::World::OxygenWorld`)

- Encapsulates world/scene management functionality
- Bridges editor world operations to native engine representation

### Design Patterns

- **C++/CLI Managed Refs:** All public types are `ref class` with proper deterministic cleanup (destructor + finalizer pattern)
- **Config Bridging:** Native configuration structures (`PlatformConfig`, `GraphicsConfig`, `EngineConfig`, `RendererConfig`) are wrapped or marshalled from managed equivalents
- **Logging Forwarding:** Optional integration with .NET `ILogger` for unified log capture
- **Header Isolation:** Public headers (`EngineRunner.h`, `EngineContext.h`) avoid exposing native logging implementation details

### WinUI 3 Integration

The interop layer handles the specific requirements for hosting the DirectX 12 engine within a WinUI 3 `SwapChainPanel`.

- **Separation of Concerns:** The core `Oxygen.Engine` remains unaware of WinUI 3 specifics. It creates a `CompositionSurface` with a standard `IDXGISwapChain`.
- **Interop Responsibility:** `EngineRunner` acts as the bridge. It accepts the `SwapChainPanel` pointer, retrieves the swap chain from the engine, and performs the necessary COM interface queries to connect them.
- **ISwapChainPanelNative:** The project explicitly defines and uses the WinUI 3 specific IID (`{63BE0B4D-909D-4652-9C00-5C3EA4763E52}`) for `ISwapChainPanelNative`, which differs from the UWP version.
- **Threading Model:** The `SetSwapChain` operation is performed within the interop layer. Consumers must ensure `CreateEngine` is called from a thread compatible with the `SwapChainPanel` (typically the UI thread).

## Project Structure

```text
Oxygen.Editor.Interop/
├── src/
│   ├── EngineRunner.h / .cpp      # Engine lifecycle and logging configuration
│   ├── EngineContext.h / .cpp     # Native engine instance wrapper
│   ├── OxygenWorld.h / .cpp       # World/scene management interface
│   ├── Config.h / .cpp            # Configuration structure bridging
│   ├── AssemblyInfo.cpp           # Managed assembly metadata
│   ├── vcpkg.json                 # vcpkg dependency manifest
│   └── Base/LoguruWrapper.h       # Logging utility wrapper
├── test/
│   ├── EngineTests.cs             # Managed test suite for interop
│   └── Oxygen.Editor.Interop.Tests.csproj
├── EditorInterop.sln              # Solution file for this interop project
├── premake5.lua                   # Premake build configuration
└── README.md
```

## Getting Started

### Prerequisites

- **Oxygen Engine Installation:** The interop project assumes the Oxygen.Engine build produces a complete installation at:

  ```text
  $(ProjectRoot)\Oxygen.Engine\out\install
  ```

  Ensure this directory is populated with all necessary headers and libraries.

- **PATH Configuration:** Add the directory containing Oxygen Engine DLLs to your system PATH:

  ```powershell
  $env:PATH += ";$(ProjectRoot)\Oxygen.Engine\out\install\bin"
  ```

- **.NET 9.0 SDK:** Required for building managed components and tests
- **C++ Build Tools:** MSVC v145 (Visual Studio 2022) toolset for C++/CLI compilation
- **vcpkg:** For native dependency management (fmt, glm)

### Building

From the interop project directory:

```powershell
# Build the C++/CLI interop library
dotnet build .\src\Oxygen.Editor.Interop.vcxproj

# Or use the solution
dotnet build .\EditorInterop.sln
```

To use Premake for build configuration:

```powershell
premake5 vs2022
```

### Running Tests

Tests verify the managed interface contracts and engine initialization paths:

```powershell
# Run all interop tests
dotnet test .\test\Oxygen.Editor.Interop.Tests.csproj

# Run with verbose logging
dotnet test .\test\Oxygen.Editor.Interop.Tests.csproj --logger "console;verbosity=detailed"
```

## Key Features

- **Managed Engine Wrapper:** Expose native Oxygen Engine through managed ref classes (`EngineRunner`, `EngineContext`)
- **Flexible Logging Configuration:** Support native-only logging or forwarding to .NET `ILogger`
- **Config Structure Bridging:** Marshal between native and managed configuration types
- **World/Scene Management:** Provide managed interface to world operations
- **Deterministic Cleanup:** Proper destructor/finalizer discipline for unmanaged resource safety
- **Type Safety:** Strong typing across C++/CLI boundaries with compile-time validation

## Development Workflow

### Adding New Engine Features

1. **Define new managed interface** in a header under `src/` (e.g., `NewFeature.h`)
2. **Implement using C++/CLI** ref classes that wrap native functionality
3. **Add configuration or context methods** to `EngineRunner` or `EngineContext` as appropriate
4. **Write managed tests** in `test/EngineTests.cs` to verify the interop boundary
5. **Update docstrings** with XML comments for IntelliSense integration

### Modifying Configuration

1. Edit bridging code in `Config.h` / `Config.cpp` to add new config fields
2. Update both native and managed configuration struct definitions
3. Add marshalling/conversion logic if needed
4. Verify tests still pass with `dotnet test`

## Coding Standards

This project follows the DroidNet repository conventions:

- **C++:** Modern C++20 with MSVC v145
- **C#:** C# 13 preview conventions (see `.github/instructions/csharp_coding_style.instructions.md`)
- **Access Modifiers:** Always explicit (`public`, `private`, `protected`)
- **Instance Members:** Use `this.` prefix for clarity in C#
- **XML Documentation:** All public methods must have XML doc comments for IntelliSense
- **Naming:** PascalCase for managed types/methods, snake_case for native types (following Oxygen.Engine conventions)
- **Determinism:** All ref classes must have proper destructor + finalizer for resource cleanup

## Testing

Tests are implemented in **MSTest** format and follow the **AAA pattern** (Arrange-Act-Assert):

```csharp
[TestClass]
public sealed class EngineTests
{
    private EngineRunner runner = null!;

    [TestInitialize]
    public void Setup() => this.runner = new EngineRunner();

    [TestCleanup]
    public void Teardown() => this.runner.Dispose();

    [TestMethod]
    public void CreateEngine_Succeeds_ReturnsValidContext()
    {
        // Arrange
        var cfg = new EngineConfig();

        // Act
        var ctx = this.runner.CreateEngine(cfg);

        // Assert
        Assert.IsNotNull(ctx);
        Assert.IsTrue(ctx.IsValid);
    }
}
```

### Test Naming Convention

- Format: `MethodName_Scenario_ExpectedBehavior`
- Example: `CreateEngine_Succeeds_ReturnsValidContext`

Run tests with coverage:

```powershell
dotnet test .\test\Oxygen.Editor.Interop.Tests.csproj /p:CollectCoverage=true
```

## Contributing

When contributing to this interop layer:

1. Ensure all public APIs have XML documentation
2. Add corresponding managed tests for any new interop boundary
3. Verify existing tests pass: `dotnet test`
4. Follow the C# and C++ coding standards referenced above
5. Keep managed/native separation clear; avoid exposing implementation details
6. Document any new marshalling or conversion patterns for future maintainers

## License

Distributed under the 3-Clause BSD License. See `LICENSE` file for details.

---

## Related Documentation

- **Oxygen Engine:** See `projects/Oxygen.Engine/.github/copilot-instructions.md` for native engine architecture
- **DroidNet Conventions:** See `.github/copilot-instructions.md` and `.github/instructions/csharp_coding_style.instructions.md`
- **Editor Integration:** Integration patterns with Oxygen.Editor components

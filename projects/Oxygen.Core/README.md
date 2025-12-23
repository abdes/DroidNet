# Oxygen.Core

**Oxygen.Core** provides core services and utilities for the Oxygen Editor, including path finding for Oxygen projects, application data management, and input validation. Part of the DroidNet ecosystem, this library abstracts file system operations and offers a domain-specific interface for managing Oxygen project and application directories.

## Technology Stack

- **.NET:** net9.0, net9.0-windows10.0.26100.0
- **File System Abstraction:** Testably.Abstractions (for testability and cross-platform support)
- **Dependencies:** DroidNet.Config (centralized configuration and path management)
- **Testing:** MSTest 4.0, AwesomeAssertions, Moq

## Project Architecture

Oxygen.Core follows a layered, interface-driven architecture:

```text
Oxygen.Core
├── Services/
│   ├── IOxygenPathFinder (interface)
│   └── OxygenPathFinder (implementation)
├── InputValidation (static utilities)
├── Constants (application constants)
└── MathUtil (math helpers)
```

**Key Design Patterns:**

- **Abstraction over Implementation:** The `IOxygenPathFinder` interface allows dependency injection and testing without file system dependencies.
- **Delegation:** `OxygenPathFinder` delegates to a parent `IPathFinder` for common platform-specific paths, composing functionality cleanly.
- **File System Abstraction:** Uses `IFileSystem` from Testably.Abstractions for cross-platform compatibility and unit testability.

## Getting Started

### Prerequisites

- .NET 9.0 SDK or later
- The project targets Windows 10.0.26100.0 for WinUI compatibility (also provides a cross-platform net9.0 target)

### Installation

Install from NuGet:

```powershell
dotnet add package Oxygen.Core
```

### Basic Usage

#### Path Finding for Oxygen Projects

```csharp
using System.IO.Abstractions;
using DroidNet.Config;
using Oxygen.Core.Services;

// Create a base path finder (from DroidNet.Config)
var fileSystem = new FileSystem();
var basePathFinder = new PathFinder(fileSystem, new PathFinderConfig
{
    Mode = "real",
    ApplicationName = "OxygenEditor",
    CompanyName = "Oxygen"
});

// Wrap it with Oxygen-specific functionality
var pathFinder = new OxygenPathFinder(basePathFinder, fileSystem);

// Use the paths
Console.WriteLine($"Personal Projects: {pathFinder.PersonalProjects}");
Console.WriteLine($"Local Projects: {pathFinder.LocalProjects}");
Console.WriteLine($"State Database: {pathFinder.StateDatabasePath}");
```

#### File Name Validation

```csharp
using Oxygen.Core;

// Validate file names (Windows-compatible)
string fileName = "myScene.scene";
bool isValid = InputValidation.IsValidFileName(fileName);

if (!isValid)
{
    Console.WriteLine($"'{fileName}' contains invalid characters.");
}

// Invalid examples: CON, PRN, NUL, COM1, LPT1, etc. (reserved names)
// Also rejects: <>:"/\|?* and control characters
```

## Project Structure

```text
src/
  Oxygen.Core.csproj      - Main library project
  Constants.cs                    - Application constants (Company, Application)
  InputValidation.cs              - File name validation utilities
  MathUtil.cs                     - Math helper functions
  Services/
    IOxygenPathFinder.cs          - Interface for Oxygen-specific paths
    OxygenPathFinder.cs           - Implementation delegating to IPathFinder

tests/
  Oxygen.Core.Tests.csproj - Test project
  InputValidationTests.cs          - Comprehensive file name validation tests
  OxygenPathFinderTests.cs         - Path finder functionality tests
```

## Key Features

- **Oxygen Project Path Management:** Provides dedicated paths for personal and local Oxygen projects
- **Application State Management:** Manages the state database location for runtime state persistence
- **Windows File Name Validation:** Validates file names against Windows reserved names and forbidden characters using regex
- **File System Abstraction:** Uses `IFileSystem` for testability and cross-platform support
- **Composite Path Finder:** Extends base path finding with Oxygen-specific directory structures

## Development Workflow

### Building

Build the library and tests:

```powershell
# Build the main library
dotnet build projects/Oxygen.Core/src/Oxygen.Core.csproj

# Build including tests
dotnet build projects/Oxygen.Core
```

### Running Tests

```powershell
# Run all tests
dotnet test projects/Oxygen.Core/tests/Oxygen.Core.Tests.csproj

# Run with coverage reporting
dotnet test projects/Oxygen.Core/tests/Oxygen.Core.Tests.csproj `
  /p:CollectCoverage=true /p:CoverletOutputFormat=lcov
```

### Project-Specific Build

For faster iteration, build just the project file:

```powershell
dotnet build projects/Oxygen.Core/src/Oxygen.Core.csproj
```

## Coding Standards

This project follows the DroidNet repository standards:

- **Language:** C# 13 (preview features enabled)
- **Nullable:** Reference types are non-nullable by default (`nullable enable`)
- **Implicit Usings:** Enabled for cleaner code
- **Access Modifiers:** Always explicit (`public`, `private`, etc.)
- **Instance Members:** Use `this.` prefix for clarity
- **Patterns:** Follow MVVM and DI conventions from `projects/Hosting/`

See `.github/instructions/csharp_coding_style.instructions.md` for detailed style guidelines.

## Testing

Tests use **MSTest** with the AAA (Arrange-Act-Assert) pattern:

```csharp
[TestClass]
public class MyTests
{
    [TestMethod]
    [DataRow("validFileName.txt", true)]
    [DataRow("invalid<name>.txt", false)]
    public void IsValidFileName_ShouldValidateCorrectly(string fileName, bool expected)
    {
        // Arrange
        // Act
        var result = InputValidation.IsValidFileName(fileName);
        // Assert
        result.Should().Be(expected);
    }
}
```

**Test Utilities & Frameworks:**

- **MSTest:** Test framework and attributes
- **AwesomeAssertions:** Fluent assertion library for readable assertions
- **Moq:** Mocking framework for dependency mocking

See `projects/TestHelpers/` for shared testing utilities across the repository.

## Contributing

When contributing to Oxygen.Core:

1. Follow the C# coding standards (see `.github/instructions/csharp_coding_style.instructions.md`)
2. Add tests for new functionality (MSTest with AAA pattern)
3. Use explicit access modifiers and `this.` for instance members
4. Prefer composition and small, well-justified API surfaces
5. Run tests locally before committing: `dotnet test`

For architectural questions or public API additions, reference design documents in `plan/` or contact the maintainers.

## License

Oxygen.Core is distributed under the **MIT License**. See the LICENSE file in the repository root for details.

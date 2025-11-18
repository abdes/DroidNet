# DroidNet Application Bootstrapper

![Bootstrap](https://img.shields.io/badge/module-Bootstrap-informational) ![C# 13](https://img.shields.io/badge/language-C%2313-green) ![.NET 9+](https://img.shields.io/badge/.NET-9%2B-blue) ![WinUI 3](https://img.shields.io/badge/platform-WinUI%203-orange)

An opinionated, reusable bootstrapping module that manages the application initialization process for WinUI 3 applications. The Bootstrap module provides a fluent configuration API for setting up dependency injection, logging, routing, MVVM infrastructure, and other core application services.

## Project Name and Description

**DroidNet Application Bootstrapper** is part of the DroidNet WinUI/.NET ecosystem. It simplifies the startup and initialization of WinUI applications by orchestrating the configuration of multiple interdependent subsystems in a clean, predictable order.

## Technology Stack

- **Language:** C# 13 (preview features enabled)
- **Platform:** Windows (WinUI 3)
- **Target Framework:** .NET 9.0
- **Runtime Requirements:** Windows 10.0.26100.0 or later
- **Dependency Injection:** DryIoc 6.0 (preview), Microsoft.Extensions.DependencyInjection/Hosting 10.0
- **Logging:** Serilog with multiple sinks:
  - Console output
  - Debug output
  - File output
  - Expression-based filtering
  - Configuration-based settings
  - Integration with Microsoft.Extensions.Logging
- **Configuration Management:** JSON-based configuration with environment-specific overrides
- **Code Quality:** StyleCop.Analyzers, Roslynator, Meziantou.Analyzer, nullable reference types enabled
- **Testing Framework:** MSTest 4.0 with AwesomeAssertions, Moq, Testably.Abstractions

## Project Architecture

The Bootstrap module orchestrates the initialization of a WinUI application through a layered, composable architecture:

```text
Application Entry Point
    ↓
Bootstrapper.Configure()      [Initialize DI, Logging, PathFinder]
    ↓
Optional Configuration Methods [WithMvvm, WithRouting, WithWinUI]
    ↓
Bootstrapper.Build()          [Build Host, Finalize Services]
    ↓
Bootstrapper.Run()            [Start Application]
```

### Key Components

- **`Bootstrapper`** - Main orchestrator class managing the startup sequence and fluent API
- **`BootstrapOptions`** - Configuration object for bootstrap-time settings (logging sinks, format providers, log levels)
- **`PathFinderService`** - Resolves application-specific file paths (config, logs, data directories)
- **`IHostBuilder`** - Underlying .NET Generic Host builder for service configuration
- **`IContainer`** - DryIoc dependency injection container (accessible after `Build()`)

### Integration with DroidNet Subsystems

The Bootstrap module orchestrates these core DroidNet subsystems:

- **Hosting** - `.NET Generic Host` integration with `UserInterfaceHostedService` for UI lifecycle management
- **Config** - JSON-based configuration with environment-specific files
- **MVVM** - View locators and view-model binding converters
- **Routing** - Angular-inspired URL-based navigation system
- **Mvvm.Generators** - Source generators for view-viewmodel wiring

## Getting Started

### Prerequisites

- .NET 9 SDK or later
- Windows 10 version 22H2 or later (Windows 10.0.26100.0+)
- Visual Studio 2022 or VS Code with C# support

### Installation

Add the NuGet package reference to your project:

```xml
<PackageReference Include="DroidNet.Bootstrap" />
```

Package versions are centrally managed in `Directory.Packages.props`. Do not manually specify versions in individual project files.

### Basic Setup

Create a `Program.cs` file with the following bootstrap sequence:

```csharp
using System.Globalization;
using DroidNet.Bootstrap;
using DroidNet.Routing;

class Program
{
    [STAThread]
    static void Main(string[] args)
    {
        var bootstrap = new Bootstrapper(args)
            .Configure(options => options
                .WithLoggingAbstraction()
                .WithOutputConsole(capacity: 5000)
                .WithOutputLog(CultureInfo.CurrentCulture))
            .WithMvvm()
            .WithRouting(new Routes())
            .WithWinUI<App>()
            .Build();

        try
        {
            bootstrap.Run();
        }
        finally
        {
            bootstrap.Dispose();
        }
    }
}
```

### Optional Logging Sinks

To use the OutputConsole or OutputLog controls, ensure their NuGet packages are installed:

```xml
<!-- Optional: For OutputConsole control integration -->
<PackageReference Include="DroidNet.Controls.OutputConsole" />

<!-- Optional: For OutputLog (rich text) control integration -->
<PackageReference Include="DroidNet.Controls.OutputLog" />
```

These packages are optional; the Bootstrap module gracefully handles missing sinks.

## Project Structure

```text
projects/Bootstrap/
├── README.md                          # This file
├── src/
│   ├── Bootstrap.csproj              # Main project file
│   ├── Bootstrapper.cs               # Core bootstrapper class
│   ├── Bootstrapper.Logs.cs          # Logging infrastructure (partial)
│   ├── BootstrapOptions.cs           # Configuration options
│   └── BootstrapperException.cs      # Custom exception type
└── tests/
    ├── Bootsrap.UI.Tests.csproj      # UI integration tests (MSTest)
    └── BootstrapperTests.cs          # Test implementations
```

## Key Features

### Fluent Configuration API

```csharp
var bootstrap = new Bootstrapper(args)
    .Configure(options => /* ... */)
    .WithMvvm()
    .WithRouting(routes)
    .WithWinUI<App>()
    .Build();
```

### Dependency Injection

- **Container:** DryIoc-based service resolution
- **Lifecycle:** Singleton, Transient registration support
- **Access:** Available via `bootstrap.Container` after `Build()`

### Comprehensive Logging

- **Provider:** Serilog with Microsoft.Extensions.Logging integration
- **Multiple Sinks:** Console, Debug, File, and optional UI controls
- **Dynamic Control:** Optional `LoggingLevelSwitch` for runtime log level adjustments
- **Templating:** Expression-based filtering and output customization

### MVVM Infrastructure

- View location and automatic view resolution
- ViewModelToView binding converter
- Integration with source-generated view-viewmodel wiring

### Routing System

- URL-based navigation with outlets (primary, modal, popup)
- Matrix parameters and nested routes
- Hierarchical route organization

### WinUI 3 Hosting

- Integrated UI lifecycle management
- Optional UI/host lifetime linking
- Standard WinUI 3 application hosting

### File System Abstraction

- Testable file system operations via `Testably.Abstractions`
- Application path resolution via `PathFinder` service
- Environment-specific configuration file handling

## Development Workflow

### Initial Setup

After cloning the repository, run the initialization script once:

```powershell
pwsh ./init.ps1
```

This sets up the .NET SDK, restores tools, and configures pre-commit hooks.

### Building

#### Build the specific Bootstrap module

```powershell
dotnet build projects/Bootstrap/src/Bootstrap.csproj
```

#### Build entire Bootstrap solution

```powershell
dotnet build projects/Bootstrap/Bootstrap.sln
```

#### Build all projects (from repository root)

```powershell
dotnet build AllProjects.sln
```

### Running Tests

#### Run Bootstrap tests only

```powershell
dotnet test --project projects/Bootstrap/tests/Bootsrap.UI.Tests.csproj
```

#### Run with code coverage

```powershell
dotnet test --project projects/Bootstrap/tests/Bootsrap.UI.Tests.csproj `
    /p:CollectCoverage=true `
    /p:CoverletOutputFormat=lcov
```

#### Run all tests

```powershell
dotnet test
```

### Preferred Development Commands

For faster iteration, use targeted commands rather than full solution builds:

```powershell
# Quick rebuild
dotnet build projects/Bootstrap/src/Bootstrap.csproj

# Quick test run
dotnet test --project projects/Bootstrap/tests/Bootsrap.UI.Tests.csproj

# Clean build artifacts (all projects)
.\clean.ps1
```

### Solution Generation

To regenerate and open the solution in Visual Studio:

```powershell
cd projects
.\open.cmd
```

This uses `dotnet slngen` to generate an optimal solution structure.

## Coding Standards

The Bootstrap module adheres to the DroidNet repository coding standards:

### C# Language Rules

- **Version:** C# 13 with preview features enabled
- **Nullable References:** Enabled (`#nullable enable`)
- **Implicit Usings:** Enabled to reduce boilerplate
- **Access Modifiers:** Always explicit (`public`, `private`, `protected`, `internal`)
- **This Reference:** Always use `this.` for instance member access
- **Braces:** Required for all control structures, even single-line statements

### Language Options (from Directory.build.props)

```xml
<PropertyGroup>
    <LangVersion>preview</LangVersion>
    <Features>Strict</Features>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <AnalysisLevel>latest</AnalysisLevel>
    <AnalysisMode>All</AnalysisMode>
    <EnforceCodeStyleInBuild>true</EnforceCodeStyleInBuild>
</PropertyGroup>
```

### Naming and Organization

- Follow PascalCase for public members
- Use descriptive names for methods and properties
- Group related functionality in partial classes (e.g., `Bootstrapper.Logs.cs`)
- No use of regions for code organization in tests

### WinUI/MVVM Practices

- Minimal code-behind; favor ViewModels for logic
- Use data binding over direct UI manipulation
- Follow the `[ViewModel(typeof(TViewModel))]` attribute pattern for view-viewmodel association
- Leverage source-generated wiring from `Mvvm.Generators`

For complete styling details, see [`.github/instructions/csharp_coding_style.instructions.md`](../../.github/instructions/csharp_coding_style.instructions.md).

## Testing

### Testing Framework

- **Framework:** MSTest 4.0
- **Assertions:** AwesomeAssertions for fluent, readable assertions
- **Mocking:** Moq for interface and dependency mocking
- **File System:** Testably.Abstractions for testable file I/O

### Test Organization

Tests follow the **AAA (Arrange-Act-Assert)** pattern with descriptive naming:

```csharp
[TestClass]
public class BootstrapperTests
{
    [TestMethod]
    public void Configure_WithValidOptions_InitializesServices()
    {
        // Arrange
        var bootstrapper = new Bootstrapper(Array.Empty<string>());

        // Act
        bootstrapper.Configure();

        // Assert
        Assert.IsNotNull(bootstrapper.FileSystemService);
        Assert.IsNotNull(bootstrapper.PathFinderService);
    }
}
```

### Naming Convention

Test methods follow the pattern: `MethodName_Scenario_ExpectedBehavior`

Examples:

- `Configure_WithValidOptions_InitializesServices`
- `Build_CalledMultipleTimes_ThrowsInvalidOperationException`
- `Run_WithoutBuild_BuildsAutomatically`

### Running Tests

```powershell
# Run all Bootstrap tests
dotnet test projects/Bootstrap/tests/Bootsrap.UI.Tests.csproj

# Run specific test class
dotnet test projects/Bootstrap/tests/Bootsrap.UI.Tests.csproj `
    --filter "ClassName=DroidNet.Bootstrap.Tests.BootstrapperTests"

# With detailed output
dotnet test projects/Bootstrap/tests/Bootsrap.UI.Tests.csproj -v detailed
```

### Test Project Conventions

- Test projects must end with `.Tests` suffix (UI tests: `.UI.Tests`)
- Tests are located in the `tests/` directory parallel to `src/`
- No regions or complex organization; flat test class structure
- Shared test utilities are in `projects/TestHelpers/`

For comprehensive testing guidelines, see [`.github/prompts/csharp-mstest.prompt.md`](../../.github/prompts/csharp-mstest.prompt.md).

## API Reference

### Bootstrapper Class

#### Methods

- **`Configure(Action<BootstrapOptions>? configure = null)`**
  - Performs initial bootstrap configuration including logging, DI, and early services
  - Must be called first (or automatically called by `Build()`)
  - Returns: `Bootstrapper` instance for chaining

- **`WithMvvm()`**
  - Configures MVVM infrastructure
  - Registers: `IViewLocator`, `ViewModelToView` converter
  - Returns: `Bootstrapper` instance for chaining

- **`WithRouting(Routes config)`**
  - Configures routing with route definitions
  - Parameter: `Routes` configuration object
  - Returns: `Bootstrapper` instance for chaining

- **`WithWinUI<TApplication>(bool isLifetimeLinked = true)`**
  - Configures WinUI hosting and registers the application
  - Type Parameter: Subclass of `Microsoft.UI.Xaml.Application`
  - Parameter: `isLifetimeLinked` controls UI/host lifetime coupling
  - Returns: `Bootstrapper` instance for chaining

- **`Build()`**
  - Finalizes configuration and builds the application host
  - Returns: `IHost` instance
  - Throws: `InvalidOperationException` if called multiple times

- **`Run()`**
  - Starts the application host and blocks until shutdown
  - No return value (void)
  - Automatically calls `Build()` if needed

- **`Dispose()`**
  - Releases all managed and unmanaged resources
  - Called automatically or explicitly for cleanup

#### Properties

- **`PathFinderService`** - File path resolution service (available after `Configure()`)
- **`FileSystemService`** - File system abstraction (available after `Configure()`)
- **`Container`** - DryIoc container for service resolution (available after `Build()`)

### BootstrapOptions Class

Passed to `Configure()` method for logging customization:

- **`WithLoggingAbstraction()`** - Integrate Serilog with MEL abstraction
- **`WithOutputConsole(int capacity, LogEventLevel level, LoggingLevelSwitch? switch)`** - Register OutputConsole sink
- **`WithOutputLog(IFormatProvider provider, string? template, LogEventLevel level, LoggingLevelSwitch? switch, uint? themeId)`** - Register OutputLog sink

## Best Practices

### Startup Pattern

```csharp
// ✓ Good: Standard bootstrap sequence
var bootstrap = new Bootstrapper(args)
    .Configure(opts => opts.WithLoggingAbstraction())
    .WithMvvm()
    .WithRouting(new Routes())
    .WithWinUI<App>()
    .Build();
bootstrap.Run();
```

### Avoid Common Pitfalls

- ✗ Don't call configuration methods before `Configure()`
- ✗ Don't call `Build()` multiple times
- ✗ Don't access `Container` before calling `Build()`

### DI and Service Registration

- For simple scenarios, let Bootstrap handle DI setup
- For advanced registration, access `bootstrap.Container` after `Build()` to add custom services

### Logging Configuration

- Configure logging in the `Configure()` callback for bootstrap-time setup
- Use `Microsoft.Extensions.Logging` abstractions in application code
- Optional UI sinks (OutputConsole, OutputLog) gracefully degrade if packages are missing

## Contributing

### Code Changes

1. Follow the C# coding standards in [`.github/instructions/csharp_coding_style.instructions.md`](../../.github/instructions/csharp_coding_style.instructions.md)
2. Write tests for new functionality following AAA pattern and naming conventions
3. Ensure all tests pass: `dotnet test`
4. Build without warnings: `dotnet build`

### Adding New Features

- Keep the fluent API surface minimal and focused
- Consider impact on application startup time
- Document new configuration options in `BootstrapOptions`
- Add corresponding tests in `Bootsrap.UI.Tests.csproj`

### Pull Request Checklist

- [ ] Code follows C# coding standards
- [ ] All tests pass
- [ ] No compiler warnings
- [ ] README updated if behavior changed
- [ ] New public APIs have XML documentation comments
- [ ] Test coverage added for new functionality

## License

This project is part of the DroidNet repository. See [LICENSE](../../LICENSE) for details.

## References

- [DroidNet Main Repository](../../)
- [DroidNet Copilot Instructions](../../.github/copilot-instructions.md)
- [C# Coding Style Guide](../../.github/instructions/csharp_coding_style.instructions.md)
- [MSTest Conventions](../../.github/prompts/csharp-mstest.prompt.md)
- [Hosting Module](../Hosting/README.md)
- [Routing Module](../Routing/README.md)
- [MVVM Module](../Mvvm/README.md)

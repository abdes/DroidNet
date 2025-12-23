# Oxygen.Editor

A comprehensive WinUI 3 application that serves as the main editor for the Oxygen game engine ecosystem. Oxygen.Editor provides a complete integrated development environment for creating, managing, and editing projects within the Oxygen Engine framework.

## Overview

Oxygen.Editor is a modular desktop application built on WinUI 3 and .NET 9, designed to provide a professional-grade editing experience for game developers using the Oxygen Engine. The editor integrates multiple specialized modules to handle project management, world editing, data persistence, and interactive UI components.

## Technology Stack

- **Platform:** WinUI 3 with Microsoft.WindowsAppSDK 1.8+
- **Runtime:** .NET 9 (target framework: `net9.0-windows10.0.26100.0`)
- **Language:** C# 13 preview with nullable reference types enabled
- **DI/IoC:** DryIoc 6.0 preview via Microsoft.Extensions.DependencyInjection/Hosting
- **MVVM Framework:** CommunityToolkit.Mvvm 8.4
- **Data Persistence:** Entity Framework Core with SQLite
- **Logging:** Serilog with multiple sinks (Console, Debug, File)
- **UI Behaviors:** CommunityToolkit.WinUI.Behaviors, CommunityToolkit.WinUI.Collections
- **Code Generation:** Source generators for View-ViewModel wiring and dependency injection
- **Reactive:** System.Reactive for event stream handling

## Project Architecture

Oxygen.Editor is organized as a modular system with clear separation of concerns:

### Core Modules

- **Oxygen.Editor.App** (`src/`) - Main WinUI application entry point with bootstrapping and UI configuration
- **Oxygen.Editor.Core** - Core services and utilities, foundational extensions for framework classes
- **Oxygen.Editor.Data** - Data layer with Entity Framework Core models and persistence services
- **Oxygen.Editor.ProjectBrowser** - Project browsing, discovery, and template management UI
- **Oxygen.Editor.Projects** - Project metadata and project-related services
- **Oxygen.Storage** - File system abstraction and storage operations
- **Oxygen.Editor.WorldEditor** - World editing interface and viewport management
- **Oxygen.Editor.Interop** - Native interoperability layer for platform-specific operations

## Getting Started

### Prerequisites

- **.NET 9 SDK** (or installation via `init.ps1`)
- **Windows 10 Build 26100.0** or later (for Windows App SDK 1.8+ support)
- **Visual Studio 2022** (recommended) or VS Code with C# extension

### Installation & Setup

1. **Clone the repository:**

   ```powershell
   git clone https://github.com/abdes/DroidNet.git
   cd DroidNet
   ```

2. **Run initialization script (first time only):**

   ```powershell
   pwsh ./init.ps1
   ```

   This installs required .NET SDK, tools, and pre-commit hooks.

3. **Build the application:**

   ```powershell
   dotnet build projects/Oxygen.Editor/src/Oxygen.Editor.App.csproj
   ```

4. **Run the application:**

   ```powershell
   dotnet run --project projects/Oxygen.Editor/src/Oxygen.Editor.App.csproj
   ```

### Project Structure

```text
Oxygen.Editor/
├── src/                              # Main application
│   ├── App.xaml                      # Application UI shell
│   ├── Program.cs                    # Entry point with host configuration
│   ├── Oxygen.Editor/
│   │   └── WorldEditor/              # World editing workspace
│   ├── Services/                     # Application services
│   │   ├── IActivationService.cs
│   │   └── ActivationService.cs
│   ├── Styles/                       # XAML resource dictionaries
│   │   ├── FontSizes.xaml
│   │   ├── TextBlock.xaml
│   │   └── Thickness.xaml
│   ├── Properties/                   # Assembly configuration
│   └── Strings/                      # Localization resources
├── Oxygen.Editor.sln                # Solution file
└── README.md                         # This file
```

**Related Modules** (referenced from `projects/Oxygen.Editor.*` and used by the app):

- `Oxygen.Editor.Core` - Core services and utilities
- `Oxygen.Editor.Data` - Data layer with Entity Framework Core
- `Oxygen.Editor.ProjectBrowser` - Project browsing UI
- `Oxygen.Editor.Projects` - Project metadata services
- `Oxygen.Storage` - File system abstraction
- `Oxygen.Editor.WorldEditor` - World editing interface
- `Oxygen.Editor.Interop` - Native interoperability

**Shared Controls** (from `projects/Controls/`):

- `Controls.DynamicTree` - Dynamic tree view for hierarchical data
- `Controls.InPlaceEdit` - In-place editing control
- `Controls.OutputConsole` - Output and logging console

## Key Features

- **Integrated Project Management** - Create, open, and manage game projects with templates
- **World Editor** - Visual world editing with hierarchical scene management (via DynamicTree)
- **Data Persistence** - SQLite-backed data storage with Entity Framework Core
- **MVVM Architecture** - Source-generated View-ViewModel wiring for maintainability
- **Docking Framework** - Flexible window and panel docking (via Docking module)
- **Dynamic UI** - Responsive UI using WinUI 3 bindings and converters
- **In-Place Editing** - Inline content editing capabilities
- **Output Logging** - Integrated console for application logging and diagnostics

## Development Workflow

### Building & Testing

**Build specific module:**

```powershell
dotnet build projects/Oxygen.Editor/src/Oxygen.Editor.App.csproj
```

**Run tests for a module:**

```powershell
dotnet test projects/Oxygen.Editor.Core/tests/Oxygen.Editor.Core.Tests.csproj
```

**Clean artifacts:**

```powershell
.\clean.ps1
```

### Architecture & Patterns

#### MVVM with Source Generators

Views use the `[ViewModel(typeof(TViewModel))]` attribute for auto-wired binding:

```csharp
[ViewModel(typeof(MyViewModel))]
public sealed partial class MyView : UserControl
{
    public MyView()
    {
        this.InitializeComponent();
    }
}
```

The `Mvvm.Generators` package automatically generates initialization code.

#### Dependency Injection

Services are registered using the standard .NET hosting pattern. See `Program.cs` for configuration examples:

```csharp
bootstrap
    .Configure(options => options.WithOutputConsole())
    .WithMvvm()
    .WithRouting(MakeRoutes())
    .WithWinUI<App>()
    .Build();
```

Services can be registered in the DryIoc container via the Hosting module.

#### Data Access with Entity Framework Core

The `Oxygen.Editor.Data` module uses EF Core with SQLite for persistence:

```csharp
// Entity models are defined in the data layer
// DbContext is configured in ConfigurePersistentStateDatabase()
```

### Code Organization & Standards

- **Namespaces:** Follow project structure (e.g., `Oxygen.Editor.ProjectBrowser.Views`)
- **Access Modifiers:** Explicit on all types and members
- **Member Access:** Use `this.` prefix for instance members
- **Nullable:** Enabled with strict null checking
- **ImplicitUsings:** Enabled for common namespaces

See `.github/instructions/csharp_coding_style.instructions.md` for detailed style guidelines.

## Testing

The project uses **MSTest** with the AAA (Arrange-Act-Assert) pattern:

- **Test Projects:** Named with `.Tests` suffix (e.g., `Oxygen.Editor.Core.Tests`)
- **UI Tests:** Named with `.UI.Tests` suffix
- **Framework:** MSTest 4.0, AwesomeAssertions, optional Moq

**Run all tests:**

```powershell
dotnet test
```

**Run with coverage:**

```powershell
dotnet test /p:CollectCoverage=true /p:CoverletOutputFormat=lcov
```

### Test Example

```csharp
[TestClass]
public class MyServiceTests
{
    [TestMethod]
    public void MyMethod_GivenCondition_ExpectedBehavior()
    {
        // Arrange
        var service = new MyService();

        // Act
        var result = service.DoSomething();

        // Assert
        result.Should().Be(expected);
    }
}
```

## Contributing

1. **Fork & clone** the repository
2. **Create a feature branch:** `git checkout -b feature/your-feature`
3. **Follow coding standards** from `.github/instructions/csharp_coding_style.instructions.md`
4. **Write tests** for new functionality
5. **Commit with clear messages** (follow conventional commits)
6. **Push and create a Pull Request**

### Code Review Expectations

- Code adheres to the C# style guide
- Tests are included and passing
- Commits are atomic and well-documented
- No broad API surface changes without design discussion

## Dependencies Management

All NuGet package versions are centrally managed in `Directory.Packages.props` at the repository root. **Do not** specify versions in individual `.csproj` files.

## Logging & Debugging

Oxygen.Editor uses **Serilog** for comprehensive logging:

```csharp
private readonly ILogger<MyClass> _logger;

public MyClass(ILogger<MyClass> logger)
{
    _logger = logger;
}

// Usage
_logger.LogInformation("Application started");
_logger.LogError(ex, "An error occurred");
```

Logs are output to:

- **Console** (during development)
- **Debug window** (Visual Studio)
- **File** (persisted logs)

## License

Distributed under the MIT License. See [LICENSE](../../LICENSE) for details.

## See Also

- [DroidNet Repository](https://github.com/abdes/DroidNet) - Parent mono-repo
- [Oxygen.Engine](../Oxygen.Engine/) - Game engine implementation
- [Hosting Module](../Hosting/) - WinUI hosting infrastructure
- [Routing Module](../Routing/) - Navigation framework
- [MVVM Generators](../Mvvm.Generators/) - Source generators for View-ViewModel wiring

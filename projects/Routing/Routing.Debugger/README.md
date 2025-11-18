# Routing.Debugger

A comprehensive debugging and visualization tool for the DroidNet routing system. This WinUI 3 application provides an interactive interface to inspect, test, and debug URL-based navigation, route recognition, and application state in real-time.

## Table of Contents

- [Project Description](#project-description)
- [Technology Stack](#technology-stack)
- [Architecture](#architecture)
- [Getting Started](#getting-started)
- [Project Structure](#project-structure)
- [Key Features](#key-features)
- [Development Workflow](#development-workflow)
- [Coding Standards](#coding-standards)
- [Testing](#testing)
- [Contributing](#contributing)
- [License](#license)

## Project Description

Routing.Debugger is a specialized WinUI application designed to help developers understand and debug the DroidNet routing system. It visualizes the routing configuration, allows real-time navigation testing, and displays detailed information about route recognition, URL parsing, and router state. The application is built using the DroidNet framework components including Routing, Hosting, MVVM, Aura windowing, and Docking.

This tool is essential for:

- Verifying route configurations and patterns
- Testing complex navigation scenarios
- Understanding route activation and parameter passing
- Debugging navigation failures
- Visualizing application state during navigation
- Exploring URL tree structure and route matching

## Technology Stack

- **Language**: C# 13 (with nullable reference types enabled)
- **.NET Target Framework**: .NET 9.0 Windows (`net9.0-windows10.0.26100.0`)
- **UI Framework**: WinUI 3 with Microsoft.WindowsAppSDK 1.8+
- **Key DroidNet Components**:
  - `Routing.Router`: Core routing engine
  - `Routing.Abstractions`: Routing interfaces and models
  - `Routing.WinUI`: WinUI integration
  - `Routing.Debugger.UI`: UI components and view models
  - `Hosting`: .NET Generic Host integration with WinUI
  - `Mvvm.Generators`: Source-generated View-ViewModel wiring
  - `Aura`: Window management and decoration
  - `Docking`: Flexible docking layout framework
  - `Bootstrap`: Application initialization
  - `Config`: Configuration management
- **Key Dependencies**:
  - CommunityToolkit.Mvvm: MVVM framework
  - Serilog: Structured logging
  - Microsoft.Extensions.DependencyInjection/Hosting
  - DryIoc: Dependency injection container

## Architecture

The Routing.Debugger application follows a modular, layered architecture:

### Application Layers

```text
┌─────────────────────────────────────────┐
│         WinUI User Interface            │
│  (XAML Views + Docking Framework)       │
├─────────────────────────────────────────┤
│        View Models (MVVM)               │
│  (Routing.Debugger.UI namespace)        │
├─────────────────────────────────────────┤
│        Routing System                   │
│  (Core routing, navigation, state)      │
├─────────────────────────────────────────┤
│  .NET Generic Host with Services        │
│  (DI, Logging, Configuration)           │
└─────────────────────────────────────────┘
```

### Key Components

**Main Application Container**: `App.xaml.cs` / `Program.cs`

- Uses custom Main entry point for full control over Host initialization
- Configures bootstrapper with MVVM, Routing, and WinUI
- Manages application lifecycle and dependency injection
- Injects: `IRouter`, `IValueConverter` (VmToView), `IWindowManagerService`

**UI Modules** (under `Routing.Debugger.UI`):

- **Welcome**: Initial landing view
- **Shell**: Main application layout container
- **Config**: Route configuration display and management
- **UrlTree**: URL parsing and tree visualization
- **State**: Router state inspection
- **Workspace**: Multi-pane debugging workspace with docking
- **Docks**: Custom docking pane implementations

**Entry Navigation**: Complex multi-outlet navigation configured in `App.OnLaunched()`:

```text
/workspace(
  dock:(
    app:Welcome                           // Welcome tab
    routes:Config                         // Routes config
    url-tree:Parser                       // URL tree viewer
    router-state:Recognizer               // State inspector
  )
)
```

### Dependency Injection & Hosting

- Uses `.NET Generic Host` via `Bootstrapper` for service registration
- `DryIoc` container for dependency resolution
- Services registered through `ConfigureApplicationServices()` in Program.cs
- Settings initialized via `InitializeSettings()` before UI launch
- Supports keyed services (e.g., `[FromKeyedServices("VmToView")]`)

### MVVM & View Wiring

- ViewModels inherit from `ObservableObject` (CommunityToolkit.Mvvm)
- Views use `[ViewModel(typeof(TViewModel))]` attribute for source-generated wiring
- Value converters registered in DI and injected into views
- ViewModel-to-View converter available as XAML static resource

### Navigation & Routing

- Uses `IRouter` for multi-outlet navigation
- Supports named outlets: `primary`, `modal`, `popup`
- Complex URL patterns with docking parameters
- Outlets bound to specific panes in the docking layout
- Navigation failures trigger application shutdown (`IHostApplicationLifetime.StopApplication()`)

## Getting Started

### Prerequisites

- **.NET 9.0 SDK** or later
- **Visual Studio 2022** 17.12 or later (recommended)
- **Windows 10 Build 26100** or later for WinUI 3
- **PowerShell 7+** for build scripts

### Initial Setup

1. **Clone the repository** and initialize:

   ```powershell
   pwsh ./init.ps1
   ```

   This runs only once and installs the .NET SDK, restores tools, and configures pre-commit hooks.

2. **Open the solution**:

   ```powershell
   cd projects
   .\open.cmd
   ```

   This regenerates and opens `Projects.sln` using `dotnet slngen`.

3. **Build the project**:

   ```powershell
   dotnet build projects/Routing/Routing.Debugger/src/Routing.Debugger.csproj
   ```

4. **Run the application**:

   ```powershell
   dotnet run --project projects/Routing/Routing.Debugger/src/Routing.Debugger.csproj
   ```

### Configuration

The application reads configuration from standard .NET configuration sources (appsettings.json, environment variables, etc.). Logging is configured via Serilog with sinks for Console, Debug, and File output.

## Project Structure

```text
Routing.Debugger/
├── README.md                            # This file
├── open.cmd                             # Helper script to open solution
├── src/
│   ├── Routing.Debugger.csproj         # Main project file
│   ├── App.xaml                         # Application root resource
│   ├── App.xaml.cs                      # Application code-behind
│   ├── Program.cs                       # Custom Main entry point
│   ├── Package.appxmanifest            # Windows packaging manifest
│   ├── app.manifest                     # Application manifest
│   ├── Properties/
│   │   ├── launchsettings.json         # Debug launch configuration
│   │   ├── GlobalSuppressions.cs       # Code analyzer suppressions
│   │   └── PublishProfiles/            # Publish configurations
│   ├── Assets/                          # Application icons and images
│   ├── Styles/                          # XAML style definitions
│   │   ├── FontSizes.xaml
│   │   ├── TextBlock.xaml
│   │   └── Thickness.xaml
│   ├── Strings/                         # Localization resources
│   │   └── en-us/Resources.resw
│   └── Welcome/                         # Welcome tab UI
│       ├── WelcomeView.xaml
│       ├── WelcomeView.xaml.cs
│       └── WelcomeViewModel.cs
```

The bulk of UI components and view models reside in the separate `Routing.Debugger.UI` project (not shown above), which is referenced by this project and contains:

- Config, Shell, UrlTree, State, Workspace UI modules
- Associated ViewModels for each module
- Docking pane implementations

## Key Features

### 1. **Route Configuration Inspector**

- View the complete route tree with all configured routes
- Display route paths, outlet names, and component types
- Inspect route parameters and guards
- Visual hierarchy showing nested routes

### 2. **URL Navigation Tester**

- Enter custom URLs and test route recognition
- Real-time URL parsing and tree visualization
- Segment group and parameter extraction
- Match result analysis

### 3. **Router State Inspector**

- View current router state snapshot
- Active route tree visualization
- Activated component display
- Parameter values and context information

### 4. **Multi-Pane Debugger Workspace**

- Flexible docking layout with resizable panes
- Multiple named outlets for side-by-side inspection
- Tab management for different debug views
- Persistent layout configuration

### 5. **Navigation History & Testing**

- Track navigation operations
- Visualize navigation lifecycle events
- Test complex multi-outlet scenarios
- Error reporting and debugging

## Development Workflow

### Building

**Targeted build** (recommended for iterative development):

```powershell
dotnet build projects/Routing/Routing.Debugger/src/Routing.Debugger.csproj
```

**Full solution build**:

```powershell
dotnet build projects/Projects.sln
```

### Running

**Debug run**:

```powershell
dotnet run --project projects/Routing/Routing.Debugger/src/Routing.Debugger.csproj
```

**Release run**:

```powershell
dotnet run --project projects/Routing/Routing.Debugger/src/Routing.Debugger.csproj --configuration Release
```

### Debugging

- Open `projects/Projects.sln` in Visual Studio
- Set breakpoints in C# code or XAML code-behind
- Press F5 to debug or Ctrl+F5 to run without debugging
- Use the Debug console for output inspection

### Publishing

Publish profiles are configured in `Properties/PublishProfiles/`:

```powershell
dotnet publish projects/Routing/Routing.Debugger/src/Routing.Debugger.csproj `
  -c Release `
  -p:PublishProfile=win-x64
```

## Coding Standards

This project follows the DroidNet repository C# coding standards:

### C# Style Requirements

- **C# 13 preview** features with nullable reference types enabled (`nullable=enable`)
- **Explicit access modifiers**: Always use `public`, `private`, `protected`, etc.
- **Instance member prefix**: Use `this.` for all instance member access
- **Code analyzers**: StyleCop.Analyzers, Roslynator, Meziantou.Analyzer enabled
- **Implicit usings**: Use project-level `ImplicitUsings` imports

### MVVM Conventions

- **ViewModels**: Inherit from `ObservableObject` or framework base classes
- **View wiring**: Use `[ViewModel(typeof(TViewModel))]` attribute on View classes
- **Code-behind**: Minimize code-behind; use ViewModels for business logic
- **Binding**: Prefer data binding over code-behind event handlers

### Naming Conventions

- **Classes/Types**: PascalCase
- **Properties/Methods**: PascalCase
- **Private fields**: `_camelCase`
- **Parameters**: camelCase
- **Constants**: PascalCase or UPPER_SNAKE_CASE

### Documentation

- Use **XML documentation comments** for public types and members
- Include parameter descriptions and return type documentation
- Add `<remarks>` sections for non-obvious implementation details

For detailed coding style guidelines, see `.github/instructions/csharp_coding_style.instructions.md`.

## Testing

This project currently has minimal unit tests. Testing the debugger application typically involves:

1. **Manual functional testing** of routing scenarios
2. **Visual verification** of UI rendering and layout
3. **Integration testing** with the Routing system

If adding tests, follow these conventions:

- **Framework**: MSTest (not xUnit or NUnit)
- **Test Project**: Name must end with `.Tests` or `.UI.Tests`
- **Test Methods**: Use `[TestClass]` and `[TestMethod]` attributes
- **Naming Pattern**: `MethodName_Scenario_ExpectedBehavior`
- **Pattern**: Arrange-Act-Assert (AAA)
- **Dependencies**: MSTest, AwesomeAssertions, Moq (optional)

Example:

```csharp
[TestClass]
public class UrlParsingTests
{
    [TestMethod]
    public void ParseUrl_ValidWorkspaceUrl_ReturnsCorrectSegments()
    {
        // Arrange
        var parser = new UrlParser();
        var url = "/workspace(dock:(app:Welcome//routes:Config))";

        // Act
        var result = parser.Parse(url);

        // Assert
        result.Should().NotBeNull();
        result.Segments.Should().HaveCount(2);
    }
}
```

To run tests:

```powershell
dotnet test projects/Routing/Routing.Debugger.Tests/Routing.Debugger.Tests.csproj
```

## Contributing

When contributing to this project:

1. **Follow the coding standards** outlined above
2. **Reference existing patterns** in similar projects (e.g., Routing.DemoApp)
3. **Use MVVM patterns** for new UI features
4. **Keep PRs focused** with small, reviewable changes
5. **Update documentation** if adding new features or changing behavior
6. **Run tests** before submitting changes:

   ```powershell
   dotnet build projects/Routing/Routing.Debugger/src/Routing.Debugger.csproj
   ```

### Adding New Debug Features

When adding new debugging capabilities:

1. Create a new feature module under `Routing.Debugger.UI`
2. Implement the View (XAML) and ViewModel (C#)
3. Register in the DI container via `ConfigureApplicationServices()`
4. Add navigation route configuration in `MakeRoutes()`
5. Wire into the main workspace docking layout

## License

Distributed under the MIT License. See the LICENSE file in the repository root for details.

---

**References**:

- [.github/copilot-instructions.md](../../.github/copilot-instructions.md) - Repository architecture and patterns
- [.github/instructions/csharp_coding_style.instructions.md](../../.github/instructions/csharp_coding_style.instructions.md) - C# coding standards
- [projects/Routing/](../Routing/) - Core routing system documentation
- [projects/Hosting/README.md](../Hosting/README.md) - Hosting and DI setup patterns
- [projects/Mvvm.Generators/README.md](../Mvvm.Generators/README.md) - MVVM source generator documentation
